// Actual FatFs -> diskio -> FsIpc -> ARM7 handlers; only device/register/cache
// operations are fakes. The filesystem image is synthetic and memory-only.
#include "storage_transport_fixture.h"
void dc_drainWriteBuffer() {}
void mem_copy32(const void* src,void* dst,u32 count) { std::memcpy(dst,src,count); }
void mem_copy16(const void* src,void* dst,u32 count) { std::memcpy(dst,src,count); }
void mem_swapByte(u8 value,u8* dst) { *dst=value; }
#include "production_ff.h"
void ipc_sendWordDirect(u32) {
    FsIpcService service;
    switch(sIpcCommand.cmd) {
        case FS_IPC_CMD_DLDI_READ_SECTORS: service.DldiReadSectors(&sIpcCommand); break;
        case FS_IPC_CMD_DLDI_WRITE_SECTORS: service.DldiWriteSectors(&sIpcCommand); break;
        case FS_IPC_CMD_DSI_SD_READ_SECTORS: service.DsiSdReadSectors(&sIpcCommand); break;
        case FS_IPC_CMD_DSI_SD_WRITE_SECTORS: service.DsiSdWriteSectors(&sIpcCommand); break;
        default: std::abort();
    }
}

static void formatSyntheticFat12() {
    disk.assign(1024*512,0);
    auto word=[](unsigned off,unsigned v) { disk[off]=v; disk[off+1]=v>>8; };
    disk[0]=0xEB; disk[1]=0x3C; disk[2]=0x90;
    word(11,512); disk[13]=1; word(14,1); disk[16]=1;
    word(17,32); word(19,1024); disk[21]=0xF8; word(22,3);
    std::memcpy(disk.data()+54,"FAT12   ",8); word(510,0xAA55);
    disk[512]=0xF8; disk[513]=disk[514]=0xFF;
}

int main() {
    unsigned failures=0, checks=0;
    auto check=[&](const char* name,bool ok) { ++checks; failures+=!ok; std::cout<<name<<':'<<(ok?"PASS":"FAIL")<<'\n'; };
    for(const char* volume:{"fat:","sd:"}) {
        FATFS fs{}; FIL file{}; UINT count=0;
        const std::string path=std::string(volume)+"/PROBE.SAV";
        formatSyntheticFat12(); failDriver=true;
        check("failed_mount_reaches_FRESULT",f_mount(&fs,volume,1)==FR_DISK_ERR);
        failDriver=false;
        check("mount_after_explicit_retry",f_mount(&fs,volume,1)==FR_OK);
        check("synthetic_file_create",f_open(&file,path.c_str(),FA_CREATE_ALWAYS|FA_READ|FA_WRITE)==FR_OK);
        std::vector<u8> payload(1024,0xAB), output(1024,0);
        check("file_write",f_write(&file,payload.data(),payload.size(),&count)==FR_OK && count==payload.size());
        check("sync_and_close",f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
        check("reopen",f_open(&file,path.c_str(),FA_READ|FA_WRITE)==FR_OK);
        failDriver=true; count=99;
        check("failed_read_reaches_FRESULT",f_read(&file,output.data(),output.size(),&count)==FR_DISK_ERR && count==0);
        failDriver=false; f_close(&file);
        check("reopen_after_read_error",f_open(&file,path.c_str(),FA_READ|FA_WRITE)==FR_OK);
        check("restart_readback",f_read(&file,output.data(),output.size(),&count)==FR_OK && count==payload.size() && output==payload);
        check("seek_before_write_failure",f_lseek(&file,0)==FR_OK);
        failDriver=true; count=99;
        check("failed_write_reaches_FRESULT",f_write(&file,payload.data(),payload.size(),&count)==FR_DISK_ERR && count==0);
        failDriver=false; f_close(&file);
        f_mount(nullptr,volume,0);
    }
    std::cout<<checks<<" actual FatFs checks, "<<failures<<" failures; hardware NOT RUN\n";
    return failures?1:0;
}
