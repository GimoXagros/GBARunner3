#pragma once
#include <stddef.h>
#include "FsTransactionResult.h"

// ARM7 owns this cache line once the command is submitted. ARM9 invalidates
// it before reading, and never cleans it while ARM7 may still be writing.
typedef struct alignas(32)
{
    volatile u32 result;
    volatile u32 completedSequence;
} fs_ipc_result_t;

typedef enum
{
    FS_IPC_CMD_DLDI_SETUP,
    FS_IPC_CMD_DLDI_READ_SECTORS,
    FS_IPC_CMD_DLDI_WRITE_SECTORS,
    FS_IPC_CMD_DSI_SD_READ_SECTORS,
    FS_IPC_CMD_DSI_SD_WRITE_SECTORS
} FsIpcCommand;

typedef struct alignas(32)
{
    u32 cmd;
    void* buffer;
    u32 sector;
    u32 count;
    u32 sequence;
    fs_ipc_result_t completion;
} fs_ipc_cmd_t;

static_assert(sizeof(fs_ipc_result_t) == 32);
static_assert(offsetof(fs_ipc_cmd_t, completion) == 32);
static_assert(sizeof(fs_ipc_cmd_t) == 64);
