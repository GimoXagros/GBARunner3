#include "common.h"
#include "RomGpioRtc.h"
#include "RomGpio.h"
#include "RtcFault.h"

RomGpio gRomGpio;
[[gnu::section(".ewram.bss")]] static RomGpioRtc sRomGpioRtc;
extern "C" bool rtc_runOnWorkStack();

void RomGpio::Initialize(rio_registers_t* romGpioRegisters)
{
    _registers = romGpioRegisters;
    _registersRomData = *romGpioRegisters;
    Reset();
}

[[gnu::section(".ewram")]] RtcPersistence::LoadStatus RomGpio::LoadRtcState(
    const char* legacyStatePath,
    const char* legacyTempPath,
    const char* legacyBackupPath,
    const char* modernStatePath,
    const char* modernTempPath,
    const char* modernBackupPath,
    const RtcPersistence::Identity& identity)
{
    return sRomGpioRtc.Initialize(
        legacyStatePath, legacyTempPath, legacyBackupPath,
        modernStatePath, modernTempPath, modernBackupPath, identity);
}

[[gnu::section(".ewram")]] bool RomGpio::FlushRtcStateIfDirty()
{
    return rtc_runOnWorkStack();
}

// The VBlank caller has a 288-byte DTCM IRQ stack. RTC FatFs preflight needs
// a separate bounded stack; the assembly entry owns switching and reentrancy.
extern "C" [[gnu::section(".ewram")]] bool rtc_flushOnWorkStackBody()
{
    return sRomGpioRtc.FlushStateIfDirty();
}

extern "C" [[gnu::noreturn, gnu::section(".ewram")]] void rtc_workStackFault()
{
    rtc_persistenceFault();
}

void RomGpio::Reset()
{
    _inputData = 0;
    _outputData = 0;
    _direction = 0;
    _control = RIO_CONTROL_READ_DISABLE;
    UpdateRomRegisters();
}

void RomGpio::UpdateRomRegisters()
{
    if (_control == RIO_CONTROL_READ_DISABLE)
    {
        // When reading of the registers is disabled, the original rom data is read.
        *_registers = _registersRomData;
    }
    else
    {
        _registers->data = GetGpioState();
        _registers->direction = _direction;
        _registers->control = RIO_CONTROL_READ_ENABLE;
    }
}

static void updateRomGpioPeripherals()
{
    sRomGpioRtc.Update(gRomGpio);
}

extern "C" void rio_write(u32 offset, u16 value)
{
    switch (offset)
    {
        case offsetof(rio_registers_t, data):
        {
            gRomGpio.WriteDataRegister(value);
            updateRomGpioPeripherals();
            break;
        }
        case offsetof(rio_registers_t, direction):
        {
            gRomGpio.WriteDirectionRegister(value);
            updateRomGpioPeripherals();
            break;
        }
        case offsetof(rio_registers_t, control):
        {
            gRomGpio.WriteControlRegister(value);
            break;
        }
    }
}
