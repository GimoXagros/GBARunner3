# B8CJ New Game to Save Slot investigation

Investigation ID: `KH-B8CJ-SAVE-SELECT-CF-2026-09`

Branch: `fix/kh-save-select-black-screen`

Stable diagnostic baseline before G: `77134325b0b3d0d16b3d78c9ea15fb81f658a36c`

Base: `develop` at `853179ffc5b9c669aa4f70ba2f207a7302905fb3`

This branch is diagnostic work and is not approved for merging into `develop`.

## Corrected reproduction

```text
Main Menu
-> New Game
-> Save Slot initialization
-> black screen
```

The failure occurs before the Save Slot selection screen. It is not currently an intro-video or movie-playback failure. The UI name alone is not evidence that save emulation is responsible.

## Existing hardware results

| Variant | Change | Result |
| --- | --- | --- |
| A | Baseline | FAIL: black before Save Slot |
| B | BG VRAM abort disabled | FAIL: same as A |
| C | VRAM write buffer disabled | FAIL: same as A |
| D | Both VRAM changes | FAIL: same as A |
| E | JIT disabled | FAIL: game does not boot; not a useful repeat test |
| F | Existing safe-DMA fallback | FAIL: same as A |

A-D make the two tested VRAM settings lower-priority hypotheses. F excludes only the tested safe-DMA fallback, not all DMA behavior. E does not prove a JIT defect; it only shows that JIT-off cannot reach the reproduction point.

## Diagnostic file analysis

All six supplied v2 `G3DG` files are exactly 64 bytes. Each contains only the boot-time `Armed` header: header size 64, record size 216, capacity 256, game code `B8CJ`, ROM size 32 MiB, `FR_OK`, zero dump attempts, and trigger `0xFFFFFFFF`. Variant flags are correct and distinct.

| Variant | Flags | SHA-256 |
| --- | ---: | --- |
| A | `0x0` | `E7599EC97761D66C36000631D7CC5AF8AAF2ED8FEFC19A86BF05C2ACC4E11DD8` |
| B | `0x1` | `3556D2B6D5C4576D1A35B67BAEFBC4FA8AB1115B28B8FDCEA356C3CB7374C522` |
| C | `0x2` | `A8E9B9DC150FD3B7334FBF91F168311C80615DDAF3FD7C3EF6AA48031AC23E2E` |
| D | `0x3` | `E6E897B42A08D21357C77D97641BA3879A3CC8A1536F8E609916121F2AE5199C` |
| E | `0x4` | `EA53793342DF0807DD368E5AAAE26A941AE1F52E20AAC6EFFE2A5C61303D694A` |
| F | `0x8` | `0B7A9055384ABD7C3F7FF0FF0C6A156EC28A5C8EDA96AB809B757C4DE35F93E7` |

The path was writable and initial header creation succeeded. A post-black trigger never produced a full file. The zero counters in the stale header do not prove that the in-memory VBlank ring never advanced.

## Control-flow audit

The table describes source coverage, not B8CJ use. “Partial” means at least one valid ARM7TDMI form reaches a restricted handler or `*JitNotImplemented()`.

| Instruction / form | Status | Current limitation |
| --- | --- | --- |
| ARM B | FULLY SUPPORTED | Direct target uses ARM PC+8; low/high boundary tests exist. |
| ARM BL | FULLY SUPPORTED | Same target calculation plus link behavior; B8CJ high target test remains. |
| ARM BX | FULLY SUPPORTED | ARM/Thumb target state is handled; broader transition-matrix tests are still desirable. |
| ARM LDR pc immediate pre-index | PARTIALLY SUPPORTED | Assembly handler explicitly assumes pre-index and no writeback. |
| ARM LDR pc immediate post-index | UNSUPPORTED | Patcher calls `armJitNotImplemented()`. |
| ARM LDR pc writeback | PARTIALLY SUPPORTED | Encoding is accepted in part, but the handler assumption does not implement all writeback semantics. |
| ARM LDR pc register | PARTIALLY SUPPORTED | Only a restricted add/pre-index/shift path is handled; zero-shift special cases and ROR are incomplete. |
| ARM LDM ...,pc | PARTIALLY SUPPORTED | Runtime C handler implements LDMIA with writeback; other valid addressing forms fall through. |
| ARM ALU result -> pc | PARTIALLY SUPPORTED | Register-shift form is unsupported; runtime operators, S forms, RRX/ROR and shift corners are restricted. |
| ARM MOVS pc / exception return | PARTIALLY SUPPORTED | Some immediate/assembly paths exist; coverage is not complete for every ALU form. |
| Thumb B | FULLY SUPPORTED | Patched branch target path exists. |
| Thumb BCond | FULLY SUPPORTED | Patched conditional target path exists. |
| Thumb BL | PARTIALLY SUPPORTED | Second half is handled, but source has a TODO to verify the preceding first half. |
| Thumb BX | FULLY SUPPORTED | Runtime high-register target path exists. |
| Thumb MOV pc | FULLY SUPPORTED | Runtime high-register target path exists. |
| Thumb ADD pc | FULLY SUPPORTED | Runtime high-register target path exists. |
| Thumb POP {pc} | FULLY SUPPORTED | Stack target and Thumb state path exist. |

This audit identifies plausible generic defects. It does not show which instruction B8CJ executes at failure.

## Address invariants retained

- Low guest ROM `0x08000000-0x081FFFFF` maps to static ARM9 execution `0x02200000-0x023FFFFF`.
- High-ROM guest execution keeps the guest virtual address and uses hicode mapping backed by SD cache.
- SD-cache backing pointers are recorded separately from guest and execution addresses.
- GamePak mirrors are normalized consistently for diagnostic identity.
- Existing ARM PC+8 behavior and `EA7B4FFE -> 0x09ED4000` are unchanged.

## G diagnostic design

`G-control-flow-pretrigger` keeps baseline emulation semantics. Select arms it at the Main Menu. It records a 128-event control-flow/cache ring in RAM and alternates checksummed `.g3diag.a` / `.g3diag.b` checkpoints. Periodic persistence and emergency persistence do not depend on input after failure.

The decoder rejects truncated or corrupt checkpoints and chooses the greatest valid checkpoint sequence. The instrumentation contains no `B8CJ` game-code branch, ROM offset workaround, save-slot skip, or title-specific config.

The normal ARM9 ITCM image already fills its 32 KiB region. In the diagnostic build only, the large ARM undefined C fallback is linked in EWRAM to make room for the control-flow breadcrumbs without moving fixed-address VM code. This changes diagnostic-path timing but not emulated instruction or address semantics; normal and nightly builds retain the original ITCM placement.

## Last valid control flow

```text
Source guest PC: unknown
Source execution PC: unknown
Instruction: unknown
Instruction type: unknown
ARM/Thumb: unknown
CPSR: unknown
LR: unknown

Raw target: unknown
Normalized guest target: unknown
Final execution target: unknown

Source ROM block: unknown
Target ROM block: unknown
Source cache block: unknown
Target cache block: unknown

JIT state: unknown
Hicode state: unknown
MPU state: unknown
```

## First divergence

Unknown. The A-F artifacts contain no trace records, and the mGBA reference has not yet been aligned to a valid G checkpoint.

## Failure type

Unknown. No listed failure category is selected without a valid transition trace.

## Root cause

**Root cause not proven.**

No emulator-core behavior has been changed by inference. Save emulation remains lower priority unless a trace proves that both emulators reach the same Save Slot initialization path and first diverge on a save access.

## Next evidence gate

1. Collect both G checkpoint files using the hardware procedure.
2. Decode the latest valid sequence.
3. Align its final control-flow events with the mGBA Main Menu-to-Save-Slot reference checkpoints.
4. Identify the first differing instruction, target, state, mapping, or cache generation.
5. Only then implement a generic fix and a synthetic regression that contains no commercial ROM data.

**Hardware verification required.**
