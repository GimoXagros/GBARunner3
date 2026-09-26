// Real Save.cpp functions are included below; only FatFs/platform calls are fake.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <map>
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
using UINT = unsigned; using DWORD = u32; using BYTE = u8; using FRESULT = int;
constexpr int FR_OK = 0, FR_DISK_ERR = 1, FA_OPEN_EXISTING = 0, FA_OPEN_ALWAYS = 0x10, FA_READ = 1, FA_WRITE = 2;
constexpr u32 CREATE_LINKMAP = UINT32_MAX;
struct FIL { u32* cltbl; int err; };
#include "GbaSaveShared.h"
constexpr u32 SAVE_DATA_SIZE = 32768, SAVE_DATA_FILL = 255, DEFAULT_SAVE_SIZE = 32768;
#include "../../code/core/arm9/source/Save/SaveType.h"
constexpr u32 ISNITRO_SAVE_BUFFER_SIZE = 131072;
u8 nitro[ISNITRO_SAVE_BUFFER_SIZE];
#define ISNITRO_SAVE_BUFFER nitro
struct SaveTypeInfo { u32 size; u32 type; };
struct Environment { static bool IsIsNitroEmulator() { return nitroMode; } static bool nitroMode; };
bool Environment::nitroMode = false;
u8 gSaveData[SAVE_DATA_SIZE]; FIL gSaveFile; gba_save_shared_t gGbaSaveShared;
u32 emu_vblankIrqSkipSaveCheckInstruction = 123;
volatile u8 gRomGpioRtcStateDirty;
unsigned rtcCalls;
struct { bool FlushRtcStateIfDirty() { ++rtcCalls; return true; } } gRomGpio;
#include "GbaSaveIpcCommand.h"
#include "IpcChannels.h"
constexpr unsigned IPC_FIFO_MSG_CHANNEL_BITS = 4;
void ipc_sendWordDirect(u32) {} bool ipc_isRecvFifoEmpty() { return false; } void ipc_recvWordDirect() {}
u32 arm_disableIrqs() { return 0; }
void vm_enableNestedIrqs() {} void vm_disableNestedIrqs() {} void dc_drainWriteBuffer() {}

std::vector<u8> disk;
u32 cursor; bool opened;
std::string fault;
unsigned writeCalls, syncCalls, openCalls, seekCalls, closeCalls;
std::map<std::string,unsigned> failAt, calls;
bool fails(const std::string& op) {
    const unsigned n = ++calls[op];
    return fault == op || fault.find(op + "+") == 0 || fault.find("+" + op) != std::string::npos ||
        (failAt.count(op) && failAt[op] == n);
}
int f_open(FIL* file, const char*, BYTE) {
    ++openCalls; opened = !fails("open"); cursor = 0; file->err = 0; file->cltbl = nullptr;
    return opened ? 0 : 1;
}
u32 f_size(FIL*) { return disk.size(); }
int f_lseek(FIL* file, u32 position) {
    ++seekCalls;
    if (file->err) return file->err;
    if (fails("seek")) return file->err = 1;
    if (position == CREATE_LINKMAP) return fails("map") ? 1 : 0;
    if (position > disk.size()) disk.resize(position, 0xCC);
    cursor = position; return 0;
}
int f_rewind(FIL* file) { return f_lseek(file, 0); }
int f_read(FIL* file, void* destination, UINT size, UINT* read) {
    *read = 0;
    if (file->err) return file->err;
    if (fails("read")) return file->err = 1;
    unsigned count = std::min<size_t>(size, disk.size() - cursor);
    if (fails("short-read") && count) --count;
    std::memcpy(destination, disk.data() + cursor, count);
    cursor += count; *read = count; return 0;
}
int f_write(FIL* file, const void* data, UINT size, UINT* written) {
    ++writeCalls; *written = 0;
    if (file->err) return file->err;
    if (fails("write") || fails("readonly")) return file->err = 1;
    if (fails("short-write") || fails("full")) size /= 2;
    if (cursor + size > disk.size()) disk.resize(cursor + size, 0xCC);
    std::memcpy(disk.data() + cursor, data, size); cursor += size; *written = size; return 0;
}
int f_sync(FIL*) { ++syncCalls; return fails("sync") ? 1 : 0; }
int f_close(FIL* file) {
    ++closeCalls;
    if (fails("close") || f_sync(file) != 0) return 1;
    opened = false; return 0;
}

struct TerminalSaveFault {};
extern "C" [[noreturn]] void sav_persistenceFault() { throw TerminalSaveFault{}; }
#include "production_save_io.h"
#include "../../code/core/arm9/source/Save/SaveFaultScreen.h"

// Only expose private members to attach shared memory in the 64-bit host.
// All Update/Flush/exit behavior below is extracted from actual ARM7 source.
#define class struct
#include "GbaSaveIpcService.h"
#undef class
int volume;
void snd_setMasterVolume(int value) { volume = value; }
bool isDSiMode() { return false; }
#include "production_arm7_save.h"
enum class Arm7State { Idle, ExitRequested };
Arm7State sState;
bool sSaveFaultSeen=false;
enum class ExitMode { Reset, PowerOff };
ExitMode sExitMode=ExitMode::Reset;
int exits;
GbaSaveIpcService sGbaSaveIpcService;
void performExit(ExitMode) { ++exits; }
#include "production_exit.h"

void reset(unsigned size = 32768) {
    disk.assign(size, 0x35); cursor = 0; opened = false; fault.clear();
    writeCalls = syncCalls = openCalls = seekCalls = closeCalls = rtcCalls = 0;
    failAt.clear(); calls.clear(); sSaveFileOpen = false; gSaveFile = {}; Environment::nitroMode = false;
    gGbaSaveShared = {}; emu_vblankIrqSkipSaveCheckInstruction = 123; gRomGpioRtcStateDirty=0;
}
unsigned checks=0, failures=0;
void result(const std::string& scenario, bool ok) { ++checks; failures+=!ok; std::cout << scenario << ':' << (ok ? "PASS" : "FAIL") << '\n'; }
bool init() { return sav_initializeSave(nullptr, "synthetic.sav"); }
template<class Action> bool terminal(Action action) {
    try { action(); } catch(const TerminalSaveFault&) { return gGbaSaveShared.saveState==GBA_SAVE_STATE_ERROR; }
    return false;
}
int main() {
    for (const auto type : {SAVE_TYPE_EEPROM_V124, SAVE_TYPE_FLASH_V120,
            SAVE_TYPE_FLASH512_V133, SAVE_TYPE_FLASH1M_V103, SAVE_TYPE_SRAM_V113}) {
        const bool sram = (type & SAVE_TYPE_MASK) == SAVE_TYPE_SRAM;
        const u32 size = sram ? 32768 : (type == SAVE_TYPE_EEPROM_V124 ? 8192 :
            (type == SAVE_TYPE_FLASH1M_V103 ? 131072 : 65536));
        reset(size); SaveTypeInfo info{size, type};
        result("production_save_type_"+std::to_string(type),
            sav_initializeSave(&info,"synthetic.sav") && opened &&
            gGbaSaveShared.saveDataSize == (sram ? size : 0) &&
            (gGbaSaveShared.saveData != nullptr) == sram);
        if(sram) { gSaveData[0]=0x79; gSaveData[size-1]=0x71; gGbaSaveShared.saveState=GBA_SAVE_STATE_WRITE; sav_writePendingFiles(); }
        else { sav_writeSaveByteToFile(0,0x79); sav_writeSaveByteToFile(size-1,0x71); sav_flushSaveFile(); }
        const bool closed=f_close(&gSaveFile)==FR_OK; sSaveFileOpen=false; gSaveFile={};
        result("save_type_round_trip_"+std::to_string(type), closed && sav_initializeSave(&info,"synthetic.sav") &&
            sav_readSaveByteFromFile(0)==0x79 && sav_readSaveByteFromFile(size-1)==0x71);
    }
    reset(0); result("normal_create", init() && disk.size() == 32768 && std::all_of(disk.begin(), disk.end(), [](u8 v){return v == 255;}));
    reset(10); bool initialized=init();
    result("short_file_extension", initialized && disk.size()==32768 && std::all_of(disk.begin(),disk.begin()+10,[](u8 v){return v==0x35;}) && std::all_of(disk.begin()+10,disk.end(),[](u8 v){return v==255;}));
    reset(65536); result("oversized_existing_preserved", init() && disk.size()==65536 && disk.back()==0x35);
    for(const auto* failure:{"open","seek","map","read","short-read"}) {
        reset(); fault=failure;
        result(std::string("initialize_")+failure,!init() && closeCalls==0);
        const unsigned opens=openCalls;
        if(opened) result(std::string("initialize_no_reopen_")+failure,!init() && opens==openCalls && closeCalls==0);
    }
    for(const auto* failure:{"write","short-write","sync","full","readonly"}) {
        reset(0); fault=failure;
        result(std::string("create_")+failure,!init() && closeCalls==0);
    }
    reset(); failAt["seek"]=2; result("initialize_rewind_failure",!init() && closeCalls==0);
    reset(); init(); auto before=disk; const unsigned opens=openCalls;
    result("live_file_reinitialization_rejected",!init() && openCalls==opens && closeCalls==0 && disk==before);
    for(const auto* failure:{"seek","write","short-write","sync","full","readonly"}) {
        reset(); init(); gSaveData[0]=0x79; gGbaSaveShared.saveState=GBA_SAVE_STATE_WRITE;
        gRomGpioRtcStateDirty=1; fault=failure;
        result(std::string("deferred_terminal_")+failure,terminal([]{sav_writePendingFiles();}) && gSaveData[0]==0x79 && rtcCalls==0);
        const unsigned writes=writeCalls,syncs=syncCalls,seeks=seekCalls;
        fault.clear();
        result(std::string("latched_no_later_io_")+failure,terminal([]{sav_writePendingFiles();}) && writeCalls==writes && syncCalls==syncs && seekCalls==seeks && rtcCalls==0);
    }
    for(const auto* failure:{"seek","read","short-read"}) {
        reset(); init(); fault=failure;
        result(std::string("byte_read_terminal_")+failure,terminal([]{sav_readSaveByteFromFile(0);}) && writeCalls==0);
    }
    for(const auto* failure:{"seek","write","short-write","full","readonly"}) {
        reset(); init(); fault=failure; before=disk;
        result(std::string("byte_write_terminal_")+failure,terminal([]{sav_writeSaveByteToFile(0,0x79);}) && disk==before);
        result(std::string("byte_fault_no_followup_sync_")+failure,syncCalls==0 && rtcCalls==0);
    }
    reset(); init(); result("byte_read_out_of_range",sav_readSaveByteFromFile(40000)==255 && disk.size()==32768);
    reset(); init(); before=disk;
    result("byte_write_out_of_range",terminal([]{sav_writeSaveByteToFile(40000,0x79);}) && disk==before);
    reset(); init(); fault="sync";
    result("flush_failure_terminal",terminal([]{sav_flushSaveFile();}));
    reset(); init(); gGbaSaveShared.saveDataSize=SAVE_DATA_SIZE+1; gGbaSaveShared.saveState=GBA_SAVE_STATE_WRITE;
    result("oversized_buffer_rejected",terminal([]{sav_writePendingFiles();}) && writeCalls==0 && rtcCalls==0);
    reset(); Environment::nitroMode=true; SaveTypeInfo tooLarge{ISNITRO_SAVE_BUFFER_SIZE+1,0};
    result("nitro_initialization_bounds",!sav_initializeSave(&tooLarge,"synthetic.sav") && openCalls==0);
    reset(); Environment::nitroMode=true; nitro[0]=0x35; nitro[ISNITRO_SAVE_BUFFER_SIZE-1]=0x71;
    result("nitro_read_bounds",sav_readSaveByteFromFile(ISNITRO_SAVE_BUFFER_SIZE)==255);
    result("nitro_write_bounds",terminal([]{sav_writeSaveByteToFile(ISNITRO_SAVE_BUFFER_SIZE,0x79);}) && nitro[0]==0x35 && nitro[ISNITRO_SAVE_BUFFER_SIZE-1]==0x71);
    reset(); init(); sGbaSaveIpcService._saveShared=&gGbaSaveShared;
    result("arm7_clean_ack",sGbaSaveIpcService.FlushSaveIfDirty()==SaveFlushResult::Clean);
    gGbaSaveShared.saveState=GBA_SAVE_STATE_DIRTY; sGbaSaveIpcService.Update();
    for(unsigned i=0;i<10;++i) sGbaSaveIpcService.Update();
    result("arm7_normal_debounce",gGbaSaveShared.saveState==GBA_SAVE_STATE_WRITE && sGbaSaveIpcService.FlushSaveIfDirty()==SaveFlushResult::Pending);
    for(unsigned size:{0u,32768u}) {
        gGbaSaveShared.saveDataSize=size; gGbaSaveShared.saveState=GBA_SAVE_STATE_ERROR; volume=127;
        for(unsigned i=0;i<120;++i) sGbaSaveIpcService.Update();
        result("arm7_terminal_error_ack_"+std::to_string(size),gGbaSaveShared.saveState==GBA_SAVE_STATE_ERROR && sGbaSaveIpcService.FlushSaveIfDirty()==SaveFlushResult::Error);
        sState=Arm7State::ExitRequested; exits=0; updateArm7ExitRequestedState();
        result("exit_stays_muted_on_error_"+std::to_string(size),exits==0 && volume==0 && gGbaSaveShared.saveState==GBA_SAVE_STATE_ERROR);
    }
    gGbaSaveShared.saveDataSize=32768; gGbaSaveShared.saveState=GBA_SAVE_STATE_WAIT; sState=Arm7State::ExitRequested; exits=0;
    updateArm7ExitRequestedState();
    result("exit_waits_for_pending",exits==0 && sState==Arm7State::ExitRequested && gGbaSaveShared.saveState==GBA_SAVE_STATE_WRITE);
    gGbaSaveShared.saveState=GBA_SAVE_STATE_CLEAN; updateArm7ExitRequestedState();
    result("clean_exit",exits==1);
    sGbaSaveIpcService._saveShared=nullptr;
    result("arm7_unconfigured_clean",sGbaSaveIpcService.FlushSaveIfDirty()==SaveFlushResult::Clean);
    for(unsigned failedChunk:{2u,3u}) {
        reset(11); SaveTypeInfo large{131072,SAVE_TYPE_FLASH1M_V103}; failAt["write"]=failedChunk;
        result("multi_chunk_fill_terminal_"+std::to_string(failedChunk),!sav_initializeSave(&large,"synthetic.sav") && disk.size()<131072 && closeCalls==0 && syncCalls==0);
    }
    std::vector<u16> screen(256*192+2,0xA55A);
    sav_renderPersistenceFaultScreen(screen.data()+1);
    result("terminal_screen_bounds",screen.front()==0xA55A && screen.back()==0xA55A);
    result("terminal_screen_visible_text",std::count(screen.begin()+1,screen.end()-1,screen[1])<256*192);
    gGbaSaveShared.saveState=GBA_SAVE_STATE_ERROR; sGbaSaveIpcService._saveShared=&gGbaSaveShared;
    sExitMode=ExitMode::PowerOff; exits=0; sSaveFaultSeen=false; updateArm7ExitRequestedState();
    result("pending_poweroff_canceled_on_first_fault",exits==0 && sSaveFaultSeen && volume==0);
    updateArm7ExitRequestedState();
    result("explicit_poweroff_after_error",exits==1 && volume==0 && gGbaSaveShared.saveState==GBA_SAVE_STATE_ERROR);
    return failures?1:0;
}
