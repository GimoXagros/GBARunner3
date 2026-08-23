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
#include "Peripherals/RomGpio/RomGpio.h"
#include "VirtualMachine/VMNestedIrq.h"
#include "MemoryEmulator/RomDefs.h"
#include "IpcChannels.h"
#include "GbaSaveIpcCommand.h"
#include "Save.h"

#define DEFAULT_SAVE_SIZE   (32 * 1024)

[[gnu::section(".ewram.bss")]]
u8 gSaveData[SAVE_DATA_SIZE] alignas(32);

[[gnu::section(".ewram.bss")]]
FIL gSaveFile alignas(32);

[[gnu::section(".ewram.bss"), gnu::aligned(32)]]
gba_save_shared_t gGbaSaveShared;

static DWORD sClusterTable[64];
static u32 sSkipSaveCheckInstruction;

void sav_initializeFileWriteScheduler(void)
{
    sSkipSaveCheckInstruction = emu_vblankIrqSkipSaveCheckInstruction;
}

[[gnu::section(".ewram")]] void sav_requestFileWrite(void)
{
    // ARM nop. The VBlank handler restores the original branch after every
    // pending SRAM and RTC write has completed.
    dc_drainWriteBuffer();
    emu_vblankIrqSkipSaveCheckInstruction = 0xE1A00000;
}

bool sav_tryPatchFunction(const u32* signature, u32 saveSwiNumber, void* patchFunction)
{
    u32* function = (u32*)mem_fastSearch16((const u32*)ROM_LINEAR_DS_ADDRESS, ROM_LINEAR_SIZE, signature);
    if (!function)
        return false;

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
    u32 saveSize = saveTypeInfo ? saveTypeInfo->size : DEFAULT_SAVE_SIZE;
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
        bool clusterMapLoaded = false;
        u32 initialSize = f_size(&gSaveFile);
        if (initialSize < saveSize)
        {
            if (f_lseek(&gSaveFile, saveSize) == FR_OK)
            {
                f_rewind(&gSaveFile);
                clusterMapLoaded = loadSaveClusterMap();
                if (!clusterMapLoaded || !fillSaveFile(initialSize, saveSize))
                {
                    f_close(&gSaveFile);
                    return false;
                }
            }
            else
            {
                f_close(&gSaveFile);
                return false;
            }
        }

        if (!clusterMapLoaded)
        {
            if (!loadSaveClusterMap())
            {
                f_close(&gSaveFile);
                return false;
            }
        }

        if (saveSize <= SAVE_DATA_SIZE)
        {
            f_rewind(&gSaveFile);
            UINT read = 0;
            if (f_read(&gSaveFile, gSaveData, saveSize, &read) != FR_OK || read != saveSize)
            {
                f_close(&gSaveFile);
                return false;
            }
        }

        if (Environment::IsIsNitroEmulator())
        {
            f_rewind(&gSaveFile);
            UINT read = 0;
            if (f_read(&gSaveFile, (void*)ISNITRO_SAVE_BUFFER, saveSize, &read) != FR_OK || read != saveSize)
            {
                f_close(&gSaveFile);
                return false;
            }
        }
    }
    else if (!Environment::IsIsNitroEmulator())
    {
        return false;
    }

    gGbaSaveShared.saveState = GBA_SAVE_STATE_CLEAN;
    if (!saveTypeInfo || (saveTypeInfo->type & SAVE_TYPE_SRAM))
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
    u8 saveByte;
    if (Environment::IsIsNitroEmulator())
    {
        // save buffer in extended memory
        saveByte = ISNITRO_SAVE_BUFFER[saveAddress];
    }
    else
    {
        // write to file
        f_lseek(&gSaveFile, saveAddress);
        UINT bytesRead = 0;
        f_read(&gSaveFile, &saveByte, 1, &bytesRead);
    }
    vm_disableNestedIrqs();
    return saveByte;
}

extern "C" void sav_writeSaveByteToFile(u32 saveAddress, u8 data)
{
    vm_enableNestedIrqs();
    if (Environment::IsIsNitroEmulator())
    {
        // save buffer in extended memory
        ISNITRO_SAVE_BUFFER[saveAddress] = data;
    }
    else
    {
        // write to file
        f_lseek(&gSaveFile, saveAddress);
        UINT bytesWritten = 0;
        f_write(&gSaveFile, &data, 1, &bytesWritten);
    }
    vm_disableNestedIrqs();
}

extern "C" void sav_flushSaveFile(void)
{
    vm_enableNestedIrqs();
    if (!Environment::IsIsNitroEmulator())
    {
        f_sync(&gSaveFile);
    }
    vm_disableNestedIrqs();
}

extern "C" void sav_writeSaveToFile(void)
{
    if (gGbaSaveShared.saveDataSize != 0 && !Environment::IsIsNitroEmulator())
    {
        f_lseek(&gSaveFile, 0);
        UINT bytesWritten = 0;
        f_write(&gSaveFile, gSaveData, gGbaSaveShared.saveDataSize, &bytesWritten);
        f_sync(&gSaveFile);
    }

    if (gRomGpioRtcStateDirty)
    {
        gRomGpio.FlushRtcStateIfDirty();
    }

    gGbaSaveShared.saveState = GBA_SAVE_STATE_CLEAN;
    emu_vblankIrqSkipSaveCheckInstruction = sSkipSaveCheckInstruction;
}
