// Real ARM7 driver handlers, ARM9 FsIpc and diskio are compiled below.
// Hardware registers, cache maintenance and drivers are deterministic fakes.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <functional>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
using vu16=volatile u16; using vu32=volatile u32;
using BYTE=u8; using DWORD=u32; using UINT=unsigned; using DSTATUS=u8;
enum DRESULT { RES_OK, RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR };
constexpr int DEV_FAT=0, DEV_SD=1, DEV_PC=2, STA_NOINIT=1;
constexpr unsigned IPCSYNC_LOCAL_DATA_SHIFT=8, IPCSYNC_REMOTE_DATA_MASK=15;
constexpr unsigned IPC_FIFO_MSG_CHANNEL_BITS=4, IPC_CHANNEL_FS=2, SDMMC_DEV_CARD=0;
#include "FsIpcCommand.h"
#include "Fat/FsIpc.h"
#include "Save/SaveSignatureSearch.h"
u32 REG_IPCSYNC;
bool gIrqYieldingEnabled=false, failDriver=false, driverCalled=false;
bool deferCommand=false, commandQueued=false;
unsigned driverCalls=0, invalidations=0;
std::function<void()> onPoll;
unsigned pollDelay=0, pollEntries=0;
u32 arm_disableIrqs() {
    ++pollEntries;
    if (onPoll && !pollDelay) { auto action=std::move(onPoll); onPoll=nullptr; action(); }
    else if(onPoll) --pollDelay;
    return 0x80;
}
void arm_restoreIrqs(u32) {}
void vm_disableIrqYielding() {}
void vm_restoreIrqYielding(bool) {}
bool vm_yieldGbaIrqs() { return false; }
void dc_flushRange(const void*, unsigned) {}
void dc_invalidateRange(const void*, unsigned) { ++invalidations; }
u32 ipc_getArm9SyncBits() { return REG_IPCSYNC >> 8 & 15; }
u32 ipc_getArm7SyncBits() { return REG_IPCSYNC & 15; }
void ipc_setArm9SyncBits(u32 v) { REG_IPCSYNC=(REG_IPCSYNC&~0xF00)|((v&15)<<8); }
void ipc_setArm7SyncBits(u32 v) { REG_IPCSYNC=(REG_IPCSYNC&~15)|(v&15); }
void ipc_sendWordDirect(u32);
void dc_drainWriteBuffer();
std::vector<u8> disk(16384+512,0x35);
bool driverRead(u32 sector,u32 count,void* data) {
    driverCalled=true; ++driverCalls;
    if(failDriver) return false;
    std::memcpy(data,disk.data()+sector*512,count*512); return true;
}
bool driverWrite(u32 sector,u32 count,const void* data) {
    driverCalled=true; ++driverCalls;
    if(failDriver) return false;
    std::memcpy(disk.data()+sector*512,data,count*512); return true;
}
bool _DLDI_readSectors_ptr(u32 s,u32 n,void* p) { return driverRead(s,n,p); }
bool _DLDI_writeSectors_ptr(u32 s,u32 n,const void* p) { return driverWrite(s,n,p); }
u32 SDMMC_readSectors(u8,u32 s,void* p,u16 n) { return driverRead(s,n,p)?0:1; }
u32 SDMMC_writeSectors(u8,u32 s,const void* p,u16 n) { return driverWrite(s,n,p)?0:1; }
struct FsIpcService {
    void DldiReadSectors(const fs_ipc_cmd_t*) const;
    void DldiWriteSectors(const fs_ipc_cmd_t*) const;
    void DsiSdReadSectors(const fs_ipc_cmd_t*) const;
    void DsiSdWriteSectors(const fs_ipc_cmd_t*) const;
};
#include "production_arm7_storage.h"
#include "production_fs_ipc.h"
#include "production_diskio.h"
#include "SdCache/SdCacheDefs.h"
struct FATFS { u32 pdrv,csize,database; };
struct FIL { struct { FATFS* fs; } obj; u32* cltbl; u32 size; };
FATFS fs{DEV_FAT,8,1}; FIL gFile{{&fs},nullptr,8192};
u32 f_size(FIL* f) { return f->size; }
constexpr u32 CREATE_LINKMAP=UINT32_MAX;
int f_lseek(FIL* f,u32) { f->cltbl[1]=2; f->cltbl[2]=2; f->cltbl[3]=0; return 0; }
constexpr int FR_OK=0;
alignas(32) u8 sdc_cache[SDC_BLOCK_COUNT][SDC_BLOCK_SIZE];
u32 arm_getCpsr() { return 0x13; }
void vm_enableNestedIrqs() {} void vm_disableNestedIrqs() {}
void dc_drainWriteBuffer() {} void jit_resetDynamicRomBlock(void*) {}
void ic_invalidateAll() {}
[[noreturn]] void sdc_storageFault(u32) { throw std::runtime_error("storage fault"); }
void logAddress(u32) {}
#include "production_sd_cache.h"
void executeQueuedCommand() {
    FsIpcService service;
    switch(sIpcCommand.cmd) {
        case FS_IPC_CMD_DLDI_READ_SECTORS: service.DldiReadSectors(&sIpcCommand); break;
        case FS_IPC_CMD_DLDI_WRITE_SECTORS: service.DldiWriteSectors(&sIpcCommand); break;
        case FS_IPC_CMD_DSI_SD_READ_SECTORS: service.DsiSdReadSectors(&sIpcCommand); break;
        case FS_IPC_CMD_DSI_SD_WRITE_SECTORS: service.DsiSdWriteSectors(&sIpcCommand); break;
        default: std::abort();
    }
    commandQueued=false;
}
void ipc_sendWordDirect(u32) {
    commandQueued=true;
    if(!deferCommand) executeQueuedCommand();
}
void result(const std::string& name,bool ok) { std::cout<<name<<':'<<(ok?"PASS":"FAIL")<<'\n'; }
int main() {
    alignas(32) u8 buffer[8192];
    for(auto device:{DEV_FAT,DEV_SD}) {
        std::string name=device==DEV_FAT?"dldi":"dsi";
        failDriver=false; std::memset(buffer,0x77,sizeof(buffer));
        result(name+"_normal_read",disk_read(device,buffer,0,2)==RES_OK && buffer[0]==disk[0]);
        buffer[0]=0x79;
        result(name+"_normal_write",disk_write(device,buffer,0,2)==RES_OK && disk[0]==0x79);
        failDriver=true; driverCalled=false; std::memset(buffer,0xAA,sizeof(buffer));
        std::memset(sTempBuffers,0xAA,sizeof(sTempBuffers));
        const auto readResult=disk_read(device,buffer,0,2);
        result(name+"_failed_read_propagated",driverCalled && readResult==RES_ERROR);
        result(name+"_failed_read_keeps_stale_bytes",buffer[0]==0xAA && buffer[1023]==0xAA);
        driverCalled=false; auto before=disk;
        const auto writeResult=disk_write(device,buffer,0,2);
        result(name+"_failed_write_propagated",driverCalled && writeResult==RES_ERROR);
        result(name+"_failed_write_not_persisted",disk==before);

        // Execute the actual async entry/wait functions without depending on
        // the host buffer's address to select the aligned branch.
        FsWaitToken token{};
        deferCommand=true;
        fs_readCacheAlignedSectorsAsync(device==DEV_FAT?FS_DEVICE_DLDI:FS_DEVICE_DSI_SD,buffer,0,8,&token);
        result(name+"_async_pending",commandQueued && !token.transactionComplete);
        executeQueuedCommand(); fs_waitForCompletion(&token,false); deferCommand=false;
        result(name+"_failed_async_acknowledged_complete",token.transactionComplete);

        // Execute the actual SdCache loader and map publication, including
        // one-slot replacement, on top of the same actual FsIpc/ARM7 path.
        const u32 signature[4]={0x12345678,0x90ABCDEF,0x13572468,0x02468ACE};
        fs.pdrv=device; sdc_init(); sBlockCount=1;
        std::memset(sdc_cache[0],0,SDC_BLOCK_SIZE);
        std::memcpy(sdc_cache[0]+4096-12,signature,12);
        std::memcpy(sdc_cache[0],signature+3,4);
        auto read=[](u32 address)->const void* { return sdc_tryLoadRomBlock(address); };
        auto fast=[](const u32* p,u32 length,const u32* sig)->const u32* {
            for(u32 i=0;i+16<=length;i+=4) if(!std::memcmp(reinterpret_cast<const u8*>(p)+i,sig,16)) return p+i/4;
            return nullptr;
        };
        result(name+"_failed_read_rejects_stale_signature",sav_findSignature16(signature,0x08000000,0x08002000,read,fast)==UINT32_MAX);
    }
    result("unknown_device_rejected",disk_read(9,buffer,0,1)==RES_PARERR && disk_write(9,buffer,0,1)==RES_PARERR);
    result("zero_length_rejected",disk_read(DEV_FAT,buffer,0,0)==RES_PARERR);
    result("sector_overflow_rejected",disk_read(DEV_FAT,buffer,UINT32_MAX,2)==RES_PARERR);
    result("dsi_count_truncation_rejected",disk_read(DEV_SD,buffer,0,65536)==RES_PARERR);
    failDriver=false;
    FsWaitToken a{}, b{}, alien{};
    result("no_active_transaction",fs_pollTransaction(nullptr)==FS_RESULT_NO_TRANSACTION);
    result("cancel_without_token",fs_cancelTransaction(nullptr)==FS_RESULT_NO_TRANSACTION);
    fs_readCacheAlignedSectorsAsync(FS_DEVICE_DLDI,buffer,0,1,&a);
    result("completion_before_polling",fs_pollTransaction(&a)==FS_RESULT_SUCCESS && a.transactionComplete);
    deferCommand=true;
    fs_readCacheAlignedSectorsAsync(FS_DEVICE_DLDI,buffer,0,1,&a);
    result("pending_poll",fs_pollTransaction(&a)==FS_RESULT_PENDING);
    alien.sequence=a.sequence;
    result("token_identity_mismatch",fs_pollTransaction(&alien)==FS_RESULT_TOKEN_MISMATCH && !a.transactionComplete);
    ipc_setArm7SyncBits(a.sequence&15);
    sIpcCommand.completion.completedSequence=a.sequence-16;
    sIpcCommand.completion.result=FS_RESULT_SUCCESS;
    result("stale_completion_rejected",fs_pollTransaction(&a)==FS_RESULT_STALE_COMPLETION && !a.transactionComplete && sCurrentWaitToken==&a);
    onPoll=[] { executeQueuedCommand(); };
    pollDelay=2;
    const unsigned beforePoll=pollEntries;
    fs_waitForCompletion(&a,false);
    result("completion_during_polling",a.result==FS_RESULT_SUCCESS && a.transactionComplete && pollEntries>=beforePoll+3);

    failDriver=true;
    fs_readCacheAlignedSectorsAsync(FS_DEVICE_DLDI,buffer,0,1,&a);
    const u32 oldSequence=a.sequence;
    onPoll=[] { executeQueuedCommand(); failDriver=false; deferCommand=false; };
    fs_readCacheAlignedSectorsAsync(FS_DEVICE_DLDI,buffer,1,1,&b);
    fs_waitForCompletion(&b,false);
    result("nested_command_keeps_old_result",a.result==FS_RESULT_IO_ERROR && a.sequence==oldSequence && b.result==FS_RESULT_SUCCESS && b.sequence!=a.sequence);
    fs_waitForCompletion(&a,false);
    result("old_token_wait_does_not_claim_new_result",a.result==FS_RESULT_IO_ERROR);

    deferCommand=true;
    fs_readCacheAlignedSectorsAsync(FS_DEVICE_DLDI,buffer,0,1,&a);
    onPoll=[&] {
        executeQueuedCommand();
        deferCommand=false;
        fs_readCacheAlignedSectorsAsync(FS_DEVICE_DLDI,buffer,1,1,&b);
        fs_waitForCompletion(&b,false);
    };
    fs_waitForCompletion(&a,false);
    result("reentrant_wait_retains_each_owner",a.transactionComplete && b.transactionComplete &&
        a.result==FS_RESULT_SUCCESS && b.result==FS_RESULT_SUCCESS && a.sequence!=b.sequence);

    deferCommand=true;
    fs_readCacheAlignedSectorsAsync(FS_DEVICE_DLDI,buffer,0,1,&a);
    const u32 reuseSequence=a.sequence;
    onPoll=[] { executeQueuedCommand(); deferCommand=false; };
    fs_readCacheAlignedSectorsAsync(FS_DEVICE_DLDI,buffer,1,1,&a);
    fs_waitForCompletion(&a,false);
    result("reused_token_drains_before_reset",a.sequence!=reuseSequence && a.result==FS_RESULT_SUCCESS);
    deferCommand=true;
    fs_readCacheAlignedSectorsAsync(FS_DEVICE_DLDI,buffer,0,1,&a);
    onPoll=[] { executeQueuedCommand(); };
    result("canceled_transaction_drained",fs_cancelTransaction(&a)==FS_RESULT_CANCELED && !commandQueued && !sCurrentWaitToken && a.transactionComplete);
    deferCommand=false;
    sSequence=UINT32_MAX;
    fs_readCacheAlignedSectorsAsync(FS_DEVICE_DLDI,buffer,0,1,&a);
    fs_waitForCompletion(&a,false);
    result("sequence_wrap_skips_zero",a.sequence==1 && a.result==FS_RESULT_SUCCESS);

    fs.pdrv=DEV_FAT;
    sdc_init(); sBlockCount=1; failDriver=false;
    result("cache_first_success",sdc_tryLoadRomBlock(0x08000000)==sdc_cache[0]);
    failDriver=true;
    result("failed_replacement_unpublishes_both",sdc_tryLoadRomBlock(0x08001000)==nullptr &&
        !sdc_romBlockToCacheBlock[0] && !sdc_romBlockToCacheBlock[1] && sCacheBlockToRomBlock[0]==SDC_ROM_BLOCK_INVALID);
    bool stopped=false;
    try { (void)sdc_loadRomBlockDirect(0x08001000); } catch(const std::runtime_error&) { stopped=true; }
    result("jit_dma_ordinary_entry_fail_closed",stopped && !sdc_romBlockToCacheBlock[1]);
    bool repeated=true;
    for(int i=0;i<5;++i) repeated &= sdc_tryLoadRomBlock(0x08001000)==nullptr && !sdc_romBlockToCacheBlock[1];
    result("consecutive_failures_not_published",repeated);
    stopped=false;
    try { (void)sdc_loadRomBlockForPatching(0x08001000); } catch(const std::runtime_error&) { stopped=true; }
    result("failed_permanent_slot_rolled_back",stopped && sBlockCount==1 && !sdc_romBlockToCacheBlock[1]);
    failDriver=false;
    result("explicit_later_read_succeeds",sdc_tryLoadRomBlock(0x08001000)==sdc_cache[0] && sdc_romBlockToCacheBlock[1]==sdc_cache[0]);

    sdc_init(); sBlockCount=2; failDriver=false;
    loadRomBlock(0,0);
    failDriver=true; stopped=false;
    try { (void)sdc_loadRomBlockForPatching(0x08000000); } catch(const std::runtime_error&) { stopped=true; }
    result("failed_promotion_preserves_valid_backing",stopped && sBlockCount==2 &&
        sdc_romBlockToCacheBlock[0]==sdc_cache[0] && sCacheBlockToRomBlock[0]==0 &&
        sCacheBlockToRomBlock[1]==SDC_ROM_BLOCK_INVALID);
    failDriver=false;
    // Call the loader directly to avoid a host 64-bit pointer in the target
    // 32-bit patch-address return. This executes the same successful promotion.
    --sBlockCount;
    result("successful_promotion_retires_previous_owner",loadRomBlock(0,1)==sdc_cache[1] &&
        sdc_romBlockToCacheBlock[0]==sdc_cache[1] && sCacheBlockToRomBlock[0]==SDC_ROM_BLOCK_INVALID);

    // Unaligned write must never copy the bounce buffer back into const input.
    std::vector<u8> writeBuffer(1025,0x61); writeBuffer[513]=0xB2;
    const auto immutable=writeBuffer;
    result("unaligned_write_source_immutable",disk_write(DEV_FAT,writeBuffer.data()+1,0,2)==RES_OK && writeBuffer==immutable);
}
