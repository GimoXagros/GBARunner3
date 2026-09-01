.section ".itcm", "ax"

#include "AsmMacros.inc"
#include "MemoryEmulator/RomDefs.h"

#ifdef GBAR3_CONTROL_FLOW_DIAGNOSTICS
arm_func cfdiag_recordArmBxTarget
    push {r0-r3,r12,lr}
    mov r0, r8
    bl cfdiag_recordControlFlowTarget
    pop {r0-r3,r12,lr}
    sub r9, r8, #ROM_LINEAR_GBA_ADDRESS
    b cfdiag_armBxTargetReturn
#endif

.section ".ewram.bss", "aw", %nobits

#if defined(GBAR3_RUNTIME_DIAGNOSTICS) || defined(GBAR3_CONTROL_FLOW_DIAGNOSTICS)
.balign 8
.global diag_stack
diag_stack:
.global cfdiag_stack
cfdiag_stack:
    .space 2048
.global diag_stackEnd
diag_stackEnd:
.global cfdiag_stackEnd
cfdiag_stackEnd:
#endif

.end
