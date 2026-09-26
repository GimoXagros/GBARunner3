#include "common.h"
#include <libtwl/ipc/ipcFifo.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include <algorithm>
#include <string.h>
#include "Fat/ff.h"
#include "Core/Environment.h"
#include "MemFastSearch.h"
#include "SaveSwi.h"
#include "SaveTypeInfo.h"
#include "cp15.h"
#include "Cpsr.h"
#include "Peripherals/RomGpio/RomGpio.h"
#include "VirtualMachine/VMNestedIrq.h"
#include "MemoryEmulator/RomDefs.h"
#include "SdCache/SdCache.h"
#include "IpcChannels.h"
#include "GbaSaveIpcCommand.h"
#include "Save.h"
#include "SaveSignatureSearch.h"
#include "SaveFault.h"

#define DEFAULT_SAVE_SIZE   (32 * 1024)

[[gnu::section(".ewram.bss")]]
u8 gSaveData[SAVE_DATA_SIZE] alignas(32);

[[gnu::section(".ewram.bss")]]
FIL gSaveFile alignas(32);

[[gnu::section(".ewram.bss"), gnu::aligned(32)]]
gba_save_shared_t gGbaSaveShared;

static DWORD sClusterTable[64];
static u32 sSkipSaveCheckInstruction;
[[gnu::section(".ewram.bss")]] static bool sSaveFileOpen;
// No error path returns to the guest or performs further filesystem I/O.
[[gnu::noreturn]] static void saveIoFailed()
{
    arm_disableIrqs();
    gGbaSaveShared.saveState = GBA_SAVE_STATE_ERROR;
    dc_drainWriteBuffer();
    sav_persistenceFault();
}

[[gnu::section(".ewram")]] void sav_initializeFileWriteScheduler(void)
{
    sSkipSaveCheckInstruction = emu_vblankIrqSkipSaveCheckInstruction;
}

[[gnu::section(".ewram")]] void sav_requestFileWrite(void)
{
    dc_drainWriteBuffer();
    emu_vblankIrqSkipSaveCheckInstruction = 0xE1A00000; // nop
}

// temporarily
extern FIL gFile;

#ifdef GBAR3_HICODE_CACHE_MAPPING

[[gnu::section(".ewram"), gnu::noinline]]
static u32* searchHiCode(const u32* signature, u32 romStart, u32 romEnd)
{
    // The runtime cache accessor is fail-closed: an I/O error cannot be
    // mistaken for a clean signature miss and proceed with unpatched saves.
    const u32 address = sav_findSignature16(signature, romStart, romEnd,
        sdc_getRomBlock, mem_fastSearch16);
    return address == UINT32_MAX ? nullptr : (u32*)sdc_loadRomBlockForPatching(address);
}

#endif

bool sav_tryPatchFunction(const u32* signature, u32 saveSwiNumber, void* patchFunction)
{
    u32* function = (u32*)mem_fastSearch16((const u32*)ROM_LINEAR_DS_ADDRESS, ROM_LINEAR_SIZE, signature);
#ifdef GBAR3_HICODE_CACHE_MAPPING
    if (!function && ROM_LINEAR_GBA_ADDRESS > 0x08000000)
    {
        function = searchHiCode(signature, 0x08000000, ROM_LINEAR_GBA_ADDRESS);
    }
    if (!function)
    {
        u32 romSize = f_size(&gFile);
        if (romSize >= ROM_LINEAR_SIZE + 4)
        {
            // The final 12 linear bytes and first cached block form one
            // logical 4 KiB boundary. Keep the tail before resolving cache.
            alignas(4) u8 linearTail[12];
            memcpy(linearTail, (const void*)(ROM_LINEAR_END_DS_ADDRESS - 12), 12);
            const auto* next = (const u8*)sdc_getRomBlock(ROM_LINEAR_END_GBA_ADDRESS);
            const u32 match = sav_findSplitBoundary16(signature, ROM_LINEAR_END_GBA_ADDRESS,
                linearTail, next, std::min<u32>(12, romSize - ROM_LINEAR_SIZE));
            if (match != UINT32_MAX)
                function = (u32*)(ROM_LINEAR_DS_ADDRESS + match - ROM_LINEAR_GBA_ADDRESS);
        }
        if (!function)
            function = searchHiCode(signature, ROM_LINEAR_END_GBA_ADDRESS, 0x08000000 + romSize);
    }
#endif
    if (!function)
    {
        return false;
    }

    sav_swiTable[saveSwiNumber] = patchFunction;
    *(u16*)function = SAVE_THUMB_SWI(saveSwiNumber);
    return true;
}

static bool loadSaveClusterMap(void)
{
    sClusterTable[0] = sizeof(sClusterTable) / sizeof(DWORD);
    gSaveFile.cltbl = sClusterTable;
    return f_lseek(&gSaveFile, CREATE_LINKMAP) == FR_OK;
}

static bool fillSaveFile(u32 start, u32 end)
{
    if (f_lseek(&gSaveFile, start) != FR_OK)
        return false;

    while (start < end)
    {
        const UINT writeSize = std::min<u32>(end - start, SAVE_DATA_SIZE);
        UINT written = 0;
        if (f_write(&gSaveFile, gSaveData, writeSize, &written) != FR_OK || written != writeSize)
            return false;
        start += written;
    }
    return f_sync(&gSaveFile) == FR_OK;
}

bool sav_initializeSave(const SaveTypeInfo* saveTypeInfo, const char* savePath)
{
    // Initialization is boot-only. Never overwrite or flush a live failed object.
    if (sSaveFileOpen) return false;
    u32 saveSize = saveTypeInfo ? saveTypeInfo->size : DEFAULT_SAVE_SIZE;
    if (Environment::IsIsNitroEmulator() && saveSize > ISNITRO_SAVE_BUFFER_SIZE)
        return false;
    const bool isSram = !saveTypeInfo ||
        (saveTypeInfo->type & SAVE_TYPE_MASK) == SAVE_TYPE_SRAM;
    if (isSram && saveSize > SAVE_DATA_SIZE)
        return false;
    memset(gSaveData, SAVE_DATA_FILL, SAVE_DATA_SIZE);
    if (Environment::IsIsNitroEmulator())
    {
        memset((void*)ISNITRO_SAVE_BUFFER, SAVE_DATA_FILL, ISNITRO_SAVE_BUFFER_SIZE);
    }
    memset(&gSaveFile, 0, sizeof(gSaveFile));

    const BYTE openMode = Environment::IsIsNitroEmulator()
        ? FA_OPEN_EXISTING | FA_READ | FA_WRITE
        : FA_OPEN_ALWAYS | FA_READ | FA_WRITE;
    const FRESULT openResult = f_open(&gSaveFile, savePath, openMode);
    if (openResult == FR_OK)
    {
        sSaveFileOpen = true;
        const u32 initialSize = f_size(&gSaveFile);
        // Append initialized bytes before building the fast-seek map. Seeking
        // to the final size first leaves an unidentifiable hole after failure.
        if ((initialSize < saveSize && !fillSaveFile(initialSize, saveSize)) ||
            !loadSaveClusterMap())
        {
            return false;
        }

        if (saveSize <= SAVE_DATA_SIZE)
        {
            if (f_rewind(&gSaveFile) != FR_OK)
            {
                return false;
            }
            UINT read = 0;
            if (f_read(&gSaveFile, gSaveData, saveSize, &read) != FR_OK || read != saveSize)
            {
                return false;
            }
        }

        if (Environment::IsIsNitroEmulator())
        {
            if (f_rewind(&gSaveFile) != FR_OK)
            {
                return false;
            }
            UINT read = 0;
            if (f_read(&gSaveFile, (void*)ISNITRO_SAVE_BUFFER, saveSize, &read) != FR_OK || read != saveSize)
            {
                return false;
            }
        }
    }
    else if (!Environment::IsIsNitroEmulator())
    {
        return false;
    }

    gGbaSaveShared.saveState = GBA_SAVE_STATE_CLEAN;
    sSkipSaveCheckInstruction = emu_vblankIrqSkipSaveCheckInstruction;
    if (isSram)
    {
        gGbaSaveShared.saveData = gSaveData;
        gGbaSaveShared.saveDataSize = saveSize;
    }
    else
    {
        gGbaSaveShared.saveData = nullptr;
        gGbaSaveShared.saveDataSize = 0;
    }

    ipc_sendWordDirect(
        ((((u32)&gGbaSaveShared) >> 5) << (IPC_FIFO_MSG_CHANNEL_BITS + 3)) |
        (GBA_SAVE_IPC_CMD_SETUP << IPC_FIFO_MSG_CHANNEL_BITS) |
        IPC_CHANNEL_GBA_SAVE);
    while (ipc_isRecvFifoEmpty());
    ipc_recvWordDirect();
    return true;
}

extern "C" u8 sav_readSaveByteFromFile(u32 saveAddress)
{
    vm_enableNestedIrqs();
    u8 saveByte = SAVE_DATA_FILL;
    if (Environment::IsIsNitroEmulator())
    {
        // save buffer in extended memory
        if (saveAddress < ISNITRO_SAVE_BUFFER_SIZE)
            saveByte = ISNITRO_SAVE_BUFFER[saveAddress];
    }
    else
    {
        if (saveAddress < f_size(&gSaveFile))
        {
            if (f_lseek(&gSaveFile, saveAddress) != FR_OK)
                saveIoFailed();
            UINT bytesRead = 0;
            if (f_read(&gSaveFile, &saveByte, 1, &bytesRead) != FR_OK || bytesRead != 1)
                saveIoFailed();
        }
    }
    vm_disableNestedIrqs();
    return saveByte;
}

extern "C" void sav_writeSaveByteToFile(u32 saveAddress, u8 data)
{
    vm_enableNestedIrqs();
    bool written = false;
    if (Environment::IsIsNitroEmulator())
    {
        if (saveAddress < ISNITRO_SAVE_BUFFER_SIZE)
        {
            ISNITRO_SAVE_BUFFER[saveAddress] = data;
            written = true;
        }
    }
    else if (saveAddress < f_size(&gSaveFile) && f_lseek(&gSaveFile, saveAddress) == FR_OK)
    {
        UINT bytesWritten = 0;
        written = f_write(&gSaveFile, &data, 1, &bytesWritten) == FR_OK && bytesWritten == 1;
    }
    if (!written)
    {
        // Stop before the EEPROM/FLASH wrapper can discard this byte or
        // report success and continue issuing the remainder of a save.
        saveIoFailed();
    }
    vm_disableNestedIrqs();
}

extern "C" void sav_flushSaveFile(void)
{
    vm_enableNestedIrqs();
    if (!Environment::IsIsNitroEmulator() && f_sync(&gSaveFile) != FR_OK)
        saveIoFailed();
    vm_disableNestedIrqs();
}

extern "C" void sav_writeSaveToFile(void)
{
    bool saved = true;
    const u32 size = gGbaSaveShared.saveDataSize;
    if (size != 0 && !Environment::IsIsNitroEmulator())
    {
        UINT bytesWritten = 0;
        saved = size <= SAVE_DATA_SIZE && size <= f_size(&gSaveFile) &&
            f_lseek(&gSaveFile, 0) == FR_OK &&
            f_write(&gSaveFile, gSaveData, size, &bytesWritten) == FR_OK &&
            bytesWritten == size && f_sync(&gSaveFile) == FR_OK;
    }

    if (!saved)
        saveIoFailed();
    gGbaSaveShared.saveState = GBA_SAVE_STATE_CLEAN;
    dc_drainWriteBuffer();
    emu_vblankIrqSkipSaveCheckInstruction = sSkipSaveCheckInstruction;
}

[[gnu::section(".ewram")]] void sav_writePendingFiles(void)
{
    if (gGbaSaveShared.saveState == GBA_SAVE_STATE_ERROR)
        saveIoFailed();
    if (gGbaSaveShared.saveState == GBA_SAVE_STATE_WRITE)
    {
        sav_writeSaveToFile();
    }

    const bool rtcStateIsClean = gRomGpio.FlushRtcStateIfDirty();
    if (gGbaSaveShared.saveState == GBA_SAVE_STATE_CLEAN && rtcStateIsClean)
    {
        emu_vblankIrqSkipSaveCheckInstruction = sSkipSaveCheckInstruction;
    }
    else
    {
        sav_requestFileWrite();
    }
}
