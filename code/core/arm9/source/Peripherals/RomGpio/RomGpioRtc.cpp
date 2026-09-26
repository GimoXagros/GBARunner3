#include "common.h"
#include <libtwl/mem/memSwap.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include <libtwl/ipc/ipcFifo.h>
#include <string.h>
#include "cp15.h"
#include "Fat/ff.h"
#include "IpcChannels.h"
#include "Save/Save.h"
#include "RtcFault.h"
#include "RomGpio.h"
#include "RomGpioRtc.h"

// RTC GPIO transactions are infrequent. Keep their implementation in EWRAM so
// the current develop branch stays within its 128 KiB VRAM-A code budget.
#define RTC_EWRAM [[gnu::section(".ewram")]]

#define ROM_GPIO_PIN_SCK            0
#define ROM_GPIO_PIN_SIO            1
#define ROM_GPIO_PIN_CS             2

#define RIO_RTC_COMMAND_RESET       0
#define RIO_RTC_COMMAND_STATUS      1
#define RIO_RTC_COMMAND_DATE_TIME   2
#define RIO_RTC_COMMAND_TIME        3
#define RIO_RTC_COMMAND_ALARM1      4
#define RIO_RTC_COMMAND_ALARM2      5
#define RIO_RTC_COMMAND_TEST_START  6
#define RIO_RTC_COMMAND_TEST_END    7

#define RIO_RTC_STATUS_INTFE        0x02
#define RIO_RTC_STATUS_INTME        0x08
#define RIO_RTC_STATUS_INTAE        0x20
#define RIO_RTC_STATUS_24H          0x40
#define RIO_RTC_STATUS_POWER        0x80

#define RIO_RTC_STATUS_WRITE_MASK   0b01101010

[[gnu::section(".ewram.bss")]]
RomGpioRtc::rio_rtc_datetime_t RomGpioRtc::sDSRtcDateTime alignas(32);

// FIL owns a sector-sized window and cannot live on the small IRQ stack used
// by the deferred VBlank writer.
[[gnu::section(".ewram.bss")]]
static FIL sRtcStateFile alignas(32);

// The FIL itself is static. Track its lifetime globally as well, so creating
// another RTC object in the same process cannot clear a handle after close fails.
[[gnu::section(".ewram.bss")]]
static bool sRtcFileHandleLive;

[[gnu::section(".ewram.bss")]]
static RtcPersistence::StateFile sLegacyRecords[3] alignas(32);

[[gnu::section(".ewram.bss")]]
static RtcPersistence::StateFileV2 sModernRecords[3] alignas(32);

[[gnu::section(".ewram.bss")]]
static RtcPersistence::FileStatus sLegacyStatus[3];

[[gnu::section(".ewram.bss")]]
static RtcPersistence::FileStatus sModernStatus[3];

[[gnu::section(".ewram.bss")]]
static RtcPersistence::StateFileV2 sRtcWriteBuffer alignas(32);

[[gnu::section(".ewram.bss")]]
volatile u8 gRomGpioRtcStateDirty;

RTC_EWRAM RtcPersistence::LoadStatus RomGpioRtc::Initialize(
    const char* legacyStatePath,
    const char* legacyTempPath,
    const char* legacyBackupPath,
    const char* modernStatePath,
    const char* modernTempPath,
    const char* modernBackupPath,
    const RtcPersistence::Identity& identity)
{
    // A failed close may still own the static FIL. Never reinitialize it or
    // retry a failed journal transaction in this process.
    if (sRtcFileHandleLive) return RtcPersistence::LoadStatus::IoError;
    if (_writeError) return RtcPersistence::LoadStatus::WriteError;
    _legacyPaths[0] = legacyStatePath;
    _legacyPaths[1] = legacyTempPath;
    _legacyPaths[2] = legacyBackupPath;
    _modernPaths[0] = modernStatePath;
    _modernPaths[1] = modernTempPath;
    _modernPaths[2] = modernBackupPath;
    _identity = identity;
    _currentRecord = { };
    _hasCurrent = false;
    _writeError = false;
    _stateDirty = false;
    gRomGpioRtcStateDirty = false;

    _statusRegister = RIO_RTC_STATUS_24H;
    _intRegister = 0;
    _rtcOffset = 0;
    _weekDayOffset = 0;
    return LoadState();
}

RTC_EWRAM RtcPersistence::FileStatus RomGpioRtc::ReadRecord(
    const char* path, void* record, u32 size, u32 magic, u16 version)
{
    using RtcPersistence::FileStatus;
    if (!path || sRtcFileHandleLive) return FileStatus::IoError;
    memset(&sRtcStateFile, 0, sizeof(sRtcStateFile));
    const FRESULT opened = f_open(&sRtcStateFile, path, FA_OPEN_EXISTING | FA_READ);
    if (opened == FR_NO_FILE) return FileStatus::Missing;
    if (opened != FR_OK) return FileStatus::IoError;
    sRtcFileHandleLive = true;

    const u32 actualSize = f_size(&sRtcStateFile);
    u8 header[8] { };
    UINT bytesRead = 0;
    const FRESULT headerResult = actualSize >= sizeof(header)
        ? f_read(&sRtcStateFile, header, sizeof(header), &bytesRead) : FR_OK;
    FileStatus result = FileStatus::Corrupt;
    if (headerResult != FR_OK || (actualSize >= sizeof(header) && bytesRead != sizeof(header)))
        result = FileStatus::IoError;
    else if (actualSize >= sizeof(header))
    {
        u32 foundMagic;
        u16 foundVersion;
        memcpy(&foundMagic, header, sizeof(foundMagic));
        memcpy(&foundVersion, header + 4, sizeof(foundVersion));
        if (foundMagic == magic && foundVersion != version)
            result = FileStatus::UnsupportedVersion;
        else if (foundMagic == magic && actualSize == size)
        {
            memcpy(record, header, sizeof(header));
            UINT remainder = 0;
            const FRESULT rest = f_read(&sRtcStateFile,
                static_cast<u8*>(record) + sizeof(header), size - sizeof(header), &remainder);
            result = rest != FR_OK || remainder != size - sizeof(header)
                ? FileStatus::IoError : FileStatus::ValidCurrent;
        }
    }
    if (f_close(&sRtcStateFile) != FR_OK) return FileStatus::IoError;
    sRtcFileHandleLive = false;
    return result;
}

RTC_EWRAM RtcPersistence::FileStatus RomGpioRtc::ReadLegacyStateFile(
    const char* path, RtcPersistence::StateFile& state)
{
    using RtcPersistence::FileStatus;
    const auto result = ReadRecord(path, &state, sizeof(state),
        RtcPersistence::STATE_MAGIC, RtcPersistence::STATE_VERSION);
    if (result != FileStatus::ValidCurrent) return result;
    if (state.checksum != RtcPersistence::CalculateChecksum(state)) return FileStatus::Corrupt;
    if (!RtcPersistence::MatchesIdentity(state, _identity)) return FileStatus::IdentityMismatch;
    return RtcPersistence::ValidateLegacy(state, _identity)
        ? FileStatus::ValidLegacy : FileStatus::Corrupt;
}

RTC_EWRAM RtcPersistence::FileStatus RomGpioRtc::ReadModernStateFile(
    const char* path, RtcPersistence::StateFileV2& state)
{
    using RtcPersistence::FileStatus;
    const auto result = ReadRecord(path, &state, sizeof(state),
        RtcPersistence::STATE_V2_MAGIC, RtcPersistence::STATE_V2_VERSION);
    if (result != FileStatus::ValidCurrent) return result;
    if (state.checksum != RtcPersistence::CalculateV2Checksum(state)) return FileStatus::Corrupt;
    if (state.gameCode != _identity.gameCode || state.romSize != _identity.romSize ||
        state.headerHash != _identity.headerHash) return FileStatus::IdentityMismatch;
    return RtcPersistence::ValidateV2(state, _identity)
        ? FileStatus::ValidCurrent : FileStatus::Corrupt;
}

RTC_EWRAM RtcPersistence::LoadStatus RomGpioRtc::ScanLegacy()
{
    using RtcPersistence::FileStatus;
    using RtcPersistence::LoadStatus;
    for (u32 i = 0; i < 3; ++i)
    {
        sLegacyStatus[i] = ReadLegacyStateFile(_legacyPaths[i], sLegacyRecords[i]);
        switch (sLegacyStatus[i])
        {
            case FileStatus::Corrupt: return LoadStatus::Corrupt;
            case FileStatus::UnsupportedVersion: return LoadStatus::UnsupportedVersion;
            case FileStatus::IdentityMismatch: return LoadStatus::IdentityMismatch;
            case FileStatus::IoError: _writeError = true; return LoadStatus::IoError;
            default: break;
        }
    }
    return LoadStatus::Ready;
}

RTC_EWRAM RtcPersistence::LoadStatus RomGpioRtc::ScanModern()
{
    using RtcPersistence::FileStatus;
    using RtcPersistence::LoadStatus;
    for (u32 i = 0; i < 3; ++i)
    {
        sModernStatus[i] = ReadModernStateFile(_modernPaths[i], sModernRecords[i]);
        switch (sModernStatus[i])
        {
            case FileStatus::UnsupportedVersion: return LoadStatus::UnsupportedVersion;
            case FileStatus::IdentityMismatch: return LoadStatus::IdentityMismatch;
            case FileStatus::IoError: _writeError = true; return LoadStatus::IoError;
            default: break;
        }
    }
    return LoadStatus::Ready;
}

RTC_EWRAM RtcPersistence::LoadStatus RomGpioRtc::CheckLegacyLineage(
    const RtcPersistence::StateFileV2& state)
{
    using RtcPersistence::FileStatus;
    using RtcPersistence::LoadStatus;
    for (u32 i = 0; i < 3; ++i)
    {
        const bool expected = (state.legacyPresence & (1u << i)) != 0;
        if (expected != (sLegacyStatus[i] == FileStatus::ValidLegacy))
            return LoadStatus::Conflict;
        if (expected && memcmp(&sLegacyRecords[i], state.legacyBytes[i],
                sizeof(RtcPersistence::StateFile)) != 0)
            return LoadStatus::Conflict;
    }
    return LoadStatus::Ready;
}

RTC_EWRAM RtcPersistence::LoadStatus RomGpioRtc::SelectModern(int& selected)
{
    using RtcPersistence::FileStatus;
    using RtcPersistence::LoadStatus;
    selected = -1;
    int firstValid = -1;
    bool hasCorrupt = false;
    bool hasReady = false;
    for (int i = 0; i < 3; ++i)
    {
        if (sModernStatus[i] == FileStatus::Corrupt) hasCorrupt = true;
        if (sModernStatus[i] != FileStatus::ValidCurrent) continue;
        if (firstValid >= 0 &&
            !RtcPersistence::SameLineage(sModernRecords[firstValid], sModernRecords[i]))
            return LoadStatus::Conflict;
        if (firstValid < 0) firstValid = i;
        if (sModernRecords[i].phase == RtcPersistence::PHASE_READY) hasReady = true;
    }
    if (firstValid < 0) return hasCorrupt ? LoadStatus::Corrupt : LoadStatus::Ready;
    if (!hasReady && hasCorrupt) return LoadStatus::Corrupt;

    for (int i = 0; i < 3; ++i)
    {
        if (sModernStatus[i] != FileStatus::ValidCurrent ||
            (hasReady && sModernRecords[i].phase != RtcPersistence::PHASE_READY))
            continue;
        bool dominates = true;
        for (int j = 0; j < 3; ++j)
        {
            if (i == j || sModernStatus[j] != FileStatus::ValidCurrent ||
                (hasReady && sModernRecords[j].phase != RtcPersistence::PHASE_READY))
                continue;
            const u32 candidate = sModernRecords[i].sequence;
            const u32 other = sModernRecords[j].sequence;
            if (candidate == other)
            {
                if (memcmp(&sModernRecords[i], &sModernRecords[j],
                        sizeof(RtcPersistence::StateFileV2)) != 0)
                    return LoadStatus::Conflict;
            }
            else if (candidate - other == 0x80000000u)
                return LoadStatus::Conflict;
            else if (!RtcPersistence::IsSequenceNewer(candidate, other))
                dominates = false;
        }
        if (dominates && selected < 0) selected = i;
    }
    return selected < 0 ? LoadStatus::Conflict : LoadStatus::Ready;
}

RTC_EWRAM void RomGpioRtc::ApplyState(const RtcPersistence::StateFileV2& state)
{
    UpdateDSDateTime();
    const u32 hostSeconds = ToSecondsSinceJanuary2000(sDSRtcDateTime, true);
    _rtcOffset = RtcPersistence::RestoreOffset(
        state.rtcSecondsSince2000,
        state.hostSecondsSince2000,
        hostSeconds);
    _weekDayOffset = state.weekDayOffset % 7;
    _statusRegister = state.statusRegister &
        (RIO_RTC_STATUS_POWER | RIO_RTC_STATUS_WRITE_MASK);
    _intRegister = state.intRegister;
    if (hostSeconds < state.hostSecondsSince2000)
    {
        MarkStateDirty();
    }
}

RTC_EWRAM RtcPersistence::LoadStatus RomGpioRtc::LoadState()
{
    using RtcPersistence::FileStatus;
    using RtcPersistence::LoadStatus;
    auto status = ScanLegacy();
    if (status != LoadStatus::Ready) return status;
    status = ScanModern();
    if (status != LoadStatus::Ready) return status;
    int selected = -1;
    status = SelectModern(selected);
    if (status != LoadStatus::Ready) return status;
    if (selected >= 0)
    {
        _currentRecord = sModernRecords[selected];
        _hasCurrent = true;
        status = CheckLegacyLineage(_currentRecord);
        if (status != LoadStatus::Ready) return status;
        if (_currentRecord.phase == RtcPersistence::PHASE_PENDING)
        {
            // Consent is recorded by the offline tool. Anchor only to the DS
            // clock at this boot; the unknown earlier interval is not added.
            RtcPersistence::StateFileV2& ready = sRtcWriteBuffer;
            ready = _currentRecord;
            UpdateDSDateTime();
            ready.hostSecondsSince2000 = ToSecondsSinceJanuary2000(sDSRtcDateTime, true);
            ready.sequence = 1;
            ready.phase = RtcPersistence::PHASE_READY;
            ready.checksum = RtcPersistence::CalculateV2Checksum(ready);
            if (!WriteStateFile(ready)) return LoadStatus::WriteError;
            _currentRecord = ready;
        }
        ApplyState(_currentRecord);
        return LoadStatus::Ready;
    }

    for (u32 i = 0; i < 3; ++i)
        if (sLegacyStatus[i] == FileStatus::ValidLegacy)
            return LoadStatus::LegacyFound;

    // A fresh user remains in memory until an ordinary GPIO change is dirty.
    // In particular, merely inspecting/starting the program writes no sidecar.
    return LoadStatus::FreshInitialized;
}

RTC_EWRAM bool RomGpioRtc::WriteStateFile(const RtcPersistence::StateFileV2& state)
{
    using RtcPersistence::FileStatus;
    using RtcPersistence::LoadStatus;
    if (_writeError || sRtcFileHandleLive ||
        !RtcPersistence::ValidateV2(state, _identity))
    {
        _writeError = true;
        return false;
    }
    if (ScanLegacy() != LoadStatus::Ready ||
        CheckLegacyLineage(state) != LoadStatus::Ready ||
        ScanModern() != LoadStatus::Ready)
    {
        _writeError = true;
        return false;
    }
    int selected = -1;
    if (SelectModern(selected) != LoadStatus::Ready ||
        (_hasCurrent != (selected >= 0)) ||
        (_hasCurrent && memcmp(&_currentRecord, &sModernRecords[selected],
            sizeof(_currentRecord)) != 0))
    {
        _writeError = true;
        return false;
    }

    int target = -1;
    for (int i = 0; i < 3; ++i)
        if (sModernStatus[i] == FileStatus::Missing) { target = i; break; }
    if (target < 0)
    {
        // Once ready exists, a pending consent record is the oldest slot.
        for (int i = 0; i < 3; ++i)
        {
            if (i == selected || sModernStatus[i] != FileStatus::ValidCurrent) continue;
            if (sModernRecords[i].phase == RtcPersistence::PHASE_PENDING)
            { target = i; break; }
            if (target < 0) target = i;
            else if (sModernRecords[target].phase != RtcPersistence::PHASE_PENDING)
            {
                const u32 a = sModernRecords[target].sequence;
                const u32 b = sModernRecords[i].sequence;
                if (a - b == 0x80000000u) { _writeError = true; return false; }
                if (RtcPersistence::IsSequenceNewer(a, b)) target = i;
            }
        }
    }
    if (target < 0) { _writeError = true; return false; }

    const bool createNew = sModernStatus[target] == FileStatus::Missing;
    memset(&sRtcStateFile, 0, sizeof(sRtcStateFile));
    const FRESULT opened = f_open(&sRtcStateFile, _modernPaths[target],
        (createNew ? FA_CREATE_NEW : FA_CREATE_ALWAYS) | FA_WRITE);
    if (opened != FR_OK) { _writeError = true; return false; }
    sRtcFileHandleLive = true;
    UINT bytesWritten = 0;
    const FRESULT written = f_write(&sRtcStateFile, &state, sizeof(state), &bytesWritten);
    const FRESULT synced = written == FR_OK && bytesWritten == sizeof(state)
        ? f_sync(&sRtcStateFile) : FR_DISK_ERR;
    const FRESULT closed = f_close(&sRtcStateFile);
    if (closed == FR_OK) sRtcFileHandleLive = false;
    if (written != FR_OK || bytesWritten != sizeof(state) ||
        synced != FR_OK || closed != FR_OK)
    {
        _writeError = true;
        return false;
    }

    // A successful close is still not enough: read and compare all 200 bytes.
    RtcPersistence::StateFileV2& readback = sModernRecords[target];
    if (ReadModernStateFile(_modernPaths[target], readback) != FileStatus::ValidCurrent ||
        memcmp(&readback, &state, sizeof(state)) != 0)
    {
        _writeError = true;
        return false;
    }
    return true;
}

RTC_EWRAM bool RomGpioRtc::FlushStateIfDirty()
{
    if (!_stateDirty)
    {
        return true;
    }
    if (_writeError) rtc_persistenceFault();

    UpdateDSDateTime();
    const u32 hostSeconds = ToSecondsSinceJanuary2000(sDSRtcDateTime, true);
    const u32 rtcSeconds = NormalizeSecondsSinceJanuary2000(
        static_cast<s64>(hostSeconds) + _rtcOffset);
    RtcPersistence::StateFileV2& state = sRtcWriteBuffer;
    if (_hasCurrent)
    {
        state = _currentRecord;
        state.sequence++;
    }
    else
    {
        state = { };
        state.magic = RtcPersistence::STATE_V2_MAGIC;
        state.version = RtcPersistence::STATE_V2_VERSION;
        state.payloadLength = RtcPersistence::STATE_V2_PAYLOAD_LENGTH;
        state.gameCode = _identity.gameCode;
        state.romSize = _identity.romSize;
        state.headerHash = _identity.headerHash;
        state.policy = RtcPersistence::POLICY_FRESH;
        state.phase = RtcPersistence::PHASE_READY;
        state.selectedLegacyRole = 0xFF;
    }
    state.hostSecondsSince2000 = hostSeconds;
    state.rtcSecondsSince2000 = rtcSeconds;
    state.weekDayOffset = _weekDayOffset;
    state.statusRegister = _statusRegister;
    state.intRegister = _intRegister;
    state.checksum = RtcPersistence::CalculateV2Checksum(state);

    if (!WriteStateFile(state))
    {
        rtc_persistenceFault();
    }

    _currentRecord = state;
    _hasCurrent = true;
    _stateDirty = false;
    gRomGpioRtcStateDirty = false;
    return true;
}

RTC_EWRAM void RomGpioRtc::MarkStateDirty()
{
    _stateDirty = true;
    gRomGpioRtcStateDirty = true;
    sav_requestFileWrite();
}

RTC_EWRAM void RomGpioRtc::Update(RomGpio& romGpio)
{
    if (!romGpio.GetPinState(ROM_GPIO_PIN_CS))
    {
        _state = RtcTransferState::CommandWaitFallingEdge;
        _shiftRegister = 0;
        _bitCount = 0;
        if (_offsetUpdateRequired)
        {
            _offsetUpdateRequired = false;
            UpdateRtcOffset();
        }
    }
    else
    {
        switch (_state)
        {
            case RtcTransferState::CommandWaitFallingEdge:
            {
                if (!romGpio.GetPinState(ROM_GPIO_PIN_SCK))
                {
                    _state = RtcTransferState::CommandWaitRisingEdge;
                }
                break;
            }
            case RtcTransferState::CommandWaitRisingEdge:
            {
                CommandWaitRisingEdge(romGpio);
                break;
            }
            case RtcTransferState::InDataWaitFallingEdge:
            {
                if (!romGpio.GetPinState(ROM_GPIO_PIN_SCK))
                {
                    _state = RtcTransferState::InDataWaitRisingEdge;
                }
                break;
            }
            case RtcTransferState::InDataWaitRisingEdge:
            {
                HandleInDataWaitRisingEdge(romGpio);
                break;
            }
            case RtcTransferState::OutDataWaitFallingEdge:
            {
                HandleOutDataWaitFallingEdge(romGpio);
                break;
            }
            case RtcTransferState::OutDataWaitRisingEdge:
            {
                if (romGpio.GetPinState(ROM_GPIO_PIN_SCK))
                {
                    _state = RtcTransferState::OutDataWaitFallingEdge;
                }
                break;
            }
            case RtcTransferState::Done:
            {
                break;
            }
        }
    }
}

RTC_EWRAM void RomGpioRtc::CommandWaitRisingEdge(RomGpio& romGpio)
{
    if (!romGpio.GetPinState(ROM_GPIO_PIN_SCK))
    {
        return;
    }

    _shiftRegister = (_shiftRegister << 1) | romGpio.GetPinState(ROM_GPIO_PIN_SIO);
    if (++_bitCount == 8)
    {
        if ((_shiftRegister >> 4) != 0b0110)
        {
            _state = RtcTransferState::Done;
        }
        else
        {
            _command = (_shiftRegister >> 1) & 7;
            if (_command == RIO_RTC_COMMAND_RESET)
            {
                RtcReset();
                _state = RtcTransferState::Done;
            }
            else if (_command >= RIO_RTC_COMMAND_TEST_START)
            {
                _state = RtcTransferState::Done;
            }
            else
            {
                bool isRead = _shiftRegister & 1;
                if (_command == RIO_RTC_COMMAND_DATE_TIME || _command == RIO_RTC_COMMAND_TIME)
                {
                    UpdateDateTime();
                }
                _state = isRead
                    ? RtcTransferState::OutDataWaitFallingEdge
                    : RtcTransferState::InDataWaitFallingEdge;
                _shiftRegister = 0;
                _bitCount = 0;
                _byteIndex = 0;
            }
        }
    }
    else
    {
        _state = RtcTransferState::CommandWaitFallingEdge;
    }
}

RTC_EWRAM void RomGpioRtc::HandleInDataWaitRisingEdge(RomGpio& romGpio)
{
    if (!romGpio.GetPinState(ROM_GPIO_PIN_SCK))
    {
        return;
    }

    _shiftRegister |= romGpio.GetPinState(ROM_GPIO_PIN_SIO) << _bitCount;
    _state = RtcTransferState::InDataWaitFallingEdge;
    if (++_bitCount == 8)
    {
        _bitCount = 0;
        switch (_command)
        {
            case RIO_RTC_COMMAND_STATUS:
            {
                _statusRegister = (_statusRegister & RIO_RTC_STATUS_POWER) | (_shiftRegister & RIO_RTC_STATUS_WRITE_MASK);
                MarkStateDirty();
                break;
            }
            case RIO_RTC_COMMAND_DATE_TIME:
            {
                switch (_byteIndex)
                {
                    case 0:
                        SetYear(_shiftRegister);
                        break;
                    case 1:
                        SetMonth(_shiftRegister);
                        break;
                    case 2:
                        SetDayOfMonth(_shiftRegister);
                        break;
                    case 3:
                        SetDayOfWeek(_shiftRegister);
                        break;
                    case 4:
                        SetHour(_shiftRegister);
                        break;
                    case 5:
                        SetMinute(_shiftRegister);
                        break;
                    case 6:
                        SetSecond(_shiftRegister);
                        break;
                }
                if (++_byteIndex == 7)
                {
                    _offsetUpdateRequired = true;
                    _state = RtcTransferState::Done;
                }
                break;
            }
            case RIO_RTC_COMMAND_TIME:
            {
                switch (_byteIndex)
                {
                    case 0:
                        SetHour(_shiftRegister);
                        break;
                    case 1:
                        SetMinute(_shiftRegister);
                        break;
                    case 2:
                        SetSecond(_shiftRegister);
                        break;
                }
                if (++_byteIndex == 3)
                {
                    _offsetUpdateRequired = true;
                    _state = RtcTransferState::Done;
                }
                break;
            }
            case RIO_RTC_COMMAND_ALARM1:
            {
                mem_swapByte(_shiftRegister, &((u8*)&_intRegister)[_byteIndex]);
                if (++_byteIndex == 2)
                {
                    _state = RtcTransferState::Done;
                    MarkStateDirty();
                }
                break;
            }
            case RIO_RTC_COMMAND_ALARM2:
            {
                mem_swapByte(_shiftRegister, &((u8*)&_intRegister)[1]);
                _state = RtcTransferState::Done;
                MarkStateDirty();
                break;
            }
        }
        _shiftRegister = 0;
    }
}

RTC_EWRAM void RomGpioRtc::HandleOutDataWaitFallingEdge(RomGpio& romGpio)
{
    if (romGpio.GetPinState(ROM_GPIO_PIN_SCK))
    {
        return;
    }

    _state = RtcTransferState::OutDataWaitRisingEdge;
    u32 outputBit = 0;
    switch (_command)
    {
        case RIO_RTC_COMMAND_STATUS:
        {
            outputBit = (_statusRegister >> _bitCount) & 1;
            if (++_bitCount == 8)
            {
                _state = RtcTransferState::Done;
            }
            break;
        }
        case RIO_RTC_COMMAND_DATE_TIME:
        {
            outputBit = (((u8*)&_dateTime)[_bitCount >> 3] >> (_bitCount & 7)) & 1;
            if (++_bitCount == 7 * 8)
            {
                _state = RtcTransferState::Done;
            }
            break;
        }
        case RIO_RTC_COMMAND_TIME:
        {
            outputBit = (((u8*)&_dateTime.time)[_bitCount >> 3] >> (_bitCount & 7)) & 1;
            if (++_bitCount == 3 * 8)
            {
                _state = RtcTransferState::Done;
            }
            break;
        }
        case RIO_RTC_COMMAND_ALARM1:
        {
            outputBit = (_intRegister >> _bitCount) & 1;
            if (++_bitCount == 16)
            {
                _state = RtcTransferState::Done;
            }
            break;
        }
        case RIO_RTC_COMMAND_ALARM2:
        {
            outputBit = (_intRegister >> (_bitCount + 8)) & 1;
            if (++_bitCount == 8)
            {
                _state = RtcTransferState::Done;
            }
            break;
        }
    }

    romGpio.SetPinState(ROM_GPIO_PIN_SIO, outputBit);
}

RTC_EWRAM void RomGpioRtc::RtcReset()
{
    mem_swapByte(0, &_dateTime.date.year);
    mem_swapByte(1, &_dateTime.date.month);
    mem_swapByte(1, &_dateTime.date.monthDay);
    mem_swapByte(0, &_dateTime.date.weekDay);
    mem_swapByte(0, &_dateTime.time.hour);
    mem_swapByte(0, &_dateTime.time.minute);
    mem_swapByte(0, &_dateTime.time.second);
    _statusRegister = 0;
    _intRegister = 0;
    UpdateRtcOffset();
}

RTC_EWRAM void RomGpioRtc::UpdateDSDateTime()
{
    dc_invalidateRange(&sDSRtcDateTime, sizeof(sDSRtcDateTime));
    ipc_sendWordDirect(
        ((((u32)&sDSRtcDateTime) >> 2) << IPC_FIFO_MSG_CHANNEL_BITS) |
        IPC_CHANNEL_RTC);
    while (ipc_isRecvFifoEmpty());
    ipc_recvWordDirect();
}

RTC_EWRAM void RomGpioRtc::UpdateDateTime()
{
    UpdateDSDateTime();
    u32 dsRtcSecondsSince2000 = ToSecondsSinceJanuary2000(sDSRtcDateTime, true);
    u32 secondsSince2000 = NormalizeSecondsSinceJanuary2000(
        static_cast<s64>(dsRtcSecondsSince2000) + _rtcOffset);
    FromSecondsSinceJanuary2000(secondsSince2000, _dateTime, _statusRegister & RIO_RTC_STATUS_24H);
    int weekDay = (_dateTime.date.weekDay + _weekDayOffset) % 7;
    _dateTime.date.weekDay = weekDay < 0 ? weekDay + 7 : weekDay;
}

RTC_EWRAM void RomGpioRtc::UpdateRtcOffset()
{
    UpdateDSDateTime();
    u32 dsRtcSecondsSince2000 = ToSecondsSinceJanuary2000(sDSRtcDateTime, true);
    u32 gbaRtcSecondsSince2000 = ToSecondsSinceJanuary2000(_dateTime, _statusRegister & RIO_RTC_STATUS_24H);
    rio_rtc_datetime_t newDateTime;
    FromSecondsSinceJanuary2000(gbaRtcSecondsSince2000, newDateTime, true);
    _rtcOffset = static_cast<s64>(gbaRtcSecondsSince2000) - dsRtcSecondsSince2000;
    _weekDayOffset = (_dateTime.date.weekDay - newDateTime.date.weekDay) % 7;
    MarkStateDirty();
}

RTC_EWRAM void RomGpioRtc::SetYear(u8 value)
{
    if ((value & 0xF) > 9 ||
        ((value >> 4) & 0xF) > 9)
    {
        value = 0;
    }
    mem_swapByte(value, &_dateTime.date.year);
}

RTC_EWRAM void RomGpioRtc::SetMonth(u8 value)
{
    value &= 0x1F;
    if (value == 0 ||
        (value >= 0x13 && value <= 0x19) ||
        (value & 0xF) > 9)
    {
        value = 1;
    }
    mem_swapByte(value, &_dateTime.date.month);
}

RTC_EWRAM void RomGpioRtc::SetDayOfMonth(u8 value)
{
    value &= 0x3F;
    if (value == 0 ||
        (value >= 0x32 && value <= 0x39) ||
        (value & 0xF) > 9)
    {
        value = 1;
    }

    u32 year = FromBcd(_dateTime.date.year);
    u32 month = FromBcd(_dateTime.date.month);
    u32 daysInMonth = GetNumberOfDaysInMonth(2000 + year, month);
    if (FromBcd(value) > daysInMonth)
    {
        value = 1;
        if (++month == 13)
        {
            month = 1;
        }

        mem_swapByte(ToBcd(month), &_dateTime.date.month);
    }

    mem_swapByte(value, &_dateTime.date.monthDay);
}

RTC_EWRAM void RomGpioRtc::SetDayOfWeek(u8 value)
{
    value &= 7;
    if (value == 7)
    {
        value = 0;
    }
    mem_swapByte(value, &_dateTime.date.weekDay);
}

RTC_EWRAM void RomGpioRtc::SetHour(u8 value)
{
    if (_statusRegister & RIO_RTC_STATUS_24H)
    {
        value &= 0x3F;
        if ((value >= 0x24 && value <= 0x29) ||
            (value & 0xF) > 9)
        {
            value = 0;
        }

        if (FromBcd(value) >= 12)
        {
            value |= 0x80; // PM flag
        }
    }
    else
    {
        value &= 0xBF;
        if (((value & ~0x80) >= 0x12 && (value & ~0x80) <= 0x19) ||
            (value & 0xF) > 9)
        {
            value = 0;
        }
    }
    mem_swapByte(value, &_dateTime.time.hour);
}

RTC_EWRAM void RomGpioRtc::SetMinute(u8 value)
{
    value &= 0x7F;
    if ((value >= 0x60 && value <= 0x79) ||
        (value & 0xF) > 9)
    {
        value = 0;
    }
    mem_swapByte(value, &_dateTime.time.minute);
}

RTC_EWRAM void RomGpioRtc::SetSecond(u8 value)
{
    value &= 0x7F;
    if ((value >= 0x60 && value <= 0x79) ||
        (value & 0xF) > 9)
    {
        value = 0;
    }
    mem_swapByte(value, &_dateTime.time.second);
}

RTC_EWRAM u32 RomGpioRtc::FromBcd(u32 bcdValue) const
{
    return bcdValue - 6 * (bcdValue >> 4);
}

RTC_EWRAM u32 RomGpioRtc::ToBcd(u32 value) const
{
    static const u8 sToBcd[100] =
    {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99
    };

    return sToBcd[value];
}

RTC_EWRAM u32 RomGpioRtc::ToSecondsSinceJanuary2000(const rio_rtc_datetime_t& dateTime, bool time24h) const
{
    u32 yearsSince2000 = FromBcd(dateTime.date.year);
    u32 leapDays = yearsSince2000 / 4;
    if ((yearsSince2000 & 3) != 0)
    {
        leapDays++;
    }

    u32 days = leapDays + yearsSince2000 * 365 + FromBcd(dateTime.date.monthDay) - 1;
    u32 month = FromBcd(dateTime.date.month);
    for (u32 i = 1; i < month; i++)
    {
        days += GetNumberOfDaysInMonth(2000 + yearsSince2000, i);
    }

    // Keep both BCD tens bits: 20-23 hours and 40-59 minutes/seconds
    // are data, not flags. The PM bit is handled separately below.
    u32 hours = FromBcd(dateTime.time.hour & 0x3F);
    if (!time24h && (dateTime.time.hour & 0x80))
    {
        hours += 12;
    }

    return ((days * 24u + hours) * 60u
        + FromBcd(dateTime.time.minute & 0x7F)) * 60u
        + FromBcd(dateTime.time.second & 0x7F);
}

RTC_EWRAM void RomGpioRtc::FromSecondsSinceJanuary2000(u32 secondsSinceJanuary2000, rio_rtc_datetime_t& dateTime, bool time24h) const
{
    dateTime.time.second = ToBcd(secondsSinceJanuary2000 % 60);
    u32 minutesSinceJanuary2000 = secondsSinceJanuary2000 / 60;
    dateTime.time.minute = ToBcd(minutesSinceJanuary2000 % 60);
    u32 hoursSinceJanuary2000 = minutesSinceJanuary2000 / 60;

    u32 hours = hoursSinceJanuary2000 % 24;
    u32 amPmFlag = hours >= 12 ? 0x80 : 0;
    if (!time24h && hours >= 12)
    {
        hours -= 12;
    }
    dateTime.time.hour = ToBcd(hours) | amPmFlag;

    u32 daysSinceJanuary2000 = hoursSinceJanuary2000 / 24;
    dateTime.date.weekDay = ((daysSinceJanuary2000 + 5) % 7); // 1 January 2000 was a Saturday

    u32 year = 2000 + daysSinceJanuary2000 * 4 / 1461;
    u32 remainingDays = ((daysSinceJanuary2000 * 4) % 1461) / 4;

    dateTime.date.year = ToBcd(year - 2000);

    u32 month = 1;
    u32 daysInMonth = GetNumberOfDaysInMonth(year, month);
    if (remainingDays >= daysInMonth)
    {
        remainingDays -= daysInMonth;
        month++;
        daysInMonth = GetNumberOfDaysInMonth(year, month);
        if (remainingDays >= daysInMonth)
        {
            remainingDays -= daysInMonth;
            month++;
            for (; month < 12; month++)
            {
                u32 daysInMonth = GetNumberOfDaysInMonth(year, month);
                if (remainingDays < daysInMonth)
                {
                    break;
                }
                remainingDays -= daysInMonth;
            }
        }
    }

    dateTime.date.month = ToBcd(month);
    dateTime.date.monthDay = ToBcd(remainingDays + 1);
}

RTC_EWRAM u32 RomGpioRtc::NormalizeSecondsSinceJanuary2000(s64 secondsSinceJanuary2000) const
{
    s64 result = secondsSinceJanuary2000 % static_cast<s64>(RtcPersistence::CYCLE_SECONDS);
    if (result < 0)
    {
        result += RtcPersistence::CYCLE_SECONDS;
    }
    return static_cast<u32>(result);
}

RTC_EWRAM u32 RomGpioRtc::GetNumberOfDaysInMonth(u32 year, u32 month) const
{
    static const u8 sDaysPerMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    u32 result = sDaysPerMonth[month - 1];
    if (month == 2)
    {
        // It is sufficient here to check if the year is divisible by 4
        result += ((year & 3) == 0);
    }

    return result;
}
