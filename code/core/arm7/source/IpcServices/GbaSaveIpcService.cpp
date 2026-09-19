#include "common.h"
#include "GbaSaveIpcService.h"

#define SAVE_WAIT_FRAMES    10

void GbaSaveIpcService::OnMessageReceived(u32 data)
{
    auto cmd = static_cast<GbaSaveIpcCommand>(data & 0x7);
    switch (cmd)
    {
        case GBA_SAVE_IPC_CMD_SETUP:
        {
            _saveShared = reinterpret_cast<gba_save_shared_t*>((data >> 3) << 5);
            SendResponseMessage(0);
            break;
        }
    }
}

void GbaSaveIpcService::Update()
{
    if (!_saveShared || _saveShared->saveDataSize == 0 || isDSiMode())
        return;

    switch (_saveShared->saveState)
    {
        case GBA_SAVE_STATE_CLEAN:
        {
            break;
        }
        case GBA_SAVE_STATE_DIRTY:
        {
            _saveShared->saveState = GBA_SAVE_STATE_WAIT;
            _saveWaitCounter = SAVE_WAIT_FRAMES;
            break;
        }
        case GBA_SAVE_STATE_WAIT:
        {
            if (--_saveWaitCounter == 0)
            {
                _saveShared->saveState = GBA_SAVE_STATE_WRITE;
            }
            break;
        }
        case GBA_SAVE_STATE_ERROR:
            // No automatic retry of a failed attempt. Preserve the error.
            break;
        case GBA_SAVE_STATE_WRITE:
        {
            // arm9 will set the state back to GBA_SAVE_STATE_CLEAN
            break;
        }
    }
}

SaveFlushResult GbaSaveIpcService::FlushSaveIfDirty()
{
    if (!_saveShared) return SaveFlushResult::Clean;
    // File-backed EEPROM/FLASH has no shared SRAM buffer, but can still fail.
    if (_saveShared->saveState == GBA_SAVE_STATE_ERROR) return SaveFlushResult::Error;
    if (_saveShared->saveDataSize == 0) return SaveFlushResult::Clean;
    switch (_saveShared->saveState)
    {
        case GBA_SAVE_STATE_CLEAN:
            return SaveFlushResult::Clean;
        case GBA_SAVE_STATE_DIRTY:
        case GBA_SAVE_STATE_WAIT:
            _saveShared->saveState = GBA_SAVE_STATE_WRITE;
            return SaveFlushResult::Pending;
        default:
            return SaveFlushResult::Pending;
    }
}
