// RTC FatFs preflight runs from VBlank, whose DTCM IRQ stack is only 288 bytes.
// Keep the generic save path unchanged and switch only the RTC flush to a
// bounded EWRAM stack. This file is assembled for ARM with interworking enabled.

.section ".ewram.bss", "aw", %nobits
.balign 8
.global rtcWorkStackBase
rtcWorkStackBase:
    .space 2048
.global rtcWorkStackEnd
rtcWorkStackEnd:
.balign 4
rtcWorkStackBusy:
    .space 4
rtcWorkStackInitialized:
    .space 4
.global rtcWorkStackHighWater
rtcWorkStackHighWater:
    .space 4

.section ".ewram", "ax", %progbits
.align 2
.global rtc_runOnWorkStack
.type rtc_runOnWorkStack, %function
rtc_runOnWorkStack:
    push {r4-r8,lr}             // 24 bytes; preserve 8-byte caller alignment
    mrs r4,cpsr
    orr r6,r4,#0x80
    msr cpsr_c,r6              // guard acquisition with IRQs masked
    ldr r5,=rtcWorkStackBusy
    ldr r6,[r5]
    cmp r6,#0
    bne .Lbusy
    mov r6,#1
    str r6,[r5]

    ldr r6,=rtcWorkStackInitialized
    ldr r0,[r6]
    cmp r0,#0
    bne .LcheckGuard
    ldr r0,=rtcWorkStackBase
    ldr r1,=0xDEAD57AC
    str r1,[r0],#4
    ldr r1,=0xA5A5A5A5
    ldr r2,=rtcWorkStackEnd
.Lfill:
    cmp r0,r2
    strlo r1,[r0],#4
    blo .Lfill
    mov r0,#1
    str r0,[r6]

.LcheckGuard:
    ldr r0,=rtcWorkStackBase
    ldr r1,[r0]
    ldr r2,=0xDEAD57AC
    cmp r1,r2
    bne .Lfatal
    mov r7,sp                 // old caller stack, after callee-save push
    ldr sp,=rtcWorkStackEnd
    sub sp,sp,#8              // reserve old SP and original CPSR
    str r7,[sp]
    str r4,[sp,#4]
    msr cpsr_cxsf,r4          // restore caller IRQ mask while work executes
    bl rtc_flushOnWorkStackBody
    mov r8,r0                 // callback result is bool in r0

    mrs r4,cpsr
    orr r6,r4,#0x80
    msr cpsr_c,r6
    ldr r7,[sp]
    ldr r4,[sp,#4]            // original CPSR, including IRQ mask and flags
    ldr r0,=rtcWorkStackBase
    ldr r1,[r0]
    ldr r2,=0xDEAD57AC
    cmp r1,r2
    bne .Loverflow

    // The untouched words still hold the fill pattern. Record the deepest
    // observed write, including this wrapper's eight-byte stack header.
    add r0,r0,#4
    ldr r1,=0xA5A5A5A5
    ldr r2,=rtcWorkStackEnd
.Lmeasure:
    cmp r0,r2
    bhs .Lmeasured
    ldr r3,[r0],#4
    cmp r3,r1
    beq .Lmeasure
    sub r0,r0,#4
.Lmeasured:
    sub r2,r2,r0
    ldr r5,=rtcWorkStackHighWater
    ldr r6,[r5]
    cmp r2,r6
    strhi r2,[r5]
    ldr r5,=rtcWorkStackBusy
    mov r6,#0
    str r6,[r5]
    mov sp,r7
    msr cpsr_cxsf,r4
    mov r0,r8
    pop {r4-r8,pc}

.Loverflow:
    mov sp,r7
.Lfatal:
    bl rtc_workStackFault      // terminal; never reuses this stack
.Lbusy:
    msr cpsr_cxsf,r4
    mov r0,#0
    pop {r4-r8,pc}
.size rtc_runOnWorkStack, .-rtc_runOnWorkStack
