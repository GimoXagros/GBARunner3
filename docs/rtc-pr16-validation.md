# PR #16 validation record

> Current status (2026-10-02): PR #16 is merged and custom-v0.1.5 is stable.
> See the [release record](releases/custom-v0.1.5.md) for completed automated
> gates and the limited user-reported hardware result. The checkpoint statements
> below, including pending/NOT RUN and upstream issue status, are historical.


**State: candidate `90a0e8b` passes the current nightly and focused suites;
the ARM/Thumb correction is checked locally and its memory audit is complete.
Final frozen-source CI, matrix, review, and device retest remain pending.**

## User report and binary-level finding

The user reported that candidate `745a321` hangs on the 3DS+DSpico setup at
the logo, while stable v0.1.4 boots with the same ROM and settings. We did not
reproduce the hang on a device or capture the first hardware PC. Static
inspection of the linked binary reproduced two unsafe
ARM/Thumb boundaries consistent with that report, under the CP15 L4 v4T
compatibility path:

- The new Thumb RTC entry reaches an `LDR pc` veneer. On the emulated ARM7
  behavior, `LDR pc` ignores the loaded Thumb bit.
- ARM FatFs code can return through `POP {pc}` with a Thumb link register;
  with ARM7 compatibility enabled, that does not perform the expected
  interworking.

The architectural background is in the [Technical Reference Manual,
CPU compatibility section](https://github.com/GimoXagros/GBARunner3/blob/0e54f515270eed85b63a9cb31f7b403b9fcba3b7/docs/Technical%20Reference%20Manual/main.tex#L71),
especially lines 71–81. This is a binary-level diagnosis consistent with the
user report, not confirmation that the physical hang has been fixed.

The minimal correction at `90a0e8b6f0373cbf95be8b459d3429103ae71481` removes
Thumb generation while retaining `-Os` for the RTC object. FatFs `get_fileinfo`
also stays in EWRAM and retains `-Os`, with ARM code generation. This keeps the
new RTC and FatFs call/return path ARM-to-ARM. The candidate NDS SHA-256 is
`62e35d1dd8a6dec8919dd7338f7979a2670cd63fe9140522b754a99f19e49ac0`.

`tools/tests/test_rtc_boot_l4_elf.py` checks the two unsafe boundaries. It
rejects the old `745a321` ELF at both intended boundaries with
`--expect-unsafe` and passes against the corrected `90a0e8b` ELF. The new test
and its CI wiring are not yet part of the successful workflow runs below; they
must run on the frozen source SHA.

## Automated results at `90a0e8b`

Nightly [36285394523](https://github.com/GimoXagros/GBARunner3/actions/runs/36285394523)
passed all three jobs: application/test build, save-I/O host checks, and linked
hicode semantics. Focused RTC workflow
[36285394518](https://github.com/GimoXagros/GBARunner3/actions/runs/36285394518)
passed:

- 428,351 calendar cases, zero failures, Python `datetime` oracle, fatal
  ASan/UBSan.
- 2,363 shared RTC/GPIO/recovery/persistence/restart host cases, zero
  failures. Two workflow steps invoke this same suite; count it once.
- All 12 named negative controls passed.
- Retained version-1 corpus: 352 bit-flip fixtures, 44 truncation fixtures,
  and 16 GPIO protocol cases.

These controlled tests do not establish hardware startup, FAT power-cut
atomicity, or durability.

## Current memory audit

The corrected ARM9 `.ewram` section is `0x4AB8` bytes (19,128 bytes), ending at
`0x02044AB8`; EWRAM BSS starts at `0x02045000`, leaving 1,352 bytes between
them. Heap starts at `0x021F2000` and ends at `0x02200000`, giving 56 KiB
versus 60 KiB at the previous candidate and the stable 64 KiB. VRAM-A BSS ends
at `0x06820000`; ITCM and DTCM usage is unchanged from the preceding audit.

The RTC loader's `Initialize` entry is ARM code at `0x02043298`. The
`get_fileinfo` helper is ARM code, 592 bytes. The pending-adoption boot path
has a conservative bound of 960 of 992 bytes (32 bytes of margin); the
fresh/no-files boot path has a bound of 904 of 992 bytes (88 bytes of margin).
Neither is a runtime peak measurement. The dedicated 2 KiB RTC work stack had
a synthetic 224-byte high-water observation, not a maximum.
The three modern-sidecar path allocations remain; peak TLSF heap use has not
been measured.

| Gate | Status |
|---|---|
| User-reported 3DS+DSpico logo hang at `745a321` | Reported by user; not reproduced on device |
| Linked ARM/Thumb boundary diagnosis | Two unsafe boundaries found, consistent with report |
| Corrected nightly, run 36285394523 | Pass: all three jobs |
| Focused RTC suite, run 36285394518 | Pass: 428,351 calendar, 2,363 shared host, 12 negative controls |
| New L4 ELF test | Old ELF rejected at both intended boundaries; corrected ELF passes locally; frozen-SHA CI step pending |
| Corrected section and stack audit | Pass; stack estimates are static, runtime peak unmeasured |
| Final frozen-source nightly and test wiring | Pending |
| Pinned reproducibility matrix for corrected source | Pending |
| Independent review | Pending |
| 3DS+DSpico hardware retest | NOT RUN; required |
| Other NDS/DSi-compatible hardware | NOT RUN; required |
| Peak TLSF heap use | Not measured |

The previous `745a321` nightly and matrix passed their then-current automated
checks, but they predate this correction and do not cover the reported device
scenario. See [`RTC-PR16-HARDWARE.md`](RTC-PR16-HARDWARE.md) for the user
procedure and `rtc-legacy-compatibility.md` for the migration policy.
