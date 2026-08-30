#include "RuntimeDiagnostics.h"

#ifdef GBAR3_RUNTIME_DIAGNOSTICS

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
constexpr u32 DiagnosticVersion = 1;
constexpr u32 RingCapacity = 256;
constexpr u16 DumpKeyMask = (1 << 2) | (1 << 8) | (1 << 9); // Select + R + L

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
    u32 dataAbortCount;
    u32 prefetchAbortCount;
    u32 sramReadCount;
    u32 sramWriteCount;
    u32 lastSramAddress;
    u32 lastSramValue;
    u32 dmaStartCount;
    u32 lastDmaChannel;
    u32 dmaFlags;
    u32 sdForbiddenRange;
    u16 display[15];
    u16 irq[4];
    u16 alignmentPadding;
    u32 timers[4];
    u32 sound[3];
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
    u32 dumpSample;
    u32 flags;
    u32 reserved[5];
};

static_assert(offsetof(DiagnosticSramState, readCount) == 0);
static_assert(offsetof(DiagnosticSramState, lastReadAddress) == 8);
static_assert(offsetof(DiagnosticSramState, writeCount) == 12);
static_assert(offsetof(DiagnosticSramState, lastWriteAddress) == 16);
static_assert(offsetof(DiagnosticSramState, lastWriteValue) == 20);
static_assert(sizeof(DiagnosticHeader) == 64);
static_assert(offsetof(DiagnosticRecord, timers) == 108);
static_assert(offsetof(DiagnosticRecord, dma) == 136);
static_assert(sizeof(DiagnosticRecord) == 216);

[[gnu::section(".ewram.bss")]] DiagnosticRecord sRing[RingCapacity];
[[gnu::section(".ewram.bss")]] char sFilePath[256];
u32 sWriteIndex;
u32 sTotalSamples;
u32 sGameCode;
u32 sRomSize;
u32 sDmaStartCount;
u32 sLastDmaChannel = 0xFFFFFFFF;
bool sDumped;
bool sDumpKeysWereDown;

extern "C" u32 vm_irqSavedLR;
extern "C" u32 memu_inst_addr;

DIAG_EWRAM u16 read16(u32 offset)
{
    return *reinterpret_cast<vu16*>(&emu_ioRegisters[offset]);
}

DIAG_EWRAM u32 read32(u32 offset)
{
    return *reinterpret_cast<vu32*>(&emu_ioRegisters[offset]);
}

DIAG_EWRAM void copyDisplayState(DiagnosticRecord& record)
{
    constexpr u32 offsets[] =
    {
        GBA_REG_OFFS_DISPCNT, GBA_REG_OFFS_DISPSTAT, GBA_REG_OFFS_VCOUNT,
        GBA_REG_OFFS_BG0CNT, GBA_REG_OFFS_BG1CNT, GBA_REG_OFFS_BG2CNT,
        GBA_REG_OFFS_BG3CNT, GBA_REG_OFFS_WININ, GBA_REG_OFFS_WINOUT,
        GBA_REG_OFFS_MOSAIC, GBA_REG_OFFS_BLDCNT, GBA_REG_OFFS_BLDALPHA,
        GBA_REG_OFFS_BLDY, GBA_REG_OFFS_BG2PA, GBA_REG_OFFS_BG2PD
    };
    for (u32 i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
        record.display[i] = read16(offsets[i]);

    record.irq[0] = read16(GBA_REG_OFFS_IE);
    record.irq[1] = read16(GBA_REG_OFFS_IF);
    record.irq[2] = read16(GBA_REG_OFFS_WAITCNT);
    record.irq[3] = read16(GBA_REG_OFFS_IME);
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

DIAG_EWRAM void dumpRing()
{
    FIL file {};
    if (f_open(&file, sFilePath, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        return;

    DiagnosticHeader header
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
        sTotalSamples,
        0,
        { 0, 0, 0, 0, 0 }
    };

    UINT written = 0;
    const bool headerWritten = f_write(&file, &header, sizeof(header), &written) == FR_OK
        && written == sizeof(header);
    if (headerWritten)
    {
        written = 0;
        f_write(&file, sRing, sizeof(sRing), &written);
        f_sync(&file);
    }
    f_close(&file);
}
}

extern "C"
{
volatile u32 gDiagDataAbortCount = 0;
volatile u32 gDiagPrefetchAbortCount = 0;
volatile DiagnosticSramState gDiagSramState = {};
}

extern "C" DIAG_EWRAM void diag_initialize(const char* filePath, u32 gameCode, u32 romSize)
{
    memset(sRing, 0, sizeof(sRing));
    strncpy(sFilePath, filePath, sizeof(sFilePath) - 1);
    sFilePath[sizeof(sFilePath) - 1] = 0;
    sWriteIndex = 0;
    sTotalSamples = 0;
    sGameCode = gameCode;
    sRomSize = romSize;
    sDmaStartCount = 0;
    sLastDmaChannel = 0xFFFFFFFF;
    sDumped = false;
    sDumpKeysWereDown = false;
}

extern "C" DIAG_EWRAM void diag_recordDmaStart(u32 channel, const GbaDmaChannel*, u32)
{
    ++sDmaStartCount;
    sLastDmaChannel = channel;
}

extern "C" DIAG_EWRAM void diag_sampleVBlank()
{
    DiagnosticRecord& record = sRing[sWriteIndex];
    memset(&record, 0, sizeof(record));
    record.sampleIndex = sTotalSamples;
    record.irqReturnAddress = vm_irqSavedLR - 4;
    record.emulatedInstructionAddress = memu_inst_addr;
    record.virtualCpsr = vm_cpsr;
    record.irqState = vm_emulatedIfImeIe;
    record.hardwareIrqMask = vm_hwIrqMask;
    record.forcedIrqMask = vm_forcedIrqMask;
    record.dataAbortCount = gDiagDataAbortCount;
    record.prefetchAbortCount = gDiagPrefetchAbortCount;
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
    for (u32 timer = 0; timer < 4; ++timer)
        record.timers[timer] = read32(GBA_REG_OFFS_TM0CNT + timer * 4);
    for (u32 i = 0; i < 3; ++i)
        record.sound[i] = read32(GBA_REG_OFFS_SOUNDCNT_L + i * 4);
    copyDmaState(record);

    sWriteIndex = (sWriteIndex + 1) & (RingCapacity - 1);
    ++sTotalSamples;

    const u16 keys = *reinterpret_cast<vu16*>(0x04000130);
    const bool dumpKeysDown = (keys & DumpKeyMask) == 0;
    if (dumpKeysDown && !sDumpKeysWereDown && !sDumped)
    {
        dumpRing();
        sDumped = true;
    }
    sDumpKeysWereDown = dumpKeysDown;
}

#endif
