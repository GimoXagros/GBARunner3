#include "RuntimeDiagnostics.h"
#ifdef GBAR3_DIAG_AUTOCAPTURE
#include "AutoCaptureDiagnostics.h"
#include "AutoCaptureFormat.h"
#include <cstddef>
#include <string.h>
#include "Emulator/IoRegisters.h"
#include "Fat/ff.h"
#include "GbaIoRegOffsets.h"
#include "Peripherals/DmaTransfer.h"
#include "VirtualMachine/VMDtcm.h"
#include "SdCache/SdCache.h"
#ifndef GBAR3_BUILD_ID
#error M diagnostics require the exact compile-time build ID
#endif
#define DIAG_EWRAM [[gnu::section(".ewram"), gnu::noinline]]
extern "C" {
extern u32 vm_irqSavedLR, memu_inst_addr, gHicodeState[2], vm_spsr_irq, vm_regs_irq[2], vm_nestedIrqLevel;
extern u32 diag_stack[], diag_stackEnd[], diag_eventStack[], diag_eventStackEnd[];
volatile DiagnosticSramState gDiagSramState = {};
volatile u32 gDiagTraceKinds = 0;
}
namespace {
constexpr u32 Unknown = 0xFFFFFFFF;
constexpr u32 Canary = 0xA93D57AC;
constexpr u32 Fill = 0xD1A650AC;
constexpr u32 Ready = 1, FirstVBlank = 2, Sampling = 4, Transition = 8, Written = 16, WriteFailed = 32;
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
    u32 extra[M_EXTRA_COUNT];
};


static_assert(sizeof(DiagnosticRecord) == M_RECORD_SIZE);
[[gnu::section(".ewram.bss")]] DiagnosticRecord sRing[M_RUNTIME_CAPACITY];
[[gnu::section(".ewram.bss")]] DiagnosticRecord sPhases[M_PHASE_CAPACITY];
[[gnu::section(".ewram.bss")]] MEvent sEvents[M_EVENT_CAPACITY];
[[gnu::section(".ewram.bss")]] MHeader h;
[[gnu::section(".ewram.bss")]] char sPath[2][512];
u32 sPathIndex, sDmaStarts, sLastDma, sPreviousDisplay, sSdOld, sSdNew, sSdCache, sSdLoads;
u16 sPreviousKeys;
bool sPhaseFrozen;
DIAG_EWRAM u32 mpu4() { u32 v; asm volatile("mrc p15, 0, %0, c6, c4, 0" : "=r"(v)); return v; }
DIAG_EWRAM u32 lockdown() { u32 v; asm volatile("mrc p15, 0, %0, c9, c0, 1" : "=r"(v)); return v; }
DIAG_EWRAM u32 control() { u32 v; asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(v)); return v; }
DIAG_EWRAM u32 nativeSpsr() { u32 v; asm volatile("mrs %0, spsr" : "=r"(v)); return v; }
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
    record.dsIrq[0] = readHardware32(0x04000210);
    record.dsIrq[1] = readHardware32(0x04000214);
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


DIAG_EWRAM u32 instruction(u32 pc, u32& valid)
{
    // A single local code word only. Never read high-ROM through the data bus.
    valid = ((pc >= 0x02200000 && pc < 0x02400000) ||
             (pc >= 0x03000000 && pc < 0x03008000) ||
             (pc >= 0x06898000 && pc < 0x0689C000));
    return valid ? *reinterpret_cast<vu32*>(pc & ~3u) : 0;
}
DIAG_EWRAM void checkStack()
{
    h.stack_flags = (diag_stack[0] == Canary ? 0 : 1) |
                    (diag_eventStack[0] == Canary ? 0 : 2);
    const u32* p = diag_stack + 1;
    while (p < diag_stackEnd && *p == Fill) ++p;
    const u32 used = reinterpret_cast<const char*>(diag_stackEnd) - reinterpret_cast<const char*>(p);
    if (used > h.stack_used) h.stack_used = used;
}
DIAG_EWRAM FRESULT writeFile(bool full)
{
    FIL file {};
    FRESULT result = f_open(&file, sPath[sPathIndex], FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) return result;
    MHeader copy = h;
    // READY/stage-only files explicitly have no payload, while retaining progress counters.
    if (!full) copy.flags |= 1;
    copy.checksum = 0;
    u32 sum = fnv1a(&copy, sizeof(copy), 2166136261u);
    if (full) {
        sum = fnv1a(sRing, sizeof(sRing), sum);
        sum = fnv1a(sPhases, sizeof(sPhases), sum);
        sum = fnv1a(sEvents, sizeof(sEvents), sum);
    }
    copy.checksum = sum;
    UINT count = 0;
    auto write = [&](const void* data, UINT size) {
        if (result != FR_OK) return;
        result = f_write(&file, data, size, &count);
        if (result == FR_OK && count != size) result = FR_DISK_ERR;
    };
    write(&copy, sizeof(copy));
    if (full) { write(sRing, sizeof(sRing)); write(sPhases, sizeof(sPhases)); write(sEvents, sizeof(sEvents)); }
    if (result == FR_OK) result = f_sync(&file);
    const FRESULT closeResult = f_close(&file);
    return result == FR_OK ? closeResult : result;
}
DIAG_EWRAM void persist(bool full)
{
    checkStack();
    ++h.sequence;
    h.last_attempt_sample = h.total_samples;
    h.status = full ? 3 : 2;
    h.write_start_vcount = readHardware16(0x04000006);
    const FRESULT result = writeFile(full);
    h.write_end_vcount = readHardware16(0x04000006);
    h.file_result = result;
    if (result == FR_OK) {
        if (full) { h.stages |= Written; h.last_success_sample = h.total_samples; }
        sPathIndex ^= 1;
    } else {
        ++h.fs_failures;
        h.status = 4; h.stages |= WriteFailed;
        // Best effort: reuse the FAILED slot; never sacrifice the other valid checkpoint.
        // If the medium cannot write even this header, only prior durable stages survive.
        writeFile(false);
    }
}
}

extern "C" DIAG_EWRAM void diag_initialize(const char* a, const char* b, u32 code, u32 size)
{
    strncpy(sPath[0], a, 511); strncpy(sPath[1], b, 511);
    h.magic = 0x47443347; h.version = 4;
    h.header_size = sizeof(MHeader); h.record_size = sizeof(DiagnosticRecord);
    h.capacity = M_RUNTIME_CAPACITY; h.phase_capacity = M_PHASE_CAPACITY;
    h.event_capacity = M_EVENT_CAPACITY; h.event_record_size = sizeof(MEvent);
    h.complete_size = M_COMPLETE_SIZE; h.game_code = code; h.rom_size = size;
    h.first_vblank_sample = h.transition_sample = h.first_a_sample = h.last_success_sample = h.first_low_sample = Unknown;
    h.first_low_target = h.first_low_source = Unknown;
    h.status = 1; h.stages = Ready;
    h.stack_size = M_STACK_SIZE; h.event_stack_size = M_EVENT_STACK_SIZE;
    static_assert(sizeof(GBAR3_BUILD_ID) == 41, "build ID must be full commit SHA");
    memcpy(&h.build_id_0, GBAR3_BUILD_ID, 40);
    for (u32* p = diag_stack; p < diag_stackEnd; ++p) *p = Fill;
    for (u32* p = diag_eventStack; p < diag_eventStackEnd; ++p) *p = Fill;
    diag_stack[0] = diag_eventStack[0] = Canary;
    h.file_result = writeFile(false); sPathIndex = 1;
    h.file_result = writeFile(false); sPathIndex = 0;
}
extern "C" DIAG_EWRAM void diag_setEnvironment(u32 hash, u32 saveSize, u32 device, u32 dsi, u32 clock)
{
    h.rom_header_hash = hash; h.save_size = saveSize; h.mount_device = device;
    h.dsi_mode = dsi; h.clock_control = clock; h.env_valid = 0x1F;
}
extern "C" DIAG_EWRAM void diag_recordConfig(const char* path, bool loaded)
{
    if (strcmp(path, "/_gba/gbarunner3.json") == 0) h.app_config_loaded = loaded ? 1 : 2;
    else h.title_config_loaded = loaded ? 1 : 2;
    h.config_path_hash = fnv1a(path, strlen(path), h.config_path_hash ? h.config_path_hash : 2166136261u);
    if (!loaded) return;
    FIL file {}; if (f_open(&file, path, FA_READ) != FR_OK) return;
    u8 buffer[128]; UINT n;
    u32 hash = h.config_data_hash ? h.config_data_hash : 2166136261u;
    while (f_read(&file, buffer, sizeof(buffer), &n) == FR_OK && n) hash = fnv1a(buffer, n, hash);
    f_close(&file); h.config_data_hash = hash;
}
extern "C" DIAG_EWRAM void diag_recordSdLoad(u32 oldBlock, u32 newBlock, u32 cacheBlock)
{
    sSdOld = oldBlock; sSdNew = newBlock; sSdCache = cacheBlock; ++sSdLoads;
}
extern "C" DIAG_EWRAM void diag_recordDmaStart(u32 channel, const GbaDmaChannel*, u32)
{
    ++sDmaStarts; sLastDma = channel;
}
extern "C" DIAG_EWRAM void diag_recordLowTarget(u32 kind, u32 source, u32 target, u32 spsr)
{
    // One event for each path; filesystem work occurs only in VBlank.
    if (gDiagTraceKinds & kind || h.event_count == M_EVENT_CAPACITY) return;
    gDiagTraceKinds |= kind;
    MEvent& e = sEvents[h.event_count];
    e.sample = h.total_samples; e.kind = kind; e.source = source; e.target = target;
    e.native_spsr = spsr; e.virtual_cpsr = vm_cpsr;
    e.irq_spsr = vm_spsr_irq; e.irq_lr = vm_regs_irq[1]; e.irq_sp = vm_regs_irq[0];
    e.marker = memu_inst_addr; e.hicode_block = gHicodeState[0];
    e.mpu_region4 = mpu4(); e.icache_lockdown = lockdown();
    e.instruction = instruction(source, e.instruction_valid); e.sequence = ++h.low_event_count;
    if (h.event_count++ == 0) {
        h.first_low_sample = h.total_samples; h.first_low_target = target;
        h.first_low_source = source; h.first_low_spsr = spsr; h.first_low_kind = kind;
    }
}
extern "C" DIAG_EWRAM void diag_sampleVBlank()
{
    const bool first = h.first_vblank_sample == Unknown;
    if (first) { h.first_vblank_sample = h.total_samples; h.stages |= FirstVBlank | Sampling; }
    const u16 keys = static_cast<u16>(~readHardware16(0x04000130)) & 0x3FF;
    const u16 edges = keys & ~sPreviousKeys; sPreviousKeys = keys;
    if (edges & 1) {
        if (h.first_a_sample == Unknown) h.first_a_sample = h.total_samples;
        h.transition_sample = h.total_samples; ++h.a_count; h.stages |= Transition;
    }
    DiagnosticRecord& r = sRing[h.write_index];
    memset(&r, 0, sizeof(r));
    r.sampleIndex = h.total_samples; r.irqReturnAddress = vm_irqSavedLR - 4;
    r.emulatedInstructionAddress = memu_inst_addr; r.virtualCpsr = vm_cpsr;
    r.irqState = vm_emulatedIfImeIe; r.hardwareIrqMask = vm_hwIrqMask; r.forcedIrqMask = vm_forcedIrqMask;
    r.hicodeBlock = gHicodeState[0]; r.hicodeBlockMask = gHicodeState[1];
    r.sramReadCount = gDiagSramState.readCount; r.sramWriteCount = gDiagSramState.writeCount;
    r.lastSramAddress = gDiagSramState.lastWriteAddress ? gDiagSramState.lastWriteAddress : gDiagSramState.lastReadAddress;
    r.dmaStartCount = sDmaStarts; r.lastDmaChannel = sLastDma; r.dmaFlags = dma_state.dmaFlags;
    r.sdForbiddenRange = gSdCacheIrqForbiddenRomBlockReplacementRange;
    copyDisplayState(r); copyTimerState(r); copyDmaState(r);
    r.keyInput = readHardware16(0x04000130);
    for (u32 i = 0; i < 3; ++i) r.sound[i] = read32(GBA_REG_OFFS_SOUNDCNT_L + i * 4);
    u32* x = r.extra;
    x[X_NATIVE_SPSR] = nativeSpsr(); x[X_DS_IME] = readHardware32(0x04000208);
    x[X_MPU_REGION4] = mpu4(); x[X_ICACHE_LOCKDOWN] = lockdown(); x[X_CP15_CONTROL] = control();
    x[X_VM_SPSR_IRQ] = vm_spsr_irq; x[X_VM_LR_IRQ] = vm_regs_irq[1]; x[X_VM_SP_IRQ] = vm_regs_irq[0];
    x[X_NESTED_IRQ_LEVEL] = vm_nestedIrqLevel;
    x[X_DS_MASTER_BRIGHTNESS] = readHardware16(0x0400006C); x[X_DS_CAPTURE_CONTROL] = readHardware32(0x04000064);
    x[X_DS_VRAM_ABCD] = readHardware32(0x04000240); x[X_DS_VRAM_EFG] = readHardware32(0x04000244);
    x[X_STAGES] = h.stages; x[X_FS_RESULT] = h.file_result; x[X_LAST_SUCCESS_SAMPLE] = h.last_success_sample;
    x[X_TRANSITION_SAMPLE] = h.transition_sample;
    x[X_SD_OLD_BLOCK] = sSdOld; x[X_SD_NEW_BLOCK] = sSdNew; x[X_SD_CACHE_BLOCK] = sSdCache; x[X_SD_LOAD_COUNT] = sSdLoads;
    x[X_WRITE_START_VCOUNT] = h.write_start_vcount; x[X_WRITE_END_VCOUNT] = h.write_end_vcount;
    x[X_STACK_FLAGS] = h.stack_flags; x[X_STACK_USED] = h.stack_used;
    x[X_NATIVE_IRQ_LR] = vm_irqSavedLR; x[X_FIRST_LOW_TARGET] = h.first_low_target; x[X_EVENT_COUNT] = h.event_count;
    x[X_KEY_EDGES] = edges;
    x[X_SOURCE_INSTRUCTION] = instruction(r.irqReturnAddress, x[X_SOURCE_INSTRUCTION_VALID]);
    u32 reason = first ? 1 : 0;
    if (edges & 1) reason |= 2;
    if (r.dsDisplay[0] != sPreviousDisplay) { reason |= 4; sPreviousDisplay = r.dsDisplay[0]; }
    if (h.event_count && !sPhaseFrozen) reason |= 8;
    x[X_ANCHOR_REASON] = reason;
    if (reason && !sPhaseFrozen) {
        sPhases[h.phase_write_index] = r;
        h.phase_write_index = (h.phase_write_index + 1) % M_PHASE_CAPACITY; ++h.phase_total;
        if (reason & 8) sPhaseFrozen = true;
    }
    h.write_index = (h.write_index + 1) % M_RUNTIME_CAPACITY; ++h.total_samples;
    // Independent of all input. First callback leaves a small durable stage;
    // second callback writes the first complete checkpoint, then at 60-frame cadence.
    if (first) persist(false);
    else if (h.total_samples == 2 || h.total_samples % M_PERSIST_INTERVAL == 0) persist(true);
}
#endif
