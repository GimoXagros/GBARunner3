.section ".itcm", "ax"
.arm

#include "AsmMacros.inc"
#include "VirtualMachine/VMDtcmDefs.inc"
#include "MemoryEmulator/RomDefs.h"

arm_func memu_prefetchAbort
    sub lr, lr, #4
    cmp lr, #0x03000000
        addlo lr, lr, #(ROM_LINEAR_GBA_ADDRESS - ROM_LINEAR_DS_ADDRESS) // relative rom
    cmp lr, #0x07000000
        addlos pc, lr, #0x003F0000 // obj vram
#if ROM_LINEAR_GBA_ADDRESS != 0x08000000
    cmp lr, #ROM_LINEAR_GBA_ADDRESS
        blo 1f
#endif
    cmp lr, #ROM_LINEAR_END_GBA_ADDRESS
        addlos pc, lr, #(ROM_LINEAR_DS_ADDRESS - ROM_LINEAR_GBA_ADDRESS) // absolute rom

1:
#ifdef GBAR3_HICODE_CACHE_MAPPING
    ldr sp,= dtcmHicodeStackEnd
    push {r0-r3, r12, lr}
#ifdef GBAR3_CONTROL_FLOW_DIAGNOSTICS
    mov r0, lr
    mrs r1, spsr
    mov r2, lr
    bl cfdiag_recordPrefetchAbort
    ldr r0, [sp, #20]
#else
    mov r0, lr
#endif
    bl hic_mapRomBlock
    ldmfd sp, {r0-r3, r12, pc}^
#else
    b . // bad prefetch abort
#endif
