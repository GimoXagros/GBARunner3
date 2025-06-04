.section ".dtcm", "aw"

#include "DtcmStackDefs.inc"

.global dtcmHicodeStack
dtcmHicodeStack:
    .word 0xDEAD57AC // stack protection word
    .space DTCM_HICODE_STACK_SIZE - 4
.global dtcmHicodeStackEnd
dtcmHicodeStackEnd:

.global dtcmIrqStack
dtcmIrqStack:
    .word 0xDEAD57AC // stack protection word
    .space DTCM_IRQ_STACK_SIZE - 4
.global dtcmIrqStackEnd
dtcmIrqStackEnd:

.global dtcmStack
dtcmStack:
    .word 0xDEAD57AC // stack protection word
    .space DTCM_STACK_SIZE - 4
.global dtcmStackEnd
dtcmStackEnd:

.end
