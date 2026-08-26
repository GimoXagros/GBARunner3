.section ".itcm", "ax"
.altmacro

#include "AsmMacros.inc"
#include "VirtualMachine/VMDtcmDefs.inc"

arm_func jit_armUndefinedB
    sub lr, lr, #0x02000000
    str lr, [r11, #-4]
    mov r10, #0
    mcr p15, 0, r10, c7, c10, 4
#ifdef GBAR3_HICODE_CACHE_MAPPING
    mcr p15, 0, r10, c6, c4, 0 // disable mpu region
#endif
    mcr p15, 0, r10, c7, c5, 0
    ldr r10, [r10, #vm_undefinedSpsr]
    push {r0-r3}
    mov r0, r11
    mov r1, lr
    bl jit_calculateArmBranchTarget
    bl jit_ensureBlockJitted
    mov r8, r0
    pop {r0-r3}
    msr spsr, r10
    movs pc, r8

.section ".dtcm", "aw"

.balign 64

.global jit_armUndefinedBTable
jit_armUndefinedBTable:
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
    .word jit_armUndefinedB
