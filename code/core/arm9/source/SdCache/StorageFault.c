#include "common.h"
#include "Cpsr.h"
#include "cp15.h"
#include "SdCache.h"

// Always writable main memory, including when called from an exception/IRQ.
// No filesystem logging, automatic retry, guest continuation or exit here.
[[gnu::section(".ewram.bss")]] volatile u32 gStorageFaultRomAddress;

[[gnu::noreturn]] void sdc_storageFault(u32 romAddress)
{
    arm_disableIrqs();
    gStorageFaultRomAddress = romAddress;
    dc_drainWriteBuffer();
    // Returning NULL into load8/16/32 assembly or a decoder would turn an I/O
    // error into a fabricated instruction/data read. Recovery/UI is separate.
    for (;;)
        asm volatile("nop" ::: "memory");
}
