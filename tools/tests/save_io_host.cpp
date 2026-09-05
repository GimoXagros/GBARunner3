// Real Save.cpp functions are included below; only FatFs/platform calls are fake.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
using UINT = unsigned; using DWORD = u32; using BYTE = u8; using FRESULT = int;
constexpr int FR_OK = 0, FR_DISK_ERR = 1, FA_OPEN_EXISTING = 0, FA_OPEN_ALWAYS = 0x10, FA_READ = 1, FA_WRITE = 2;
constexpr u32 CREATE_LINKMAP = UINT32_MAX;
struct FIL { u32* cltbl; };
#include "GbaSaveShared.h"
constexpr u32 SAVE_DATA_SIZE = 32768, SAVE_DATA_FILL = 255, DEFAULT_SAVE_SIZE = 32768;
constexpr u32 SAVE_TYPE_SRAM = 1, ISNITRO_SAVE_BUFFER_SIZE = 131072;
u8 nitro[ISNITRO_SAVE_BUFFER_SIZE];
#define ISNITRO_SAVE_BUFFER nitro
struct SaveTypeInfo { u32 size; u32 type; };
struct Environment { static bool IsIsNitroEmulator() { return false; } };
u8 gSaveData[SAVE_DATA_SIZE]; FIL gSaveFile; gba_save_shared_t gGbaSaveShared;
u32 emu_vblankIrqSkipSaveCheckInstruction = 123;
struct { bool FlushRtcStateIfDirty() { return true; } } gRomGpio;
constexpr unsigned IPC_FIFO_MSG_CHANNEL_BITS = 4, GBA_SAVE_IPC_CMD_SETUP = 0, IPC_CHANNEL_GBA_SAVE = 0;
void ipc_sendWordDirect(u32) {} bool ipc_isRecvFifoEmpty() { return false; } void ipc_recvWordDirect() {}
void vm_enableNestedIrqs() {} void vm_disableNestedIrqs() {} void dc_drainWriteBuffer() {}

std::vector<u8> disk;
u32 cursor; bool opened;
std::string fault;
unsigned writeCalls, syncCalls;
int f_open(FIL*, const char*, BYTE) { opened = fault != "open"; cursor = 0; return opened ? 0 : 1; }
u32 f_size(FIL*) { return disk.size(); }
int f_lseek(FIL*, u32 position) {
    if (fault == "seek" || (position == CREATE_LINKMAP && fault == "map")) return 1;
    if (position == CREATE_LINKMAP) return 0;
    if (position > disk.size()) disk.resize(position, 0xCC); // FatFs extension bytes are unspecified
    cursor = position; return 0;
}
int f_rewind(FIL* file) { return f_lseek(file, 0); }
int f_read(FIL*, void* destination, UINT size, UINT* read) {
    *read = 0;
    if (fault == "read") return 1;
    unsigned count = std::min<size_t>(size, disk.size() - cursor);
    if (fault == "short-read" && count) --count;
    std::memcpy(destination, disk.data() + cursor, count);
    cursor += count; *read = count; return 0;
}
int f_write(FIL*, const void* data, UINT size, UINT* written) {
    ++writeCalls; *written = 0;
    if (fault == "write" || fault == "readonly") return 1;
    if (fault == "short-write" || fault == "full") size /= 2;
    if (cursor + size > disk.size()) disk.resize(cursor + size, 0xCC);
    std::memcpy(disk.data() + cursor, data, size); cursor += size; *written = size; return 0;
}
int f_sync(FIL*) { ++syncCalls; return fault == "sync" ? 1 : 0; }
int f_close(FIL*) { if (fault == "close") return 1; opened = false; return 0; }

#include "production_save_io.h"

void reset(unsigned size = 32768) {
    disk.assign(size, 0x35); cursor = 0; opened = false; fault.clear(); writeCalls = syncCalls = 0;
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
        reset(0); fault = failure; result((std::string("create_") + failure).c_str(), !init() && !opened);
    }
    reset(); init(); gSaveData[0] = 0x79; gGbaSaveShared.saveState = GBA_SAVE_STATE_WRITE;
    sav_writePendingFiles(); f_close(&gSaveFile); init();
    result("write_close_reopen", gSaveData[0] == 0x79 && gGbaSaveShared.saveState == GBA_SAVE_STATE_CLEAN);
    for (const auto* failure : {"seek", "write", "short-write", "sync", "full", "readonly"}) {
        reset(); init(); gSaveData[0] = 0x79; gGbaSaveShared.saveState = GBA_SAVE_STATE_WRITE; fault = failure;
        sav_writePendingFiles();
        result((std::string("deferred_") + failure).c_str(), gGbaSaveShared.saveState != GBA_SAVE_STATE_CLEAN);
        unsigned syncsAfterFailure = syncCalls;
        fault.clear(); sav_writePendingFiles();
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
}
