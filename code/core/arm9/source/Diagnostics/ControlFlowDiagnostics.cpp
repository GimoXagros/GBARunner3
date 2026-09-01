#include "ControlFlowDiagnostics.h"

#ifdef GBAR3_CONTROL_FLOW_DIAGNOSTICS

#include <cstddef>
#include <string.h>
#include "Cpsr.h"
#include "Fat/ff.h"
#include "MemoryEmulator/RomDefs.h"
#include "SdCache/SdCache.h"
#include "JitPatcher/JitCommon.h"
#include "VirtualMachine/VMDtcm.h"

namespace
{
#define CFDIAG_EWRAM [[gnu::section(".ewram"), gnu::noinline]]

constexpr u32 DiagnosticMagic = 0x46433347; // "G3CF"
constexpr u32 DiagnosticVersion = 1;
constexpr u32 RingCapacity = 128;
constexpr u16 KeyA = 1 << 0;
constexpr u16 KeySelect = 1 << 2;
constexpr u32 InvalidValue = 0xFFFFFFFF;
constexpr u32 PersistFrameInterval = 15;

enum class DiagnosticStatus : u32
{
    Ready = 1,
    Armed = 2,
    Checkpoint = 3,
    Emergency = 4
};

enum class PersistReason : u32
{
    Boot = 1,
    Arm = 2,
    Input = 3,
    Periodic = 4,
    NotImplemented = 5,
    RepeatedPrefetchAbort = 6,
    RepeatedHicodeMiss = 7
};

enum class EventType : u32
{
    Arm = 1,
    Input = 2,
    VBlank = 3,
    ArmB = 10,
    ArmBl = 11,
    ArmBx = 12,
    ArmLdrPc = 13,
    ArmLdmPc = 14,
    ArmAluPc = 15,
    ThumbB = 20,
    ThumbBCond = 21,
    ThumbBlPrefix = 22,
    ThumbBl = 23,
    ThumbBx = 24,
    ThumbMovPc = 25,
    ThumbAddPc = 26,
    ThumbPopPc = 27,
    Swi = 30,
    Undefined = 31,
    PrefetchAbort = 32,
    HicodeMiss = 33,
    HicodeMap = 34,
    SdCacheLoad = 35,
    SdCacheEvict = 36,
    NotImplemented = 37
};

enum EventState : u32
{
    StateArmed = 1 << 0,
    StateThumb = 1 << 1,
    StateJitPatch = 1 << 2,
    StateRuntime = 1 << 3
};

struct DiagnosticEvent
{
    u32 sequence;
    u32 type;
    u32 sourceGuestPc;
    u32 sourceExecutionPc;
    u32 instruction;
    u32 state;
    u32 cpsr;
    u32 lr;
    u32 rawTarget;
    u32 normalizedGuestTarget;
    u32 finalExecutionTarget;
    u32 sourceRomBlock;
    u32 targetRomBlock;
    u32 sourceCacheBlock;
    u32 targetCacheBlock;
    u32 jitState;
    u32 hicodeBlock;
    u32 hicodeMask;
    u32 mpuRegion4;
    u32 prefetchAbortCount;
    u32 undefinedCount;
    u32 hicodeMissCount;
    u32 sdCacheLoadCount;
    u32 aux;
};

struct DiagnosticHeader
{
    u32 magic;
    u32 version;
    u32 headerSize;
    u32 recordSize;
    u32 capacity;
    u32 writeIndex;
    u32 totalEvents;
    u32 gameCode;
    u32 romSize;
    u32 checkpointSequence;
    u32 status;
    u32 flags;
    u32 checksum;
    u32 reason;
    u32 fileResult;
    u32 armSequence;
};

static_assert(sizeof(DiagnosticEvent) == 96);
static_assert(sizeof(DiagnosticHeader) == 64);

[[gnu::section(".ewram.bss")]] DiagnosticEvent sRing[RingCapacity];
[[gnu::section(".ewram.bss")]] char sPathA[512];
[[gnu::section(".ewram.bss")]] char sPathB[512];
u32 sWriteIndex;
u32 sTotalEvents;
u32 sEventSequence;
u32 sCheckpointSequence;
u32 sArmSequence;
u32 sGameCode;
u32 sRomSize;
u32 sFrameCount;
u32 sPrefetchAbortCount;
u32 sUndefinedCount;
u32 sHicodeMissCount;
u32 sSdCacheLoadCount;
u32 sPendingEventIndex;
u32 sPendingEventSequence;
u32 sLastPrefetchAddress;
u32 sRepeatedPrefetchCount;
u32 sLastHicodeMissAddress;
u32 sRepeatedHicodeMissCount;
u16 sLastKeysDown;
bool sArmed;
bool sForcePersist;
bool sWritePathB;
FRESULT sLastFileResult;
DiagnosticStatus sStatus;
PersistReason sPersistReason;

extern "C" u32 vm_irqSavedLR;
extern "C" u32 memu_inst_addr;
extern "C" u32 gHicodeState[2];

CFDIAG_EWRAM u32 readMpuRegion4()
{
    u32 config;
    asm volatile("mrc p15, 0, %0, c6, c4, 0\n" : "=r" (config));
    return config;
}

CFDIAG_EWRAM u32 normalizeRomMirror(u32 address)
{
    if (address >= 0x08000000 && address < 0x0E000000)
        return address & ~0x06000000;
    return address;
}

CFDIAG_EWRAM u32 toGuestAddress(u32 address)
{
    const u32 stateBit = address & 1;
    const u32 alignedAddress = address & ~1u;
    if (alignedAddress >= ROM_LINEAR_DS_ADDRESS && alignedAddress < ROM_LINEAR_END_DS_ADDRESS)
    {
        return alignedAddress - ROM_LINEAR_DS_ADDRESS + ROM_LINEAR_GBA_ADDRESS + stateBit;
    }

    const u32 cacheAddress = sdc_getRomAddressForCachePointer(
        reinterpret_cast<const void*>(alignedAddress));
    if (cacheAddress != InvalidValue)
        return cacheAddress | stateBit;

    return normalizeRomMirror(address);
}

CFDIAG_EWRAM u32 toExecutionAddress(u32 guestAddress)
{
    const u32 normalizedAddress = normalizeRomMirror(guestAddress);
    const u32 stateBit = normalizedAddress & 1;
    const u32 alignedAddress = normalizedAddress & ~1u;
    if (alignedAddress >= ROM_LINEAR_GBA_ADDRESS && alignedAddress < ROM_LINEAR_END_GBA_ADDRESS)
    {
        return alignedAddress - ROM_LINEAR_GBA_ADDRESS + ROM_LINEAR_DS_ADDRESS + stateBit;
    }
    return normalizedAddress;
}

CFDIAG_EWRAM u32 getRomBlock(u32 guestAddress)
{
    const u32 normalizedAddress = normalizeRomMirror(guestAddress) & ~1u;
    if (normalizedAddress < 0x08000000 || normalizedAddress >= 0x0A000000)
        return InvalidValue;
    return (normalizedAddress - 0x08000000) >> SDC_BLOCK_SHIFT;
}

CFDIAG_EWRAM u32 getCacheBlock(u32 address)
{
    u32 result = sdc_getCacheBlockIndexForPointer(
        reinterpret_cast<const void*>(address & ~1u));
    if (result != InvalidValue)
        return result;

    const u32 romBlock = getRomBlock(toGuestAddress(address));
    if (romBlock == InvalidValue)
        return InvalidValue;
    return sdc_getCacheBlockIndexForRomBlock(romBlock);
}

CFDIAG_EWRAM bool isJittableAddress(u32 address)
{
    const u32 alignedAddress = address & ~1u;
    return
        (alignedAddress >= ROM_LINEAR_DS_ADDRESS && alignedAddress < ROM_LINEAR_END_DS_ADDRESS) ||
        (alignedAddress >= 0x08000000 && alignedAddress < 0x0E000000) ||
        (alignedAddress >= 0x02000000 && alignedAddress < 0x02040000) ||
        (alignedAddress >= 0x03000000 && alignedAddress < 0x04000000) ||
        (alignedAddress >= 0x06000000 && alignedAddress < 0x06018000) ||
        sdc_getCacheBlockIndexForPointer(reinterpret_cast<const void*>(alignedAddress)) != InvalidValue;
}

CFDIAG_EWRAM EventType classifyArmInstruction(u32 instruction, bool patched)
{
    if ((!patched && (instruction & 0x0E000000) == 0x0A000000) ||
        (patched && (instruction & 0x0E000000) == 0x0C000000))
    {
        return instruction & 0x01000000 ? EventType::ArmBl : EventType::ArmB;
    }
    if ((!patched && (instruction & 0x0FFFFFF0) == 0x012FFF10) ||
        (patched && (instruction & 0x0FFFFFF0) == 0x01B00090))
        return EventType::ArmBx;
    if ((!patched && (instruction & 0x0E108000) == 0x08108000) ||
        (patched && (instruction & 0x0E500010) == 0x06400010))
        return EventType::ArmLdmPc;
    if ((!patched && (instruction & 0x0C50F000) == 0x0410F000) ||
        (patched && (instruction & 0x0F900810) == 0x0E800010))
        return EventType::ArmLdrPc;
    if ((!patched && (((instruction & 0x0E00F010) == 0x0000F000) ||
                      ((instruction & 0x0E00F010) == 0x0000F010) ||
                      ((instruction & 0x0E00F000) == 0x0200F000))) ||
        (patched && (instruction & 0x0E000000) == 0x0E000000))
        return EventType::ArmAluPc;
    if ((instruction & 0x0F000000) == 0x0F000000)
        return EventType::Swi;
    return EventType::Undefined;
}

CFDIAG_EWRAM EventType classifyThumbInstruction(u32 instruction, bool patched)
{
    if (!patched)
    {
        if ((instruction & 0xF000) == 0xD000)
            return EventType::ThumbBCond;
        if ((instruction & 0xF800) == 0xE000)
            return EventType::ThumbB;
        if ((instruction & 0xF800) == 0xF000)
            return EventType::ThumbBlPrefix;
        if ((instruction & 0xF800) == 0xF800)
            return EventType::ThumbBl;
        if ((instruction & 0xFF87) == 0x4700)
            return EventType::ThumbBx;
        if ((instruction & 0xFF87) == 0x4487)
            return EventType::ThumbAddPc;
        if ((instruction & 0xFF87) == 0x4687)
            return EventType::ThumbMovPc;
        if ((instruction & 0xFF00) == 0xBD00)
            return EventType::ThumbPopPc;
        if ((instruction & 0xFF00) == 0xDF00)
            return EventType::Swi;
        return EventType::Undefined;
    }

    if ((instruction & 0xFF80) == 0xBB80)
        return EventType::ThumbPopPc;
    if ((instruction & 0xFA00) == 0xB200)
        return EventType::ThumbB;
    if ((instruction & 0xF801) == 0xE801)
        return EventType::ThumbBl;
    if ((instruction & 0xFC00) == 0xB800)
        return EventType::ThumbBCond;
    if ((instruction & 0xFF0F) == 0xB100)
        return EventType::ThumbBx;
    if ((instruction & 0xFF0F) == 0xDE00)
        return EventType::ThumbMovPc;
    if ((instruction & 0xFF0F) == 0xBF00)
        return EventType::ThumbAddPc;
    return EventType::Undefined;
}

CFDIAG_EWRAM u32 calculateArmBranchTarget(u32 sourceGuestPc, u32 instruction)
{
    const s32 offset = static_cast<s32>(instruction << 8) >> 6;
    return normalizeRomMirror(sourceGuestPc + 8 + offset);
}

CFDIAG_EWRAM u32 calculateThumbBranchTarget(
    u32 sourceGuestPc, u32 instruction, EventType type)
{
    if (type == EventType::ThumbBCond)
    {
        const s32 offset = static_cast<s32>(static_cast<s8>(instruction & 0xFF)) << 1;
        return normalizeRomMirror((sourceGuestPc & ~1u) + 4 + offset) | 1;
    }
    if (type == EventType::ThumbB)
    {
        const s32 offset = static_cast<s32>(instruction << 21) >> 20;
        return normalizeRomMirror((sourceGuestPc & ~1u) + 4 + offset) | 1;
    }
    return 0;
}

CFDIAG_EWRAM void pushEvent(
    EventType type, u32 sourceExecutionPc, u32 instruction, u32 state,
    u32 cpsr, u32 lr, u32 rawTarget, u32 aux, bool pending)
{
    if (!sArmed)
        return;

    const u32 irqState = arm_disableIrqs();
    DiagnosticEvent& event = sRing[sWriteIndex];
    memset(&event, 0, sizeof(event));
    event.sequence = ++sEventSequence;
    event.type = static_cast<u32>(type);
    event.sourceExecutionPc = sourceExecutionPc;
    event.sourceGuestPc = toGuestAddress(sourceExecutionPc);
    event.instruction = instruction;
    event.state = state | StateArmed;
    event.cpsr = cpsr;
    event.lr = lr;
    event.rawTarget = rawTarget;
    event.normalizedGuestTarget = rawTarget ? toGuestAddress(rawTarget) : 0;
    event.finalExecutionTarget = rawTarget ? toExecutionAddress(event.normalizedGuestTarget) : 0;
    event.sourceRomBlock = getRomBlock(event.sourceGuestPc);
    event.targetRomBlock = rawTarget ? getRomBlock(event.normalizedGuestTarget) : InvalidValue;
    event.sourceCacheBlock = getCacheBlock(sourceExecutionPc);
    event.targetCacheBlock = rawTarget ? getCacheBlock(rawTarget) : InvalidValue;
    event.jitState =
        (isJittableAddress(sourceExecutionPc) &&
            jit_isBlockJitted(reinterpret_cast<void*>(sourceExecutionPc)) ? 1u : 0u) |
        (rawTarget && isJittableAddress(rawTarget) &&
            jit_isBlockJitted(reinterpret_cast<void*>(rawTarget)) ? 2u : 0u);
    event.hicodeBlock = gHicodeState[0];
    event.hicodeMask = gHicodeState[1];
    event.mpuRegion4 = readMpuRegion4();
    event.prefetchAbortCount = sPrefetchAbortCount;
    event.undefinedCount = sUndefinedCount;
    event.hicodeMissCount = sHicodeMissCount;
    event.sdCacheLoadCount = sSdCacheLoadCount;
    event.aux = aux;

    if (pending)
    {
        sPendingEventIndex = sWriteIndex;
        sPendingEventSequence = event.sequence;
    }
    sWriteIndex = (sWriteIndex + 1) & (RingCapacity - 1);
    ++sTotalEvents;
    arm_restoreIrqs(irqState);
}

CFDIAG_EWRAM u32 fnv1a(const void* data, size_t size, u32 hash)
{
    const auto* bytes = static_cast<const u8*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

CFDIAG_EWRAM DiagnosticHeader makeHeader(PersistReason reason)
{
    return
    {
        DiagnosticMagic,
        DiagnosticVersion,
        sizeof(DiagnosticHeader),
        sizeof(DiagnosticEvent),
        RingCapacity,
        sWriteIndex,
        sTotalEvents,
        sGameCode,
        sRomSize,
        sCheckpointSequence,
        static_cast<u32>(sStatus),
        0,
        0,
        static_cast<u32>(reason),
        static_cast<u32>(sLastFileResult),
        sArmSequence
    };
}

CFDIAG_EWRAM bool writeCheckpointFile(
    const char* path, bool includeRing, PersistReason reason)
{
    FIL file {};
    sLastFileResult = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (sLastFileResult != FR_OK)
        return false;

    DiagnosticHeader header = makeHeader(reason);
    if (includeRing)
    {
        u32 checksum = fnv1a(&header, sizeof(header), 2166136261u);
        header.checksum = fnv1a(sRing, sizeof(sRing), checksum);
    }

    UINT written = 0;
    sLastFileResult = f_write(&file, &header, sizeof(header), &written);
    bool success = sLastFileResult == FR_OK && written == sizeof(header);
    if (success && includeRing)
    {
        written = 0;
        sLastFileResult = f_write(&file, sRing, sizeof(sRing), &written);
        success = sLastFileResult == FR_OK && written == sizeof(sRing);
    }
    if (success)
    {
        sLastFileResult = f_sync(&file);
        success = sLastFileResult == FR_OK;
    }
    const FRESULT closeResult = f_close(&file);
    if (success && closeResult != FR_OK)
    {
        sLastFileResult = closeResult;
        success = false;
    }
    return success;
}

CFDIAG_EWRAM void persist(PersistReason reason, bool emergency)
{
    if (!sArmed)
        return;
    ++sCheckpointSequence;
    sStatus = emergency ? DiagnosticStatus::Emergency : DiagnosticStatus::Checkpoint;
    sPersistReason = reason;
    const char* path = sWritePathB ? sPathB : sPathA;
    if (writeCheckpointFile(path, true, reason))
        sWritePathB = !sWritePathB;
}

CFDIAG_EWRAM void copyPath(char* destination, const char* source)
{
    strncpy(destination, source, 511);
    destination[511] = 0;
}
}

extern "C" CFDIAG_EWRAM void cfdiag_initialize(
    const char* pathA, const char* pathB, u32 gameCode, u32 romSize)
{
    memset(sRing, 0, sizeof(sRing));
    copyPath(sPathA, pathA);
    copyPath(sPathB, pathB);
    sWriteIndex = 0;
    sTotalEvents = 0;
    sEventSequence = 0;
    sCheckpointSequence = 0;
    sArmSequence = InvalidValue;
    sGameCode = gameCode;
    sRomSize = romSize;
    sFrameCount = 0;
    sPrefetchAbortCount = 0;
    sUndefinedCount = 0;
    sHicodeMissCount = 0;
    sSdCacheLoadCount = 0;
    sPendingEventIndex = InvalidValue;
    sPendingEventSequence = InvalidValue;
    sLastPrefetchAddress = InvalidValue;
    sRepeatedPrefetchCount = 0;
    sLastHicodeMissAddress = InvalidValue;
    sRepeatedHicodeMissCount = 0;
    sLastKeysDown = 0;
    sArmed = false;
    sForcePersist = false;
    sWritePathB = false;
    sLastFileResult = FR_OK;
    sStatus = DiagnosticStatus::Ready;
    sPersistReason = PersistReason::Boot;
    writeCheckpointFile(sPathA, false, PersistReason::Boot);
    writeCheckpointFile(sPathB, false, PersistReason::Boot);
}

extern "C" CFDIAG_EWRAM void cfdiag_observeKeys(u16 keyInput)
{
    const u16 keysDown = static_cast<u16>(~keyInput) & 0x03FF;
    const u16 newlyPressed = keysDown & ~sLastKeysDown;
    sLastKeysDown = keysDown;

    if (!sArmed && (newlyPressed & KeySelect))
    {
        memset(sRing, 0, sizeof(sRing));
        sWriteIndex = 0;
        sTotalEvents = 0;
        sEventSequence = 0;
        sArmed = true;
        sStatus = DiagnosticStatus::Armed;
        sForcePersist = true;
        pushEvent(EventType::Arm, memu_inst_addr, 0, 0, vm_cpsr, 0, 0, keyInput, false);
        sArmSequence = sEventSequence;
        return;
    }

    if (sArmed && newlyPressed)
    {
        pushEvent(EventType::Input, memu_inst_addr, 0, 0, vm_cpsr, 0, 0, keyInput, false);
        if (newlyPressed & KeyA)
            sForcePersist = true;
    }
}

extern "C" CFDIAG_EWRAM void cfdiag_sampleVBlank()
{
    const u16 keys = *reinterpret_cast<vu16*>(0x04000130);
    cfdiag_observeKeys(keys);
    if (!sArmed)
        return;

    ++sFrameCount;
    if (sForcePersist || (sFrameCount % PersistFrameInterval) == 0)
    {
        pushEvent(
            EventType::VBlank, memu_inst_addr, 0, 0, vm_cpsr, vm_irqSavedLR,
            0, keys, false);
        const PersistReason reason = sForcePersist ? PersistReason::Input : PersistReason::Periodic;
        sForcePersist = false;
        persist(reason, false);
    }
}

extern "C" CFDIAG_EWRAM void cfdiag_recordJitPatchArm(
    const u32* instructionPtr, u32 instruction)
{
    if (!sArmed)
        return;
    const EventType type = classifyArmInstruction(instruction, false);
    if (type == EventType::Undefined)
        return;
    const u32 sourceExecutionPc = reinterpret_cast<u32>(instructionPtr);
    const u32 sourceGuestPc = toGuestAddress(sourceExecutionPc);
    const u32 rawTarget = type == EventType::ArmB || type == EventType::ArmBl
        ? calculateArmBranchTarget(sourceGuestPc, instruction) : 0;
    pushEvent(
        type, sourceExecutionPc, instruction, StateJitPatch, vm_cpsr, 0,
        rawTarget, 0, false);
}

extern "C" CFDIAG_EWRAM void cfdiag_recordJitPatchThumb(
    const u16* instructionPtr, u16 instruction)
{
    if (!sArmed)
        return;
    const EventType type = classifyThumbInstruction(instruction, false);
    if (type == EventType::Undefined)
        return;
    const u32 sourceExecutionPc = reinterpret_cast<u32>(instructionPtr);
    const u32 sourceGuestPc = toGuestAddress(sourceExecutionPc) | 1;
    const u32 rawTarget = calculateThumbBranchTarget(sourceGuestPc, instruction, type);
    pushEvent(
        type, sourceExecutionPc, instruction, StateThumb | StateJitPatch,
        vm_cpsr | 0x20, 0, rawTarget, 0, false);
}

extern "C" CFDIAG_EWRAM void cfdiag_recordArmUndefined(
    u32 sourceExecutionPc, u32 instruction, u32 cpsr, u32 lr)
{
    ++sUndefinedCount;
    const EventType type = classifyArmInstruction(instruction, true);
    pushEvent(
        type, sourceExecutionPc, instruction, StateRuntime, cpsr, lr,
        0, 0, true);
}

extern "C" CFDIAG_EWRAM void cfdiag_recordControlFlowTarget(u32 rawTarget)
{
    if (!sArmed || sPendingEventIndex == InvalidValue)
        return;

    const u32 irqState = arm_disableIrqs();
    DiagnosticEvent& event = sRing[sPendingEventIndex];
    if (event.sequence == sPendingEventSequence)
    {
        event.rawTarget = rawTarget;
        event.normalizedGuestTarget = toGuestAddress(rawTarget);
        event.finalExecutionTarget = toExecutionAddress(event.normalizedGuestTarget);
        event.targetRomBlock = getRomBlock(event.normalizedGuestTarget);
        event.targetCacheBlock = getCacheBlock(rawTarget);
        if (isJittableAddress(rawTarget) &&
            jit_isBlockJitted(reinterpret_cast<void*>(rawTarget)))
            event.jitState |= 2;
    }
    sPendingEventIndex = InvalidValue;
    sPendingEventSequence = InvalidValue;
    arm_restoreIrqs(irqState);
}

extern "C" CFDIAG_EWRAM void cfdiag_recordThumbControlFlow(
    u32 sourceExecutionPc, u32 instruction, u32 rawTarget, u32 cpsr, u32 lr)
{
    const EventType type = classifyThumbInstruction(instruction, true);
    pushEvent(
        type, sourceExecutionPc, instruction, StateThumb | StateRuntime,
        cpsr | 0x20, lr, rawTarget, 0, false);
}

extern "C" CFDIAG_EWRAM void cfdiag_recordPrefetchAbort(
    u32 faultAddress, u32 cpsr, u32 lr)
{
    ++sPrefetchAbortCount;
    if (faultAddress == sLastPrefetchAddress)
        ++sRepeatedPrefetchCount;
    else
    {
        sLastPrefetchAddress = faultAddress;
        sRepeatedPrefetchCount = 1;
    }
    pushEvent(
        EventType::PrefetchAbort, faultAddress, 0, StateRuntime,
        cpsr, lr, faultAddress, sRepeatedPrefetchCount, false);
    sForcePersist = true;
    if (sArmed && sRepeatedPrefetchCount == 4)
        persist(PersistReason::RepeatedPrefetchAbort, true);
}

extern "C" CFDIAG_EWRAM void cfdiag_recordHicodeMiss(u32 faultAddress)
{
    ++sHicodeMissCount;
    if (faultAddress == sLastHicodeMissAddress)
        ++sRepeatedHicodeMissCount;
    else
    {
        sLastHicodeMissAddress = faultAddress;
        sRepeatedHicodeMissCount = 1;
    }
    pushEvent(
        EventType::HicodeMiss, faultAddress, 0, StateRuntime,
        vm_cpsr, 0, faultAddress, sRepeatedHicodeMissCount, false);
    sForcePersist = true;
    if (sArmed && sRepeatedHicodeMissCount == 4)
        persist(PersistReason::RepeatedHicodeMiss, true);
}

extern "C" CFDIAG_EWRAM void cfdiag_recordHicodeMap(u32 gbaAddress, u32 mapMode)
{
    pushEvent(
        EventType::HicodeMap, gbaAddress, 0, StateRuntime,
        vm_cpsr, 0, gbaAddress, mapMode, false);
}

extern "C" CFDIAG_EWRAM void cfdiag_recordSdCache(
    u32 oldRomBlock, u32 newRomBlock, u32 cacheBlock)
{
    ++sSdCacheLoadCount;
    if (oldRomBlock != InvalidValue)
    {
        pushEvent(
            EventType::SdCacheEvict, 0, 0, StateRuntime, vm_cpsr, 0,
            0x08000000 + oldRomBlock * SDC_BLOCK_SIZE, cacheBlock, false);
    }
    pushEvent(
        EventType::SdCacheLoad, 0, 0, StateRuntime, vm_cpsr, 0,
        0x08000000 + newRomBlock * SDC_BLOCK_SIZE, cacheBlock, false);
}

extern "C" CFDIAG_EWRAM void cfdiag_recordNotImplemented()
{
    pushEvent(
        EventType::NotImplemented, memu_inst_addr, 0, StateRuntime,
        vm_cpsr, 0, 0, 0, false);
    persist(PersistReason::NotImplemented, true);
}

#endif
