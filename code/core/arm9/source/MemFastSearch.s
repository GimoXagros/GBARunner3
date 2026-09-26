.text
.arm

// r0: data
// r1: size
// r2: pattern (4 words, each word should be unique)
.global mem_fastSearch16
.type mem_fastSearch16, %function
mem_fastSearch16:
    push {r4-r11,lr}
    add r1, r1, r0
    sub r1, r1, #44 // 32 bytes to load at once + 12 bytes after it
    ldmia r2, {r2-r5}

1:
    ldmia r0!, {r6-r12,lr}
    cmp r6, r2
    cmpne r7, r2
    cmpne r8, r2
    cmpne r9, r2
    cmpne r10, r2
    cmpne r11, r2
    cmpne r12, r2
    cmpne lr, r2
    beq fastSearch16_firstWordMatch
fastSearch16_continueFastSearch:
    cmp r0, r1
    ble 1b

    // only need to handle the last couple of words now
    add r1, #(44 - 16)
2:
    ldr r6, [r0], #4
    cmp r6, r2
    beq fastSearch16_firstWordMatchLast
fastSearch16_continueLastSearch:
    cmp r0, r1
    ble 2b

    mov r0, #0
    pop {r4-r11,pc}

fastSearch16_firstWordMatchLast:
    // Keep r0 at the next candidate when a prefix fails. Post-incrementing
    // the lookahead skipped later aligned starts in the final window.
    ldr r6, [r0]
    cmp r6, r3
    ldreq r6, [r0, #4]
    cmpeq r6, r4
    ldreq r6, [r0, #8]
    cmpeq r6, r5
    bne fastSearch16_continueLastSearch
    sub r0, r0, #4
    pop {r4-r11,pc}

fastSearch16_firstWordMatch:
    // A first-word collision is rare. Recheck every aligned start in this
    // batch in address order, then resume the eight-word fast path. The old
    // specialized branches advanced r0 after a failed continuation and could
    // skip a real match at the next candidate (including the linear ROM path).
    sub r0, r0, #32
    mov r7, #8
3:
    ldr r6, [r0]
    cmp r6, r2
    bne 4f
    ldr r6, [r0, #4]
    cmp r6, r3
    bne 4f
    ldr r6, [r0, #8]
    cmp r6, r4
    bne 4f
    ldr r6, [r0, #12]
    cmp r6, r5
    popeq {r4-r11,pc}
4:
    add r0, r0, #4
    subs r7, r7, #1
    bne 3b
    b fastSearch16_continueFastSearch
