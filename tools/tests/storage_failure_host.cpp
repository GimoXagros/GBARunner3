// Real ARM7 driver handlers, ARM9 FsIpc and diskio are compiled below.
// Hardware registers, cache maintenance and drivers are deterministic fakes.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
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
u32 arm_disableIrqs() { return 0x80; }
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
        auto read=[](u32 address)->const void* { return sdc_loadRomBlockDirect(address); };
        auto fast=[](const u32* p,u32 length,const u32* sig)->const u32* {
            for(u32 i=0;i+16<=length;i+=4) if(!std::memcmp(reinterpret_cast<const u8*>(p)+i,sig,16)) return p+i/4;
            return nullptr;
        };
        result(name+"_failed_read_rejects_stale_signature",sav_findSignature16(signature,0x08000000,0x08002000,read,fast)==UINT32_MAX);
    }
    result("unknown_device_rejected",disk_read(9,buffer,0,1)==RES_PARERR && disk_write(9,buffer,0,1)==RES_PARERR);
}
