#include "AsmMacros.inc"
#include "VirtualMachine/VMDtcmDefs.inc"
#include "MemoryEmulator/RomDefs.h"
#ifdef GBAR3_DIAG_AUTOCAPTURE
.section ".ewram", "ax"
.arm

// The boot SYS stack is also 4 mod 8. Over-aligned FIL locals require the real
// ABI guarantee: otherwise their memset can overwrite saved registers.
arm_func diag_initialize
    push {r4,lr}
    mov r4, sp
    bic sp, sp, #7
    bl diag_initializeMetadata
    // Metadata initialization fills this stack, before anything uses it.
    ldr sp,= diag_stackEnd
    bl diag_writeReadyFiles
    mov sp, r4
    pop {r4,pc}

arm_func diag_recordConfig
    push {r4,lr}
    mov r4, sp
    // Config is read before diag_initialize. Do not fill the diagnostic stack
    // here; it has no active frames and the later initialization adds canaries.
    ldr sp,= diag_stackEnd
    bl diag_recordConfigAligned
    mov sp, r4
    pop {r4,pc}

arm_func diag_setEnvironment
    ldr r12, [sp] // fifth argument on the original caller stack
    push {r4,lr}
    mov r4, sp
    bic sp, sp, #7
    sub sp, sp, #8
    str r12, [sp]
    bl diag_setEnvironmentAligned
    mov sp, r4
    pop {r4,pc}

arm_func diag_recordDmaStart
    push {r4,lr}
    mov r4, sp
    bic sp, sp, #7
    bl diag_recordDmaStartAligned
    mov sp, r4
    pop {r4,pc}

// The legacy hicode C call chain can enter with SP == 4 mod 8. Do not inherit
// that ABI defect in the new SD metadata call or change the production stack.
arm_func diag_recordSdLoad
    push {r4,lr}
    mov r4, sp
    bic sp, sp, #7
    bl diag_recordSdLoadAligned
    mov sp, r4
    pop {r4,pc}

// Every C call switches to the aligned, separate event stack. The incoming
// FIQ/ABT scratch stack can be 4 mod 8; no C call uses that stack here.
// Only the generic low-address self-return interval is traced. No title PC.
.macro low_trace kind, target
    push {r0-r3,r12,lr}
    cmp \target, #0x01200000
    bls 9f
    cmp \target, #0x02000000
    bhs 9f
    ldr r0,= gDiagTraceKinds
    ldr r0, [r0]
    tst r0, #\kind
    bne 9f
    mov r0, sp
    mov r2, \target
    ldr sp,= diag_eventStackEnd
    push {r0,r1}
    mov r0, #\kind
    .if \kind == 1
        mov r1, r11
        mov r3, r10
    .elseif \kind == 2
        sub r1, r11, #4
        ldr r3,= vm_undefinedSpsr
        ldr r3, [r3]
    .elseif \kind == 4
        ldr r1,= vm_undefinedInstructionAddr
        ldr r1, [r1]
        sub r1, r1, #4
        mov r3, r10
    .else
        ldr r1,= memu_inst_addr
        ldr r1, [r1]
        mrs r3, spsr
    .endif
    bl diag_recordLowTarget
    pop {r0,r1}
    mov sp, r0
9:
    pop {r0-r3,r12,lr}
.endm

arm_func diag_thumbTarget
    low_trace 1, r8
    sub lr, r8, #ROM_LINEAR_GBA_ADDRESS
    b diag_thumbTargetReturn

arm_func diag_armBxTarget
    low_trace 2, r8
    sub r9, r8, #ROM_LINEAR_GBA_ADDRESS
    b cfdiag_armBxTargetReturn

arm_func diag_irqReturnTarget
    low_trace 4, r8
    msr spsr, r10
    b diag_irqReturnTargetReturn

arm_func diag_prefetchEntry
    // ABT SP is not guaranteed to address a stack on exception entry.
    // Preserve it without touching a shared guest register or dereferencing it.
    str sp, diag_prefetchSavedSp
    ldr sp,= diag_prefetchScratchEnd
    sub lr, lr, #4
    low_trace 8, lr
    ldr sp, diag_prefetchSavedSp
    b diag_prefetchEntryReturn
.pool
.balign 4
diag_prefetchSavedSp: .word 0
.global diag_prefetchScratch
diag_prefetchScratch: .word 0xA93D57AC
    .space 28
diag_prefetchScratchEnd:
#endif
