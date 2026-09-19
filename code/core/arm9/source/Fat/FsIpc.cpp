#include "common.h"
#include <libtwl/ipc/ipcFifoSystem.h>
#include <libtwl/ipc/ipcFifo.h>
#include <libtwl/ipc/ipcSync.h>
#include <string.h>
#include <stdint.h>
#include "IpcChannels.h"
#include "FsIpcCommand.h"
#include "cp15.h"
#include "Cpsr.h"
#include "VirtualMachine/VMNestedIrq.h"
#include "FsIpc.h"

[[gnu::section(".ewram.bss")]]
alignas(32) static fs_ipc_cmd_t sIpcCommand;
[[gnu::section(".ewram.bss")]]
alignas(32) static u8 sTempBuffers[2][512];

static FsWaitToken* volatile sCurrentWaitToken = nullptr;
static u32 sSequence;
static u32 sActiveSequence;

// Called with IRQs disabled. IPCSYNC is a notification, not transaction ID.
static FsTransactionResult pollCurrentTransaction()
{
    FsWaitToken* current = sCurrentWaitToken;
    if (!current)
        return FS_RESULT_NO_TRANSACTION;
    const u32 ipcSync = REG_IPCSYNC;
    if ((ipcSync & IPCSYNC_REMOTE_DATA_MASK) != (sActiveSequence & 15))
        return FS_RESULT_PENDING;
    dc_invalidateRange(&sIpcCommand.completion, sizeof(sIpcCommand.completion));
    if (sIpcCommand.completion.completedSequence != sActiveSequence)
        return FS_RESULT_STALE_COMPLETION;
    const u32 result = sIpcCommand.completion.result;
    if (result != FS_RESULT_SUCCESS && result != FS_RESULT_IO_ERROR)
        return FS_RESULT_STALE_COMPLETION;
    current->result = static_cast<FsTransactionResult>(result);
    current->transactionComplete = true;
    sCurrentWaitToken = nullptr;
    return current->result;
}

extern "C" FsTransactionResult fs_pollTransaction(FsWaitToken* token)
{
    const u32 irqs = arm_disableIrqs();
    FsTransactionResult result;
    if (token && token->transactionComplete)
        result = token->result;
    else if (token && (token != sCurrentWaitToken || token->sequence != sActiveSequence))
        result = FS_RESULT_TOKEN_MISMATCH;
    else
        result = pollCurrentTransaction();
    arm_restoreIrqs(irqs);
    return result;
}

extern "C" [[gnu::noinline]]
u32 fs_waitForCompletion(FsWaitToken* token, bool keepIrqsDisabled)
{
    u32 irqs = arm_disableIrqs();
#ifdef GBAR3_IRQ_YIELDING
    const bool yielding = vm_disableIrqYielding();
#endif
    while (true)
    {
        if (token && token->transactionComplete)
            break;
        if (token && (token != sCurrentWaitToken || token->sequence != sActiveSequence))
        {
            token->result = FS_RESULT_TOKEN_MISMATCH;
            token->transactionComplete = true;
            break;
        }
        const auto result = pollCurrentTransaction();
        if (result == FS_RESULT_NO_TRANSACTION || result == FS_RESULT_SUCCESS || result == FS_RESULT_IO_ERROR)
            break;
#ifdef GBAR3_IRQ_YIELDING
        if (yielding && !(irqs & 0x80) && vm_yieldGbaIrqs())
            continue;
#endif
        arm_restoreIrqs(irqs);
        irqs = arm_disableIrqs();
    }
#ifdef GBAR3_IRQ_YIELDING
    vm_restoreIrqYielding(yielding);
#endif
    if (!keepIrqsDisabled)
        arm_restoreIrqs(irqs);
    return irqs;
}

extern "C" u32 fs_waitForCompletionOfCurrentTransaction(bool keepIrqsDisabled)
{
    return fs_waitForCompletion(nullptr, keepIrqsDisabled);
}

extern "C" FsTransactionResult fs_cancelTransaction(FsWaitToken* token)
{
    const u32 irqs = arm_disableIrqs();
    if (!token || token != sCurrentWaitToken || token->sequence != sActiveSequence)
    {
        arm_restoreIrqs(irqs);
        return token ? FS_RESULT_TOKEN_MISMATCH : FS_RESULT_NO_TRANSACTION;
    }
    // No driver cancellation primitive exists. Drain before releasing memory.
    fs_waitForCompletion(token, true);
    token->result = FS_RESULT_CANCELED;
    arm_restoreIrqs(irqs);
    return FS_RESULT_CANCELED;
}

static bool validTransfer(FsDevice device, const void* buffer, u32 sector, u32 count)
{
    return (device == FS_DEVICE_DLDI || device == FS_DEVICE_DSI_SD) && buffer &&
        count && count <= 0xFFFF && sector <= UINT32_MAX - (count - 1);
}

static void executeIpcCommandAsync(FsDevice device, bool write, void* buffer,
    u32 sector, u32 count, FsWaitToken* token)
{
    // Retire the old token BEFORE touching a reused token or DMA buffer.
    const u32 irqs = fs_waitForCompletion(nullptr, true);
    if (!token)
    {
        arm_restoreIrqs(irqs);
        return;
    }
    if (!validTransfer(device, buffer, sector, count))
    {
        token->sequence = 0;
        token->result = FS_RESULT_INVALID_ARGUMENT;
        token->transactionComplete = true;
        arm_restoreIrqs(irqs);
        return;
    }
    if (++sSequence == 0)
        ++sSequence;
    sActiveSequence = sSequence;
    token->sequence = sSequence;
    token->result = FS_RESULT_PENDING;
    token->transactionComplete = false;
    sIpcCommand.cmd = device == FS_DEVICE_DLDI
        ? (write ? FS_IPC_CMD_DLDI_WRITE_SECTORS : FS_IPC_CMD_DLDI_READ_SECTORS)
        : (write ? FS_IPC_CMD_DSI_SD_WRITE_SECTORS : FS_IPC_CMD_DSI_SD_READ_SECTORS);
    sIpcCommand.buffer = buffer;
    sIpcCommand.sector = sector;
    sIpcCommand.count = count;
    sIpcCommand.sequence = sSequence;
    // No ARM7 owner remains: resetting/cleaning both lines is now safe.
    sIpcCommand.completion.result = FS_RESULT_PENDING;
    sIpcCommand.completion.completedSequence = 0;
    if (write)
        dc_flushRange(buffer, 512 * count);
    else
        dc_invalidateRange(buffer, 512 * count);
    dc_flushRange(&sIpcCommand, sizeof(sIpcCommand));
    dc_drainWriteBuffer();
    sCurrentWaitToken = token;
    ipc_setArm9SyncBits(sSequence & 15);
    ipc_sendWordDirect((static_cast<u32>(reinterpret_cast<uintptr_t>(&sIpcCommand)) >> 2 << IPC_FIFO_MSG_CHANNEL_BITS) | IPC_CHANNEL_FS);
    arm_restoreIrqs(irqs);
}

extern "C" void fs_readCacheAlignedSectorsAsync(FsDevice device, void* buffer, u32 sector, u32 count, FsWaitToken* token)
{
    executeIpcCommandAsync(device, false, buffer, sector, count, token);
}

extern "C" void fs_writeCacheAlignedSectorsAsync(FsDevice device, const void* buffer, u32 sector, u32 count, FsWaitToken* token)
{
    executeIpcCommandAsync(device, true, const_cast<void*>(buffer), sector, count, token);
}

static FsTransactionResult transferSectors(FsDevice device, void* buffer, u32 sector, u32 count, bool write)
{
    if (!validTransfer(device, buffer, sector, count))
        return FS_RESULT_INVALID_ARGUMENT;
    const uintptr_t address = reinterpret_cast<uintptr_t>(buffer);
    if ((address >> 24) == 2 && !(address & 31))
    {
        FsWaitToken token;
        executeIpcCommandAsync(device, write, buffer, sector, count, &token);
        fs_waitForCompletion(&token, false);
        return token.result;
    }
    // Bounce storage stays owned under IRQ exclusion through copy-out. An IRQ
    // must not reuse it between physical completion and the caller's copy.
    // Stack buffers could be in DTCM, invisible to ARM7.
    const u32 irqs = fs_waitForCompletion(nullptr, true);
    FsTransactionResult result = FS_RESULT_SUCCESS;
    for (u32 i = 0; i < count; ++i)
    {
        auto* bytes = static_cast<u8*>(buffer) + 512 * i;
        if (write)
            memcpy(sTempBuffers[0], bytes, 512);
        FsWaitToken token;
        executeIpcCommandAsync(device, write, sTempBuffers[0], sector + i, 1, &token);
        fs_waitForCompletion(&token, true);
        result = token.result;
        if (result != FS_RESULT_SUCCESS)
            break;
        if (!write)
            memcpy(bytes, sTempBuffers[0], 512);
    }
    arm_restoreIrqs(irqs);
    return result;
}

extern "C" FsTransactionResult fs_readSectors(FsDevice device, void* buffer, u32 sector, u32 count)
{
    return transferSectors(device, buffer, sector, count, false);
}

extern "C" FsTransactionResult fs_writeSectors(FsDevice device, const void* buffer, u32 sector, u32 count)
{
    return transferSectors(device, const_cast<void*>(buffer), sector, count, true);
}
