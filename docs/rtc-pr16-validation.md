# PR #16 validation record

**State: target build and memory audit pass at the current checkpoint; final
matrix and review remain pending. Hardware verification has not been run.**
Results below are tied to named source checkpoints and runs.

## Focused automated checks

At `f24e3d0e8a3025df5923b4ec17e964c6ef884dcd`, focused RTC workflow
[36282878735](https://github.com/GimoXagros/GBARunner3/actions/runs/36282878735)
passed:

- 428,351 calendar cases, zero failures, Python `datetime` oracle, fatal
  ASan/UBSan.
- 2,363 cases in the shared RTC/GPIO/recovery/persistence/restart suite, zero
  failures. Two workflow steps invoke that same suite; count it once.
- All 12 named negative controls, including two regression assertions that
  fail against the preceding `de698234` production implementation.
- Retained legacy corpus: 352 v1 bit-flip fixtures, 44 truncation fixtures,
  and 16 GPIO protocol cases. Legacy recovery expectations use the approved
  explicit-adoption policy.

## Current target link and memory audit

At source checkpoint `04b6a3f32e85e57e4ac94a72ce03c6a3f25835d1`, the
application target link passed. Full nightly workflow
[36283099966](https://github.com/GimoXagros/GBARunner3/actions/runs/36283099966)
was still running when this note was updated; the focused RTC workflow at
this checkpoint,
[36283099934](https://github.com/GimoXagros/GBARunner3/actions/runs/36283099934),
passed. Full nightly completed successfully across all three jobs: application
and test build, save-I/O host tests, and linked hicode semantics.

The final ARM9 layout has `.text` ending at `0x0680D548`, VRAM-A BSS ending at
`0x06820000`, and `.ewram` ending exactly at EWRAM BSS start `0x02044000`:
16,384 bytes with zero slack before BSS. Heap remains `0x021F1000` through
`0x02200000`, 60 KiB versus the stable 64 KiB. ITCM ends at `0x7FF8` (8 bytes
free); DTCM is full at 16 KiB. No section overlap was found.

The helpers are placed in distinct appropriate target sections:
`get_fileinfo` is Thumb/`-Os`, 438 bytes at `0x020439B1`; `f_stat` is ARM,
112 bytes at `0x0680AC28`. Required interworking veneers are present and were
verified. A null `FILINFO` bypasses the helper call at runtime. The helper
bodies are unchanged. `.su` reports 40 bytes for `get_fileinfo` and 72 bytes
for `f_stat`; existing RTC frames remain `ReadRecord` 64, `WriteStateFile`
56, and `Flush` 32 bytes. `f_stat`'s backend path is 488 bytes, below the
existing `f_open` path at 528 bytes.

The direct/tail-call graph plus `.su` audit gives a conservative RTC path bound
of 224 bytes of RTC frames plus 528 bytes of FatFs/IPC frames, 752 bytes total
within the dedicated 2 KiB EWRAM stack. No reachable indirect RTC calls or
recursion were found. The linked runner passed directory-slot, sequential
boundary, and wrapper checks; 248 bytes was the observed synthetic high-water,
not a maximum and not hardware evidence.

The previous `f_stat`/`get_fileinfo` link attempt at `f24e3d0` exceeded VRAM-A
by `0x800`; the current placement resolves that target link. Peak TLSF heap
use is unmeasured, and the three modern-sidecar path allocations remain an
accepted hardware limitation. Do not claim the stable 64 KiB heap availability
for this build.

The NDS artifact for `04b6a3f` is under external evidence directory
`../pr16-evidence/04b6a3f/nightly`. Application SHA-256 is
`d7b00a3c1b88c1698f12f217482c71da27a52a66780770392e2f056f579eee3b`;
test NDS remains `50cce7e4ee4f5ae5fd814d0dea14edf39a0ecb4c017af5395cb327d32b713de7`.

| Gate | Status at this checkpoint |
|---|---|
| Focused RTC tests, run 36283099934 | Pass: 428,351 calendar, 2,363 shared host, 12 negative controls |
| Application target link and section audit, `04b6a3f` | Pass; no overlap, zero EWRAM code slack before BSS |
| Linked directory/sequence/RTC wrapper runner | Pass; synthetic high-water 248 bytes only |
| Full nightly, run 36283099966 | Pass: all three jobs |
| Pinned reproducibility matrix on final frozen SHA | Pending |
| Independent read-only review of final candidate | Pending |
| Peak TLSF heap use | Not measured; hardware limitation |
| NDS/DSi-compatible hardware | NOT RUN; HARDWARE VERIFICATION REQUIRED |
| Single hardware-check artifact and manifest | Pending final CI, matrix, and review |

The planned user package contains one application NDS, an exact-source/NDS
hash and toolchain manifest, this hardware guide, and `tools/rtc_migrate.py`
with usage instructions. Test NDS, ELF/MAP, logs, ROMs, BIOS, save, and RTC
files are excluded. Controlled host tests do not establish FAT power-cut
atomicity or durability. See
[`RTC-PR16-HARDWARE.md`](RTC-PR16-HARDWARE.md) for the hardware procedure.
