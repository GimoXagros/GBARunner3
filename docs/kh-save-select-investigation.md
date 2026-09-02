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

## G hardware result and H diagnostic design

Hardware testing found that G stopped when Select was pressed at the Main Menu. Without Select, the original New Game black screen still reproduced. G therefore introduced a separate diagnostic regression and produced no root-cause evidence. Its guest KEYINPUT hook and Select-triggered immediate full checkpoint are not used for diagnosis.

`H-control-flow-vblank-arm` keeps baseline emulation semantics. Select and A are observed only from the DS VBlank callback; the guest KEYINPUT load path is untouched. The diagnostic callback uses a dedicated 2 KiB EWRAM stack instead of the 288-byte IRQ scratch stack. Select only arms the RAM ring and performs no filesystem write. The first full checkpoint is delayed until 60 VBlanks after the first A press following arm, then alternates checksummed `.g3diag.a` / `.g3diag.b` files once per second. Emergency persistence does not depend on input after failure.

The decoder rejects truncated or corrupt checkpoints and chooses the greatest valid checkpoint sequence. The instrumentation contains no `B8CJ` game-code branch, ROM offset workaround, save-slot skip, or title-specific config.

The normal ARM9 ITCM image already fills its 32 KiB region. In the diagnostic build only, the large ARM undefined C fallback is linked in EWRAM to make room for the control-flow breadcrumbs without moving fixed-address VM code. This changes diagnostic-path timing but not emulated instruction or address semantics; normal and nightly builds retain the original ITCM placement.

## H hardware trace result

Both submitted H checkpoints are complete 12,352-byte `G3CF` v1 files with
valid FNV-1a checksums and game code `B8CJ`.

| File | SHA-256 | Sequence | Status | Reason |
| --- | --- | ---: | --- | --- |
| `.g3diag.a` | `3F0242A16D4F1BDB924E5E62312B869C6516D670533CA0B382030CD7E8E49C15` | 3 | checkpoint | periodic |
| `.g3diag.b` | `606EEA6836AB5D82095A8FBF66787ABE0CFB3D08BC9B5F929097CFAA3C26D42B` | 4 | emergency | not-implemented |

The decoder therefore selects `.g3diag.b`. The periodic `.a` proves that H
armed and persisted without the Select stall seen in G. The emergency `.b`
captures the failure rather than only a late black-screen sample.

## I hardware trace result

Both I checkpoints are complete and checksummed. `.g3diag.a` sequence 3 is a
periodic checkpoint; `.g3diag.b` sequence 4 is another `not-implemented`
emergency checkpoint.

| File | SHA-256 | Result |
| --- | --- | --- |
| `.g3diag.a` | `0F00D12074CFA0D8FA4611E39CF27C8DA125B143CFDBEEFBFFC9BAC8BAD58762` | valid periodic checkpoint |
| `.g3diag.b` | `730118902240EA1CD6BDCAC60EB05255F519E6D126A29E717E328BA209B95FB8` | valid emergency checkpoint |

The selected I trace ends in the same `0x09ED3570`, `0xB100BC01`,
`NOT_IMPLEMENTED` sequence as H. This rejects the I implementation, not the
saved-state diagnosis: I tested `r13` inside `notHicodeMiss` after
`ldr sp,=dtcmHicodeStackEnd` had replaced that register with the scratch stack
pointer. J instead reloads `vm_undefinedSpsr` from DTCM after switching to the
FIQ register bank.

## J hardware trace result

Both J checkpoints are complete and checksummed periodic checkpoints. No
emergency persistence occurred.

| File | SHA-256 | Sequence | Status |
| --- | --- | ---: | --- |
| `.g3diag.a` | `228F222C9CA708699698BE9153E3E965168BC9EEAF02AD447D625A87424C8BC4` | 79 | periodic checkpoint |
| `.g3diag.b` | `5AF0B7B00A5E3066A4C388BC4AAA0C84722E4F0C9992AD8AD7D690E6F41E4774` | 80 | periodic checkpoint |

The decoder selects `.g3diag.b`. The former terminal signature
`0x09ED3570`, `0xB100BC01`, `NOT_IMPLEMENTED` is absent. Execution reaches a
later high-ROM block (`0x09EE76C0`), VBlank persistence continues for about 80
seconds after the transition input, and the latest record is a normal periodic
VBlank rather than an emergency. This verifies that reloading
`vm_undefinedSpsr` removes the first proven CPU control-flow failure.

The subsequent hardware observation establishes that the screen still becomes
black before the Save Slot UI appears. The former terminal control-flow failure
is gone, but it was not the only incompatibility. Because CPU checkpoints and
VBlank continue for about 80 seconds, the remaining symptom is now investigated
as a live display, peripheral-completion, or guest polling failure. The proven
saved-SPSR correction remains in place.

## H/I failure: last valid control flow

```text
0x080656C2 Thumb MOV pc,r0
-> raw/guest target 0x09ED3561
-> prefetch abort at 0x09ED3560
-> high-ROM block mapped
-> guest bytes at 0x09ED3570: 01 BC 00 47
   (Thumb POP {r0}; BX r0)
-> JIT patches BX 0x4700 to Thumb undefined trap 0xB100
```

## H/I failure: first divergence (resolved by J)

`hic_undefinedHicodeMiss` correctly recognizes that the high-ROM block is
already mapped, but its `notHicodeMiss` path unconditionally branches to
`vm_undefinedArmInstructionInLR`. The saved CPSR is `0x20000030`, whose Thumb
bit is set. The handler nevertheless reads the aligned 32-bit value at
`0x09ED3570`, combining `0xBC01` with the JIT trap `0xB100` into
`0xB100BC01`, and sends it to the ARM decoder.

## H/I failure type (not the remaining J/K symptom)

CPU control-flow failure: high-ROM Thumb undefined-dispatch state loss. It is
not a display-only black frame and the trace does not identify save emulation,
DMA, VRAM, RTC, or video playback as the first failing subsystem.

## H/I root cause (resolved by J)

The generic high-ROM undefined dispatcher loses the guest ARM/Thumb state on a
mapped-block non-miss. The resulting fake ARM instruction is unsupported and
ends at `armJitNotImplemented()` (`memu_inst_addr = 0x03006DB0`).

The correction tests the saved SPSR Thumb bit in `notHicodeMiss`. ARM traps
retain the existing 32-bit path. Thumb traps select the proper 16-bit halfword
from the aligned I-cache word and enter the Thumb dispatcher after its normal
memory load, avoiding an invalid high-ROM data read. It does not change
high-ROM mapping, SD cache addressing, RTC, save paths, or the established RC5
entry branch behavior.

## K submitted hardware result — 2026-09-02

Both submitted K sidecars are exactly 64 bytes: G3DG v3 ready headers,
sequence 0, totalSamples 0, status Ready. Both SHA-256 values are
`E369FEBC7B9E602F692DECA35EEB2EDC65B361398DCD4A9B9C3F9E6B95EB17DA`.
They do not contain the 64-record runtime ring (a complete file is 15,680
bytes). No PC, DMA, timer, IRQ, or display conclusion can be drawn from them.

User observation: still black before the Save Slot screen, with a short
mechanical noise every few seconds. This does not prove that game audio or
the game CPU continued. The user is unavailable for hardware testing for
several hours and requested autonomous melonDS validation, without another
hardware round now.

Static instrumentation finding: the K VBlank hook puts five words on an
8-byte-aligned diagnostic stack before calling C. This violates the 8-byte
call-boundary alignment. The L diagnostic preserves six words instead. This
is an instrumentation correctness fix, **not an established cause of the K
missing checkpoints or the game's black screen**. No JIT, mapping, DMA, RTC,
save-path, or game configuration change is included.

## Autonomous melonDS lab — 2026-09-02

The submitted hardware failure is still **unresolved**. No game-compatibility
fix is inferred from the following lab success. A small headless frontend runs
the GBARunner3 NDS application inside the official melonDS core; see
[`tools/melonds`](../tools/melonds/README.md) for exact source, lab patches,
commands, and known limitations. ROM/BIOS/save contents remain local.

Stock melonDS 1.1 lacks required cache-debug operations. Experimental cache
history also required lab corrections: a 4 GiB MPU-region overflow, cache-tag
write fallthrough, debug-tag index reconstruction, and a denied STM continuing
in privileged exception mode. ROM-free tests cover these host-emulator cases.
None is evidence of the reported hardware fault, and none changes GBARunner3
mapping, JIT, DMA, RTC, or save handling. The final successful pinned host is
`cce3e49252392801da758ad41ed66d5f730d9a07` plus `cache-lab.patch`.

### Inputs and observations

- 32 MiB B8CJ ROM SHA-256:
  `0B3670F3A08BD763D153ECE802FEDF71D03DF40D0B2E004DE2835856266586C3`.
  Local D:, N:, and isolated lab copies match. Hardware sidecars do not store
  the ROM hash, so identity with the actual historical hardware run is not
  independently proven by those sidecars.
- GBA BIOS SHA-256:
  `FD2547724B505F487E6DCB29EC2ECFF3AF35A841A77AB2E85FD87350ABD36570`;
  the D: and lab BIOS match.
- Input save SHA-256:
  `2D864C0B789A43214EEE8524D3182075125E5CA2CD527F3582EC87FFD94076BC`;
  the D: and lab input saves match. Only isolated copies are used for writes.
- No `/_gba/gbarunner3.json` or matching B8CJ title configuration exists on
  the inspected D: card. The lab therefore uses the current built-in defaults.
- An mGBA 0.10.5 cross-check displayed the Korean introductory notice and
  sampled its key-poll code at `0x09ED40F4`. This was input sanity checking,
  **not** a GBARunner3 pass or a complete mGBA New Game validation. Initial
  zeros near `0x09ED4000` are not evidence of ROM corruption: executable code
  follows them and both emulators reached the notice.

### Application and diagnostic results

| Build | NDS SHA-256 | Patched melonDS result |
| --- | --- | --- |
| K, exact `5824056` rebuild | `4782FA5DC82431DEA7DCC0A1FA87BFD350D9A09A0CAB6A7FB0BFCC58FBABE87B` | Matches the submitted K binary; title, menu, New Game, Save Slot visible; selecting a slot subsequently displayed intro footage. |
| L, aligned diagnostic stack | `159B567F919DC6B980A070E27EDF8895D6F121D746D4287059FF1D59CC3B9DF7` | Independent run using copies of D: inputs; title, menu, New Game, Save Slot visible after 600 post-input frames. |

Both lab runs wrote complete 15,680-byte runtime checkpoints with valid
checksums. K's final sequence was 17 after entering footage; L's was 10 while
the Save Slot screen was visible. This does not explain why submitted K
hardware files contain only Ready headers. K/L do not diverge at Save Slot in
this lab, so L is an instrumentation fix only, not a confirmed game fix.

The exact J binary (SHA-256
`3663811864E6D07D4262C38EC25033ABE2CFC212D468AB58676CF1AF476A9A56`)
did not complete the initial 900-frame command in the same host environment;
its last periodic heartbeat was at frame 780, before any menu input. A repeat
showed the same limit. This is earlier than J's hardware failure and remains an
undiagnosed host/application interaction. It further limits the use of this
experimental melonDS environment as a hardware substitute. Do not label it
the first divergence of the user's New Game failure.

Local L checkpoint SHA-256 values: sequence 9
`ADA8A6D703F0740D353E648928CA8396E3F087D8AC4FF67FD862C532E423D0E0`,
sequence 10
`5EDE6A8AEFC78043C899E126658BA7A7278EBE9A8FB25AED1674A2BF7BC8234E`.
The associated Save Slot screenshot SHA-256 is
`FEEA793889C2BAB344AA57191E1C5F41C33A8C48C3B185392B8DCDE176734DA3`.
Payloads and screenshots stay in the local lab, not the repository.

At L's visible Save Slot checkpoint: guest DISPCNT `0x1B40`, DS DISPCNT
`0x00011B10`, mapped hicode block `0x09ED3580`, timer 0 counter changing,
and DMA-start delta 22 across the retained 64 samples. Repeated interrupt PCs
`0x06898348` / emulated-instruction marker `0x0689834C` coincide with a
visible, responsive screen and are not evidence of CPU freeze. VCOUNT is
sampled at a fixed VBlank phase; one VCOUNT value cannot prove it stopped.

Video completion, actual gameplay, sustained A/V synchronization, and the
other RC5 hardware regression titles have **not** been verified in this run.
Do not promote a release from these results.

### Build/test status

- Diagnostic K/L CI run `33625717299`: success; exact K hash reproduced.
- Application/test-ROM build run `33625717333`: success.
- Runtime/CF decoder, saved-SPSR dispatch, and diagnostic-stack tests: pass.
- Patched melonDS ROM-free selftests: pass. These are not GBARunner3 core
  regression tests proving the hardware root cause.
- GoogleTest test-ROM execution on hardware: not performed.
- No A-F game configuration changes were applied during the K/L runs.

## Next evidence gate

The K runtime-state build preserves J behavior and records the most recent 64
VBlanks. Each record combines guest PC/CPSR with guest and translated DS display
state, live timer counters, IRQ masks/pending state, all four DMA channels,
sound controls, high-ROM mapping, and SD-cache exclusion state. It alternates
checksummed `.g3diag.a` / `.g3diag.b` checkpoints once per second after the New
Game input; no post-failure hotkey is required.

The lab now reaches the Save Slot UI, but the hardware interval has no K
runtime samples to compare against it. Preserve this successful lab checkpoint
and the prior J hardware control-flow trace. Investigate remaining DS/3DS,
storage, cache/timing, and instrumentation differences without assigning a
game root cause from host-emulator defects. Do not request another hardware
round while the user is unavailable. Ultimately a valid black-interval trace
must distinguish display state from DMA/timer/IRQ polling. Only the first
proven divergent subsystem will be changed. Recheck the established RC5
hardware baseline before promotion.

**Hardware verification required.**
