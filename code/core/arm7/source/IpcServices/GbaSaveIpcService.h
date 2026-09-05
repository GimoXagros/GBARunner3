#pragma once
#include "ThreadIpcService.h"
#include "GbaSaveShared.h"
#include "GbaSaveIpcCommand.h"
#include "IpcChannels.h"

enum class SaveFlushResult { Clean, Pending, Error };

class GbaSaveIpcService : public IpcService
{
    gba_save_shared_t* _saveShared = nullptr;
    u32 _saveWaitCounter;

public:
    GbaSaveIpcService()
        : IpcService(IPC_CHANNEL_GBA_SAVE) { }

    void OnMessageReceived(u32 data) override;

    void Update();
    SaveFlushResult FlushSaveIfDirty();
};
