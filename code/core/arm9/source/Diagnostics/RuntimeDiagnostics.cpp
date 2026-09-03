#include "RuntimeDiagnostics.h"

#if defined(GBAR3_RUNTIME_DIAGNOSTICS) && !defined(GBAR3_DIAG_AUTOCAPTURE)

#include <cstddef>
#include <string.h>
#include "Emulator/IoRegisters.h"
#include "Fat/ff.h"
#include "GbaIoRegOffsets.h"
#include "Peripherals/DmaTransfer.h"
#include "SdCache/SdCache.h"
#include "VirtualMachine/VMDtcm.h"

namespace
{
#define DIAG_EWRAM [[gnu::section(".ewram"), gnu::noinline]]

constexpr u32 DiagnosticMagic = 0x47443347; // "G3DG"
constexpr u32 DiagnosticVersion = 3;
constexpr u32 RingCapacity = 64;
constexpr u16 KeyA = 1 << 0;
constexpr u16 KeySelect = 1 << 2;
constexpr u32 PersistFrameInterval = 60;
constexpr u32 DiagnosticFlags = 0
#ifdef GBAR3_DIAG_DISABLE_BG_VRAM_ABORT
    | (1 << 0)
#endif
#ifdef GBAR3_DIAG_DISABLE_VRAM_WRITE_BUFFER
    | (1 << 1)
#endif
#ifdef GBAR3_DIAG_DISABLE_JIT
    | (1 << 2)
#endif
#ifdef GBAR3_DIAG_FORCE_SAFE_DMA
    | (1 << 3)
#endif
    ;

struct DmaSnapshot
{
    u32 source;
    u32 destination;
    u16 count;
    u16 control;
    u32 currentSource;
    u32 currentDestination;
};

struct DiagnosticRecord
{
    u32 sampleIndex;
    u32 irqReturnAddress;
    u32 emulatedInstructionAddress;
    u32 virtualCpsr;
    u32 irqState;
    u32 hardwareIrqMask;
    u32 forcedIrqMask;
    u32 hicodeBlock;
    u32 hicodeBlockMask;
    u32 sramReadCount;
    u32 sramWriteCount;
    u32 lastSramAddress;
    u32 lastSramValue;
    u32 dmaStartCount;
    u32 lastDmaChannel;
    u32 dmaFlags;
    u32 sdForbiddenRange;
    u16 display[21];
    u16 irq[4];
    u16 keyInput;
    u32 timers[4];
    u32 sound[3];
    u32 dsDisplay[2];
    u32 dsIrq[2];
    DmaSnapshot dma[4];
};

struct DiagnosticHeader
{
    u32 magic;
    u32 version;
    u32 headerSize;
    u32 recordSize;
    u32 capacity;
    u32 writeIndex;
    u32 totalSamples;
    u32 gameCode;
    u32 romSize;
    u32 checkpointSequence;
    u32 flags;
    u32 status;
    u32 fileResult;
    u32 checksum;
    u32 armSample;
    u32 transitionSample;
};

enum class DiagnosticStatus : u32
{
    Ready = 1,
    Armed = 2,
    Checkpoint = 3,
    WriteFailed = 4
};

static_assert(offsetof(DiagnosticSramState, readCount) == 0);
static_assert(offsetof(DiagnosticSramState, lastReadAddress) == 8);
static_assert(offsetof(DiagnosticSramState, writeCount) == 12);
static_assert(offsetof(DiagnosticSramState, lastWriteAddress) == 16);
static_assert(offsetof(DiagnosticSramState, lastWriteValue) == 20);
static_assert(sizeof(DiagnosticHeader) == 64);
static_assert(offsetof(DiagnosticRecord, timers) == 120);
static_assert(offsetof(DiagnosticRecord, dsDisplay) == 148);
static_assert(offsetof(DiagnosticRecord, dma) == 164);
static_assert(sizeof(DiagnosticRecord) == 244);

[[gnu::section(".ewram.bss")]] DiagnosticRecord sRing[RingCapacity];
[[gnu::section(".ewram.bss")]] char sPathA[512];
[[gnu::section(".ewram.bss")]] char sPathB[512];
u32 sWriteIndex;
u32 sTotalSamples;
u32 sGameCode;
u32 sRomSize;
u32 sDmaStartCount;
u32 sLastDmaChannel;
u32 sCheckpointSequence;
u32 sArmSample;
u32 sTransitionSample;
u32 sFramesAfterTransition;
u16 sLastKeysDown;
FRESULT sLastFileResult;
DiagnosticStatus sStatus;
bool sArmed;
bool sTransitionObserved;
bool sWritePathB;

extern "C" u32 vm_irqSavedLR;
extern "C" u32 memu_inst_addr;
extern "C" u32 gHicodeState[2];

DIAG_EWRAM u16 read16(u32 offset)
{
    return *reinterpret_cast<vu16*>(&emu_ioRegisters[offset]);
}

DIAG_EWRAM u32 read32(u32 offset)
{
    return *reinterpret_cast<vu32*>(&emu_ioRegisters[offset]);
}

DIAG_EWRAM u16 readHardware16(u32 address)
{
    return *reinterpret_cast<vu16*>(address);
}

DIAG_EWRAM u32 readHardware32(u32 address)
{
    return *reinterpret_cast<vu32*>(address);
}

DIAG_EWRAM u16 gbaVCountFromDs(u16 dsVCount)
{
    if (dsVCount < 160)
        return dsVCount;
    if (dsVCount < 192)
        return 160;
    const u16 gbaVCount = dsVCount - 32;
    return gbaVCount > 227 ? 227 : gbaVCount;
}

DIAG_EWRAM void copyDisplayState(DiagnosticRecord& record)
{
    constexpr u32 offsets[] =
    {
        GBA_REG_OFFS_DISPCNT, GBA_REG_OFFS_DISPSTAT, GBA_REG_OFFS_VCOUNT,
        GBA_REG_OFFS_BG0CNT, GBA_REG_OFFS_BG1CNT, GBA_REG_OFFS_BG2CNT,
        GBA_REG_OFFS_BG3CNT, GBA_REG_OFFS_WININ, GBA_REG_OFFS_WINOUT,
        GBA_REG_OFFS_MOSAIC, GBA_REG_OFFS_BLDCNT, GBA_REG_OFFS_BLDALPHA,
        GBA_REG_OFFS_BLDY, GBA_REG_OFFS_BG2PA, GBA_REG_OFFS_BG2PB,
        GBA_REG_OFFS_BG2PC, GBA_REG_OFFS_BG2PD, GBA_REG_OFFS_BG2X_L,
        GBA_REG_OFFS_BG2X_H, GBA_REG_OFFS_BG2Y_L, GBA_REG_OFFS_BG2Y_H
    };
    for (u32 i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
        record.display[i] = read16(offsets[i]);

    const u16 dsDispStat = readHardware16(0x04000004);
    const u16 dsVCount = readHardware16(0x04000006);
    record.display[1] = (record.display[1] & ~0xC7u) | (dsDispStat & 7);
    record.display[2] = gbaVCountFromDs(dsVCount);
    record.dsDisplay[0] = readHardware32(0x04000000);
    record.dsDisplay[1] = static_cast<u32>(dsDispStat) | (static_cast<u32>(dsVCount) << 16);

    record.irq[0] = read16(GBA_REG_OFFS_IE);
    record.irq[1] = vm_emulatedIfImeIe & 0x3FFF;
    record.irq[2] = read16(GBA_REG_OFFS_WAITCNT);
    record.irq[3] = (vm_emulatedIfImeIe >> 15) & 1;
    record.dsIrq[0] = readHardware16(0x04000210);
    record.dsIrq[1] = readHardware16(0x04000214);
}

DIAG_EWRAM void copyTimerState(DiagnosticRecord& record)
{
    for (u32 timer = 0; timer < 4; ++timer)
    {
        const u32 offset = GBA_REG_OFFS_TM0CNT + timer * 4;
        const u16 liveCounter = readHardware16(0x04000100 + timer * 4);
        const u16 guestControl = read16(offset + 2);
        record.timers[timer] = liveCounter | (static_cast<u32>(guestControl) << 16);
    }
}

DIAG_EWRAM void copyDmaState(DiagnosticRecord& record)
{
    for (u32 channel = 0; channel < 4; ++channel)
    {
        const auto* io = reinterpret_cast<const GbaDmaChannel*>(
            &emu_ioRegisters[GBA_REG_OFFS_DMA0SAD + channel * sizeof(GbaDmaChannel)]);
        record.dma[channel] =
        {
            io->src,
            io->dst,
            io->count,
            io->control,
            dma_state.channels[channel].curSrc,
            dma_state.channels[channel].curDst
        };
    }
}

DIAG_EWRAM u32 fnv1a(const void* data, size_t size, u32 hash)
{
    const auto* bytes = static_cast<const u8*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

DIAG_EWRAM DiagnosticHeader makeHeader()
{
    return
    {
        DiagnosticMagic,
        DiagnosticVersion,
        sizeof(DiagnosticHeader),
        sizeof(DiagnosticRecord),
        RingCapacity,
        sWriteIndex,
        sTotalSamples,
        sGameCode,
        sRomSize,
        sCheckpointSequence,
        DiagnosticFlags,
        static_cast<u32>(sStatus),
        static_cast<u32>(sLastFileResult),
        0,
        sArmSample,
        sTransitionSample
    };
}

DIAG_EWRAM bool writeDiagnosticFile(const char* path, bool includeRing)
{
    FIL file {};
    sLastFileResult = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (sLastFileResult != FR_OK)
        return false;

    DiagnosticHeader header = makeHeader();
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

DIAG_EWRAM void persist()
{
    ++sCheckpointSequence;
    sStatus = DiagnosticStatus::Checkpoint;
    const char* path = sWritePathB ? sPathB : sPathA;
    if (writeDiagnosticFile(path, true))
    {
        sWritePathB = !sWritePathB;
        return;
    }
    sStatus = DiagnosticStatus::WriteFailed;
}

DIAG_EWRAM void copyPath(char* destination, const char* source)
{
    strncpy(destination, source, 511);
    destination[511] = 0;
}
}

extern "C"
{
volatile DiagnosticSramState gDiagSramState = {};
}

extern "C" DIAG_EWRAM void diag_initialize(
    const char* pathA, const char* pathB, u32 gameCode, u32 romSize)
{
    memset(sRing, 0, sizeof(sRing));
    copyPath(sPathA, pathA);
    copyPath(sPathB, pathB);
    sWriteIndex = 0;
    sTotalSamples = 0;
    sGameCode = gameCode;
    sRomSize = romSize;
    sDmaStartCount = 0;
    sLastDmaChannel = 0xFFFFFFFF;
    sCheckpointSequence = 0;
    sArmSample = 0xFFFFFFFF;
    sTransitionSample = 0xFFFFFFFF;
    sFramesAfterTransition = 0;
    sLastKeysDown = 0;
    sLastFileResult = FR_OK;
    sStatus = DiagnosticStatus::Ready;
    sArmed = false;
    sTransitionObserved = false;
    sWritePathB = false;
    writeDiagnosticFile(sPathA, false);
    writeDiagnosticFile(sPathB, false);
}

extern "C" DIAG_EWRAM void diag_recordDmaStart(u32 channel, const GbaDmaChannel*, u32)
{
    ++sDmaStartCount;
    sLastDmaChannel = channel;
}

extern "C" DIAG_EWRAM void diag_sampleVBlank()
{
    const u16 keys = readHardware16(0x04000130);
    const u16 keysDown = static_cast<u16>(~keys) & 0x03FF;
    const u16 newlyPressed = keysDown & ~sLastKeysDown;
    sLastKeysDown = keysDown;

    if (!sArmed && (newlyPressed & KeySelect))
    {
        memset(sRing, 0, sizeof(sRing));
        sWriteIndex = 0;
        sTotalSamples = 0;
        sArmed = true;
        sTransitionObserved = false;
        sStatus = DiagnosticStatus::Armed;
        sArmSample = 0;
    }
    if (!sArmed)
        return;

    if (!sTransitionObserved && (newlyPressed & KeyA))
    {
        sTransitionObserved = true;
        sTransitionSample = sTotalSamples;
        sFramesAfterTransition = 0;
    }

    DiagnosticRecord& record = sRing[sWriteIndex];
    memset(&record, 0, sizeof(record));
    record.sampleIndex = sTotalSamples;
    record.irqReturnAddress = vm_irqSavedLR - 4;
    record.emulatedInstructionAddress = memu_inst_addr;
    record.virtualCpsr = vm_cpsr;
    record.irqState = vm_emulatedIfImeIe;
    record.hardwareIrqMask = vm_hwIrqMask;
    record.forcedIrqMask = vm_forcedIrqMask;
    record.hicodeBlock = gHicodeState[0];
    record.hicodeBlockMask = gHicodeState[1];
    record.sramReadCount = gDiagSramState.readCount;
    record.sramWriteCount = gDiagSramState.writeCount;
    record.lastSramAddress = gDiagSramState.lastWriteAddress != 0
        ? gDiagSramState.lastWriteAddress : gDiagSramState.lastReadAddress;
    record.lastSramValue = gDiagSramState.lastWriteValue;
    record.dmaStartCount = sDmaStartCount;
    record.lastDmaChannel = sLastDmaChannel;
    record.dmaFlags = dma_state.dmaFlags;
    record.sdForbiddenRange = gSdCacheIrqForbiddenRomBlockReplacementRange;
    copyDisplayState(record);
    record.keyInput = keys;
    copyTimerState(record);
    for (u32 i = 0; i < 3; ++i)
        record.sound[i] = read32(GBA_REG_OFFS_SOUNDCNT_L + i * 4);
    copyDmaState(record);

    sWriteIndex = (sWriteIndex + 1) & (RingCapacity - 1);
    ++sTotalSamples;

    if (sTransitionObserved && ++sFramesAfterTransition == PersistFrameInterval)
    {
        sFramesAfterTransition = 0;
        persist();
    }
}

#endif
