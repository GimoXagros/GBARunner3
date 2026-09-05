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
constexpr u32 SAVE_TYPE_SRAM = 1, ISNITRO_SAVE_BUFFER_SIZE = 131072;
u8 nitro[ISNITRO_SAVE_BUFFER_SIZE];
#define ISNITRO_SAVE_BUFFER nitro
struct SaveTypeInfo { u32 size; u32 type; };
struct Environment { static bool IsIsNitroEmulator() { return nitroMode; } static bool nitroMode; };
bool Environment::nitroMode = false;
u8 gSaveData[SAVE_DATA_SIZE]; FIL gSaveFile; gba_save_shared_t gGbaSaveShared;
u32 emu_vblankIrqSkipSaveCheckInstruction = 123;
struct { bool FlushRtcStateIfDirty() { return true; } } gRomGpio;
#include "GbaSaveIpcCommand.h"
#include "IpcChannels.h"
constexpr unsigned IPC_FIFO_MSG_CHANNEL_BITS = 4;
void ipc_sendWordDirect(u32) {} bool ipc_isRecvFifoEmpty() { return false; } void ipc_recvWordDirect() {}
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

#include "production_save_io.h"

// Only expose private members to attach shared memory in the 64-bit host.
// All Update/Flush/exit behavior below is extracted from actual ARM7 source.
#define class struct
#include "GbaSaveIpcService.h"
#undef class
bool isDSiMode() { return false; }
#include "production_arm7_save.h"
enum class Arm7State { Idle, ExitRequested };
Arm7State sState;
int sExitMode, exits, volume;
GbaSaveIpcService sGbaSaveIpcService;
void performExit(int) { ++exits; }
void snd_setMasterVolume(int value) { volume = value; }
#include "production_exit.h"

void reset(unsigned size = 32768) {
    disk.assign(size, 0x35); cursor = 0; opened = false; fault.clear(); writeCalls = syncCalls = openCalls = seekCalls = closeCalls = 0;
    failAt.clear(); calls.clear(); sSaveFileOpen = sByteWriteFailed = false; gSaveFile = {}; Environment::nitroMode = false;
    gGbaSaveShared = {}; emu_vblankIrqSkipSaveCheckInstruction = 123;
}
void result(const char* scenario, bool ok) { std::cout << scenario << ':' << (ok ? "PASS" : "FAIL") << '\n'; }
bool init() { return sav_initializeSave(nullptr, "synthetic.sav"); }
int main() {
    reset(0); result("normal_create", init() && disk.size() == 32768 && std::all_of(disk.begin(), disk.end(), [](u8 v){return v == 255;}));
    reset(10); bool initialized = init();
    result("short_file_extension", initialized && std::all_of(disk.begin(), disk.begin()+10, [](u8 v){return v == 0x35;}) && std::all_of(disk.begin()+10,disk.end(),[](u8 v){return v==255;}));
    reset(65536); result("oversized_existing_preserved", init() && disk.size() == 65536 && disk.back() == 0x35);
    for (const auto* failure : {"open", "seek", "map", "read", "short-read"}) {
        reset(); fault = failure; result((std::string("initialize_") + failure).c_str(), !init() && !opened);
    }
    for (const auto* failure : {"write", "short-write", "sync", "full", "readonly"}) {
        reset(0); fault = failure; result((std::string("create_") + failure).c_str(), !init() && (std::string(failure) == "sync" ? opened && sSaveFileOpen : !opened));
    }
    reset(); init(); gSaveData[0] = 0x79; gGbaSaveShared.saveState = GBA_SAVE_STATE_WRITE;
    sav_writePendingFiles(); closeSaveFile(); init();
    result("write_close_reopen", gSaveData[0] == 0x79 && gGbaSaveShared.saveState == GBA_SAVE_STATE_CLEAN);
    for (const auto* failure : {"seek", "write", "short-write", "sync", "full", "readonly"}) {
        reset(); init(); gSaveData[0] = 0x79; gGbaSaveShared.saveState = GBA_SAVE_STATE_WRITE; fault = failure;
        sav_writePendingFiles();
        result((std::string("deferred_") + failure).c_str(), gGbaSaveShared.saveState != GBA_SAVE_STATE_CLEAN);
        unsigned syncsAfterFailure = syncCalls;
        fault.clear(); sav_retryFailedWrite("synthetic.sav");
        result((std::string("retry_") + failure).c_str(), disk[0] == 0x79 && syncCalls > syncsAfterFailure && gGbaSaveShared.saveState == GBA_SAVE_STATE_CLEAN);
    }
    for (const auto* failure : {"read", "short-read"}) {
        reset(); init(); fault = failure;
        result((std::string("byte_") + failure).c_str(), sav_readSaveByteFromFile(0) == 255);
    }
    reset(); init(); result("byte_read_out_of_range", sav_readSaveByteFromFile(40000) == 255 && disk.size() == 32768);
    reset(); init(); cursor = 1; disk[1] = 0x71; fault = "seek";
    result("byte_read_failed_seek", sav_readSaveByteFromFile(0) == 255);
    reset(); init(); cursor = 1; fault = "seek"; sav_writeSaveByteToFile(0, 0x79);
    result("byte_write_failed_seek_preserves_file", disk[1] == 0x35);
    reset(); init(); sav_writeSaveByteToFile(40000, 0x79);
    result("byte_write_out_of_range", disk.size() == 32768);
    reset(0); fault = "write"; bool failed = !init(); fault.clear(); initialized = init();
    result("interrupted_initialization_retry_fill", failed && initialized && std::all_of(disk.begin(),disk.end(),[](u8 v){return v==255;}));
    reset(); init(); fault = "sync"; sav_flushSaveFile();
    result("flush_failure_visible", gGbaSaveShared.saveState != GBA_SAVE_STATE_CLEAN);
    reset(); init(); fault = "close";
    result("close_failure_observable", f_close(&gSaveFile) != FR_OK && opened);
    reset(); init(); gGbaSaveShared.saveState = GBA_SAVE_STATE_WRITE; fault = "write";
    for (unsigned i = 0; i < 120; ++i) sav_writePendingFiles();
    result("no_unbounded_vblank_retry", writeCalls <= 1);
    result("terminal_error_preserved", gGbaSaveShared.saveState == GBA_SAVE_STATE_ERROR);

    for (const auto* failure : {"seek", "write", "short-write", "sync", "full", "readonly"}) {
        reset(11); fault = failure; const bool failedInit = !init(); fault.clear();
        result((std::string("initialize_retry_") + failure).c_str(), failedInit && init() &&
            std::all_of(disk.begin(), disk.begin()+11, [](u8 b){return b==0x35;}) &&
            std::all_of(disk.begin()+11, disk.end(), [](u8 b){return b==255;}));
    }
    reset(); fault = "read+close"; initialized = init();
    unsigned opens = openCalls; auto* table = gSaveFile.cltbl;
    bool failedAgain = !init();
    result("cleanup_close_keeps_live_object", !initialized && failedAgain && opened && sSaveFileOpen && openCalls == opens && gSaveFile.cltbl == table);
    fault.clear(); result("cleanup_close_retry_then_reopen", init() && openCalls == opens + 1);
    reset(); failAt["seek"] = 2;
    result("initialize_rewind_failure", !init());
    reset(11); failAt["map"] = 1;
    result("map_failure_after_fill_retry", !init() && init() && disk[10] == 0x35 && disk[11] == 255 && disk.back() == 255);
    reset(); init(); gSaveData[0] = 0x79; gGbaSaveShared.saveState = GBA_SAVE_STATE_WRITE; fault = "write"; sav_writePendingFiles();
    fault = "close"; opens = openCalls;
    result("retry_close_failure_preserves_ram", !sav_retryFailedWrite("synthetic.sav") && opened && gSaveData[0] == 0x79 && openCalls == opens && gGbaSaveShared.saveState == GBA_SAVE_STATE_ERROR);
    fault = "open"; result("retry_open_failure_preserves_ram", !sav_retryFailedWrite("synthetic.sav") && gSaveData[0] == 0x79 && gGbaSaveShared.saveState == GBA_SAVE_STATE_ERROR);
    fault.clear(); result("retry_after_failed_open", sav_retryFailedWrite("synthetic.sav") && disk[0] == 0x79);
    for (const auto* failure : {"map", "write", "short-write", "sync", "readonly"}) {
        reset(); init(); gSaveData[0] = 0x79; gGbaSaveShared.saveState = GBA_SAVE_STATE_WRITE; fault = "write"; sav_writePendingFiles();
        fault = failure;
        result((std::string("retry_fails_") + failure).c_str(), !sav_retryFailedWrite("synthetic.sav") && gSaveData[0] == 0x79 && gGbaSaveShared.saveState == GBA_SAVE_STATE_ERROR);
    }
    reset(); init(); gSaveData[0] = 0x79; gGbaSaveShared.saveState = GBA_SAVE_STATE_WRITE; fault="write"; sav_writePendingFiles(); fault.clear(); disk.resize(10);
    result("retry_rejects_short_existing", !sav_retryFailedWrite("synthetic.sav") && disk.size()==10 && gSaveData[0]==0x79);
    reset(); init(); gGbaSaveShared.saveDataSize=SAVE_DATA_SIZE+1; gGbaSaveShared.saveState=GBA_SAVE_STATE_WRITE; sav_writePendingFiles();
    result("oversized_buffer_rejected", gGbaSaveShared.saveState==GBA_SAVE_STATE_ERROR && writeCalls==0);
    reset(); init(); const auto before=disk; sav_writeSaveByteToFile(40000,0x79); sav_flushSaveFile();
    result("byte_failure_survives_successful_flush", disk==before && gGbaSaveShared.saveState==GBA_SAVE_STATE_ERROR);
    reset(); init(); gGbaSaveShared.saveDataSize=0; gGbaSaveShared.saveState=GBA_SAVE_STATE_ERROR;
    result("file_backed_retry_rejected", !sav_retryFailedWrite("synthetic.sav"));
    reset(); Environment::nitroMode=true; SaveTypeInfo tooLarge{ISNITRO_SAVE_BUFFER_SIZE+1,0};
    result("nitro_initialization_bounds", !sav_initializeSave(&tooLarge,"synthetic.sav") && openCalls==0);
    reset(); Environment::nitroMode=true; nitro[0]=0x35; nitro[ISNITRO_SAVE_BUFFER_SIZE-1]=0x71;
    sav_writeSaveByteToFile(ISNITRO_SAVE_BUFFER_SIZE,0x79);
    result("nitro_byte_bounds", sav_readSaveByteFromFile(ISNITRO_SAVE_BUFFER_SIZE)==255 && nitro[0]==0x35 && nitro[ISNITRO_SAVE_BUFFER_SIZE-1]==0x71 && gGbaSaveShared.saveState==GBA_SAVE_STATE_ERROR);

    reset(); init(); sGbaSaveIpcService._saveShared=&gGbaSaveShared;
    result("arm7_clean_ack", sGbaSaveIpcService.FlushSaveIfDirty()==SaveFlushResult::Clean);
    gGbaSaveShared.saveState=GBA_SAVE_STATE_DIRTY; sGbaSaveIpcService.Update();
    for(unsigned i=0;i<10;++i) sGbaSaveIpcService.Update();
    result("arm7_normal_debounce", gGbaSaveShared.saveState==GBA_SAVE_STATE_WRITE && sGbaSaveIpcService.FlushSaveIfDirty()==SaveFlushResult::Pending);
    gSaveData[0]=0x79; fault="write"; sav_writePendingFiles();
    for(unsigned i=0;i<120;++i) sGbaSaveIpcService.Update();
    result("arm7_terminal_error_ack", gGbaSaveShared.saveState==GBA_SAVE_STATE_ERROR && sGbaSaveIpcService.FlushSaveIfDirty()==SaveFlushResult::Error && writeCalls==1);
    sState=Arm7State::ExitRequested; volume=0; exits=0; updateArm7ExitRequestedState();
    result("exit_cancelled_on_error", sState==Arm7State::Idle && volume==127 && exits==0 && gGbaSaveShared.saveState==GBA_SAVE_STATE_ERROR);
    fault.clear(); bool recovered=sav_retryFailedWrite("synthetic.sav");
    sState=Arm7State::ExitRequested; updateArm7ExitRequestedState();
    result("explicit_retry_then_clean_exit", recovered && exits==1 && disk[0]==0x79);
    gGbaSaveShared.saveState=GBA_SAVE_STATE_WAIT; sState=Arm7State::ExitRequested; exits=0; updateArm7ExitRequestedState();
    result("exit_waits_for_pending", exits==0 && sState==Arm7State::ExitRequested && gGbaSaveShared.saveState==GBA_SAVE_STATE_WRITE);
    gGbaSaveShared.saveDataSize=0; gGbaSaveShared.saveState=GBA_SAVE_STATE_ERROR;
    result("arm7_file_backed_error_ack", sGbaSaveIpcService.FlushSaveIfDirty()==SaveFlushResult::Error);
    sState=Arm7State::ExitRequested; updateArm7ExitRequestedState();
    result("file_backed_error_cancels_exit", sState==Arm7State::Idle && exits==0);
    sGbaSaveIpcService._saveShared=nullptr;
    result("arm7_unconfigured_clean", sGbaSaveIpcService.FlushSaveIfDirty()==SaveFlushResult::Clean);
}
