#include "common.h"
#include <libtwl/ipc/ipcFifo.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include "Cpsr.h"
#include "cp15.h"
#include "IpcChannels.h"
#include "GbaSaveIpcCommand.h"
#include "Save.h"
#include "SaveFault.h"
#include "SaveFaultScreen.h"

// Terminal code uses existing main-memory headroom; VRAM A is at its layout limit.
extern "C" [[gnu::noreturn, gnu::section(".ewram")]] void sav_persistenceFault(void)
{
    arm_disableIrqs();
    gGbaSaveShared.saveState = GBA_SAVE_STATE_ERROR;
    dc_flushRange(&gGbaSaveShared, sizeof(gGbaSaveShared));
    dc_drainWriteBuffer();
    // Also registers the error during boot, before normal save setup completed.
    // ARM7 retains power-button handling, mutes sound and refuses a clean exit.
    ipc_sendWordDirect(
        ((((u32)&gGbaSaveShared) >> 5) << (IPC_FIFO_MSG_CHANNEL_BITS + 3)) |
        (GBA_SAVE_IPC_CMD_SETUP << IPC_FIFO_MSG_CHANNEL_BITS) | IPC_CHANNEL_GBA_SAVE);

    // Freeze guest display producers before borrowing only VRAM B. VRAM A
    // contains the executing core and must never be remapped or overwritten.
    *(vu32*)0x04000208 = 0; // IME
    for (u32 channel = 0; channel < 4; ++channel)
        *(vu32*)(0x040000B8 + channel * 12) = 0;
    *(vu32*)0x04000064 = 0; // display capture
    *(vu32*)0x04000000 = 1u << 7; // blank while switching framebuffer
    *(vu8*)0x04000241 = 0x80; // VRAM B -> LCDC, 0x06820000
    auto* pixels = reinterpret_cast<volatile unsigned short*>(0x06820000);
    sav_renderPersistenceFaultScreen(pixels);
    // MPU region 5 makes LCDC VRAM cacheable. Publish pixels to display DMA.
    dc_flushRange((const void*)pixels, 256 * 192 * 2);
    dc_drainWriteBuffer();
    *(vu16*)0x04000304 |= 0x8003; // LCD, main 2D, main engine on top screen
    *(vu16*)0x0400006C = 0; // main master brightness
    *(vu32*)0x04001000 = 1u << 7; // blank sub engine
    *(vu32*)0x04000000 = (2u << 16) | (1u << 18); // VRAM display, bank B
    for (;;) asm volatile("nop" ::: "memory");
}
