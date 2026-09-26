// No ROM, BIOS or user save is read. The FAT12 image is generated in RAM.
// Save.cpp -> vendored FatFs -> diskio -> FsIpc -> ARM7 -> synthetic device.
#include "fatfs_foundation.h"
#include "GbaSaveShared.h"
#include "GbaSaveIpcCommand.h"
#include "Save/SaveType.h"
constexpr u32 IPC_CHANNEL_GBA_SAVE=14;
constexpr u32 SAVE_DATA_SIZE=32768, SAVE_DATA_FILL=255, DEFAULT_SAVE_SIZE=32768;
constexpr u32 ISNITRO_SAVE_BUFFER_SIZE=131072;
u8 nitro[ISNITRO_SAVE_BUFFER_SIZE];
#define ISNITRO_SAVE_BUFFER nitro
struct SaveTypeInfo { u32 size,type; };
struct Environment { static bool IsIsNitroEmulator() { return false; } };
u8 gSaveData[SAVE_DATA_SIZE]; FIL gSaveFile{}; gba_save_shared_t gGbaSaveShared{};
u32 emu_vblankIrqSkipSaveCheckInstruction=123;
volatile u8 gRomGpioRtcStateDirty;
unsigned rtcCalls;
struct { bool FlushRtcStateIfDirty() { ++rtcCalls; return true; } } gRomGpio;
bool ipc_isRecvFifoEmpty() { return false; } void ipc_recvWordDirect() {}
void vm_enableNestedIrqs() {} void vm_disableNestedIrqs() {}
struct TerminalSaveFault {};
extern "C" [[noreturn]] void sav_persistenceFault() { throw TerminalSaveFault{}; }
#include "production_save_io.h"

unsigned checks=0,failures=0;
void check(const std::string& name,bool ok) { ++checks; failures+=!ok; std::cout<<name<<':'<<(ok?"PASS":"FAIL")<<'\n'; }
template<class Action> bool terminal(Action action) {
    try { action(); } catch(const TerminalSaveFault&) { return gGbaSaveShared.saveState==GBA_SAVE_STATE_ERROR; }
    return false;
}
void resetMedia(FATFS& fs,const char* volume) {
    failDriver=false; failAtCall=UINT32_MAX; driverCalls=0; rtcCalls=0;
    formatSyntheticFat12(); sSaveFileOpen=false; gSaveFile={}; gGbaSaveShared={};
    gRomGpioRtcStateDirty=0; fs={};
    if(f_mount(&fs,volume,1)!=FR_OK) throw std::runtime_error("synthetic mount failed");
}
int main() {
    for(const char* volume:{"fat:","sd:"}) {
        FATFS fs{}; const std::string path=std::string(volume)+"/TEST.SAV";
        const std::string label=std::string(volume);
        for(u32 type:{SAVE_TYPE_SRAM_V113,SAVE_TYPE_EEPROM_V124,SAVE_TYPE_FLASH_V120,SAVE_TYPE_FLASH512_V133,SAVE_TYPE_FLASH1M_V103}) {
            const bool sram=(type&SAVE_TYPE_MASK)==SAVE_TYPE_SRAM;
            const u32 size=sram?32768:(type==SAVE_TYPE_EEPROM_V124?8192:(type==SAVE_TYPE_FLASH1M_V103?131072:65536));
            SaveTypeInfo info{size,type}; resetMedia(fs,volume);
            check(label+"create_"+std::to_string(type),sav_initializeSave(&info,path.c_str()) && f_size(&gSaveFile)==size);
            if(sram) { gSaveData[0]=0x79; gSaveData[size-1]=0x71; gGbaSaveShared.saveState=GBA_SAVE_STATE_WRITE; sav_writePendingFiles(); }
            else { sav_writeSaveByteToFile(0,0x79); sav_writeSaveByteToFile(size-1,0x71); sav_flushSaveFile(); }
            check(label+"close_"+std::to_string(type),f_close(&gSaveFile)==FR_OK);
            // Model a restart: release all file/cache state and remount the same media.
            f_mount(nullptr,volume,0); sSaveFileOpen=false; gSaveFile={}; gGbaSaveShared={}; fs={};
            check(label+"remount_"+std::to_string(type),f_mount(&fs,volume,1)==FR_OK);
            check(label+"round_trip_"+std::to_string(type),sav_initializeSave(&info,path.c_str()) && sav_readSaveByteFromFile(0)==0x79 && sav_readSaveByteFromFile(size-1)==0x71);
            f_close(&gSaveFile); f_mount(nullptr,volume,0);
        }
        // Real truncation/extension: preserve the known prefix and fill only the tail.
        resetMedia(fs,volume);
        FIL seed{}; UINT count=0; std::vector<u8> prefix(11,0x35);
        if(f_open(&seed,path.c_str(),FA_CREATE_ALWAYS|FA_READ|FA_WRITE)!=FR_OK ||
           f_write(&seed,prefix.data(),prefix.size(),&count)!=FR_OK || count!=prefix.size() ||
           f_close(&seed)!=FR_OK) throw std::runtime_error("seed creation failed");
        check(label+"short_file_extension",sav_initializeSave(nullptr,path.c_str()) && f_size(&gSaveFile)==32768 &&
            std::all_of(gSaveData,gSaveData+11,[](u8 b){return b==0x35;}) &&
            std::all_of(gSaveData+11,gSaveData+32768,[](u8 b){return b==255;}));
        f_close(&gSaveFile); f_mount(nullptr,volume,0);

        // Full synthetic media produces a genuine FR_OK short write, not a mocked API result.
        resetMedia(fs,volume);
        const std::string filler=std::string(volume)+"/FILL.BIN";
        if(f_open(&seed,filler.c_str(),FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK) throw std::runtime_error("filler creation failed");
        std::vector<u8> block(8192,0xAB); bool full=false;
        for(unsigned i=0;i<128 && !full;++i) {
            if(f_write(&seed,block.data(),block.size(),&count)!=FR_OK) throw std::runtime_error("unexpected filler disk error");
            full=count<block.size();
        }
        if(!full || f_close(&seed)!=FR_OK) throw std::runtime_error("failed to fill synthetic media");
        check(label+"full_disk_initialization_rejected",!sav_initializeSave(nullptr,path.c_str()) && sSaveFileOpen && f_size(&gSaveFile)<32768 && rtcCalls==0);
        f_mount(nullptr,volume,0);

        for(const char* operation:{"read","write","deferred","sync"}) {
            resetMedia(fs,volume);
            if(!sav_initializeSave(nullptr,path.c_str())) throw std::runtime_error("synthetic save initialization failed");
            // Flush an ordinary byte into FatFs' partial-sector buffer before failing sync.
            if(std::string(operation)=="sync") sav_writeSaveByteToFile(1024,0x79);
            gSaveData[0]=0x79; gGbaSaveShared.saveState=GBA_SAVE_STATE_WRITE; gRomGpioRtcStateDirty=1;
            const auto mediaBefore=disk; const unsigned callsBefore=driverCalls; failDriver=true;
            bool continued=false;
            const bool stopped=terminal([&]{
                if(std::string(operation)=="read") (void)sav_readSaveByteFromFile(1024);
                else if(std::string(operation)=="write") sav_writeSaveByteToFile(1024,0x79);
                else if(std::string(operation)=="sync") sav_flushSaveFile();
                else sav_writePendingFiles();
                continued=true;
            });
            check(label+"driver_"+operation+"_terminal",stopped && !continued && driverCalls>callsBefore && rtcCalls==0 && disk==mediaBefore);
            const unsigned after=driverCalls; failDriver=false;
            check(label+"driver_"+operation+"_latched",terminal([]{sav_writePendingFiles();}) && after==driverCalls && rtcCalls==0);
            f_mount(nullptr,volume,0);
        }
        resetMedia(fs,volume); failDriver=true; const unsigned before=driverCalls;
        check(label+"initialization_driver_failure",!sav_initializeSave(nullptr,path.c_str()) && driverCalls>before && rtcCalls==0);
        failDriver=false; f_mount(nullptr,volume,0);
    }
    std::cout<<checks<<" production Save/FatFs/transport checks, "<<failures<<" failures; hardware NOT RUN\n";
    return failures?1:0;
}
