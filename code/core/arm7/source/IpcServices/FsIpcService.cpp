#include "common.h"
#include "../mmc/sdmmc.h"
#include <libtwl/ipc/ipcSync.h>
#include <string.h>
#include "dldi.h"
#include "FsIpcService.h"

void FsIpcService::Start()
{
    if (isDSiMode())
    {
        SDMMC_init(SDMMC_DEV_CARD);
    }
    ThreadIpcService::Start();
}

void FsIpcService::HandleMessage(u32 data)
{
    auto cmd = reinterpret_cast<const fs_ipc_cmd_t*>(data << 2);
    switch (cmd->cmd)
    {
        case FS_IPC_CMD_DLDI_SETUP:
        {
            SetupDldi(cmd);
            break;
        }
        case FS_IPC_CMD_DLDI_READ_SECTORS:
        {
            DldiReadSectors(cmd);
            break;
        }
        case FS_IPC_CMD_DLDI_WRITE_SECTORS:
        {
            DldiWriteSectors(cmd);
            break;
        }
        case FS_IPC_CMD_DSI_SD_READ_SECTORS:
        {
            DsiSdReadSectors(cmd);
            break;
        }
        case FS_IPC_CMD_DSI_SD_WRITE_SECTORS:
        {
            DsiSdWriteSectors(cmd);
            break;
        }
    }
}

void FsIpcService::SetupDldi(const fs_ipc_cmd_t* cmd) const
{
    memcpy(_dldi_start, cmd->buffer, sizeof(_dldi_start));
    bool result = _DLDI_startup_ptr();
    SendResponseMessage(result);
}

static void completeTransfer(const fs_ipc_cmd_t* cmd, u32 sequence, bool success)
{
    auto* completion = const_cast<fs_ipc_result_t*>(&cmd->completion);
    completion->result = success ? FS_RESULT_SUCCESS : FS_RESULT_IO_ERROR;
    // ARM7 has no data cache/write buffer. Publish identity last, then notify.
    asm volatile("" ::: "memory");
    completion->completedSequence = sequence;
    asm volatile("" ::: "memory");
    ipc_setArm7SyncBits(sequence & 15);
}

void FsIpcService::DldiReadSectors(const fs_ipc_cmd_t* cmd) const
{
    const u32 sequence = cmd->sequence;
    completeTransfer(cmd, sequence, _DLDI_readSectors_ptr(cmd->sector, cmd->count, cmd->buffer));
}

void FsIpcService::DldiWriteSectors(const fs_ipc_cmd_t* cmd) const
{
    const u32 sequence = cmd->sequence;
    completeTransfer(cmd, sequence, _DLDI_writeSectors_ptr(cmd->sector, cmd->count, cmd->buffer));
}

void FsIpcService::DsiSdReadSectors(const fs_ipc_cmd_t* cmd) const
{
    const u32 sequence = cmd->sequence;
    completeTransfer(cmd, sequence,
        SDMMC_readSectors(SDMMC_DEV_CARD, cmd->sector, cmd->buffer, cmd->count) == 0);
}

void FsIpcService::DsiSdWriteSectors(const fs_ipc_cmd_t* cmd) const
{
    const u32 sequence = cmd->sequence;
    completeTransfer(cmd, sequence,
        SDMMC_writeSectors(SDMMC_DEV_CARD, cmd->sector, cmd->buffer, cmd->count) == 0);
}
