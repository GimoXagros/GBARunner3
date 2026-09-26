#pragma once
#include "FsTransactionResult.h"

typedef enum
{
    FS_DEVICE_DLDI,
    FS_DEVICE_DSI_SD
} FsDevice;

/// @brief Struct used to track completion of an async sd read or write.
typedef struct
{
    vu16 transactionComplete;
    vu32 sequence;
    volatile FsTransactionResult result;
} FsWaitToken;

#ifdef __cplusplus
extern "C" {
#endif

FsTransactionResult fs_readSectors(FsDevice device, void* buffer, u32 sector, u32 count);
FsTransactionResult fs_writeSectors(FsDevice device, const void* buffer, u32 sector, u32 count);

void fs_readCacheAlignedSectorsAsync(FsDevice device, void* buffer, u32 sector, u32 count, FsWaitToken* waitToken);
void fs_writeCacheAlignedSectorsAsync(FsDevice device, const void* buffer, u32 sector, u32 count, FsWaitToken* waitToken);
u32 fs_waitForCompletion(FsWaitToken* waitToken, bool keepIrqsDisabled);
u32 fs_waitForCompletionOfCurrentTransaction(bool keepIrqsDisabled);
// Poll does not wait; stale completion is observed but never retires ownership.
FsTransactionResult fs_pollTransaction(FsWaitToken* waitToken);
// Cancellation drains ARM7 first: buffer/command lifetime cannot end early.
FsTransactionResult fs_cancelTransaction(FsWaitToken* waitToken);

#ifdef __cplusplus
}
#endif
