# PR #16 validation record

**State: automated checkpoint passes; final gate remains open.** The results
below apply to named checkpoints and do not imply hardware verification.

## Historical arithmetic candidate

The earlier arithmetic-only head `53c9545c47499a97016885894da3b92770366a22`
reported 428,351 calendar cases and 424 GPIO/recovery checks. That head had no
v2 migration or production write/restart/reload coverage. These figures are
historical and do not substitute for the v2 checks below.

## Passing automated checkpoints

The focused RTC workflow [36281135086](https://github.com/GimoXagros/GBARunner3/actions/runs/36281135086)
passed at runtime checkpoint `236e4ff03c209a6d6517166d5c5012d1c1efc372`:

- Frozen v1 byte fixtures and the read-only inspect / explicit-adoption tool
  tests passed.
- Calendar conversion passed 428,351 cases with zero failures, a Python
  `datetime` oracle, and fatal ASan/UBSan settings.
- The shared RTC/GPIO/recovery/persistence/restart suite passed 2,340 cases
  with zero failures under synthetic FatFs and clock seams. CI invoked this same
  suite through both the GPIO/recovery and production-persistence entry points;
  these are not 4,680 independent cases.
- The suite retains 352 version-1 bit-flip cases, 44 truncation cases, and 16
  GPIO protocol cases. Old automatic-recovery expectations were replaced by the
  approved legacy gate and explicit adoption policy.
- All 10 named negative controls were rejected by their intended assertions:
  `no-dirty`, `partial-offset`, `legacy-auto-load`, `legacy-overwrite`,
  `backup-delete`, `direct-old-delta`, `version-bypass`, `identity-bypass`,
  `uncleared-dirty`, and `repeat-migration`.

The pinned nightly [36281135017](https://github.com/GimoXagros/GBARunner3/actions/runs/36281135017)
passed all jobs at the same checkpoint, including the devkitARM application
and test NDS build, repository checks, RTC host checks, and linked ARM
regressions including the production RTC adoption/restart path. This is build
and controlled linked-test evidence, not device execution.

## Memory and remaining gates

The linked application probe at `b72014b` passed through the production loader
and RTC-only stack wrapper, including ARM `r4-r11`, SP/CPSR, guard/canary, and
re-entry checks. A source call-graph plus `.su` audit found a conservative
reachable stack bound of 224 bytes for RTC frames plus 528 bytes for FatFs/IPC,
752 bytes total within the 2 KiB EWRAM wrapper; the audited call graph has no
reachable indirect RTC calls or recursion. A synthetic seam observed 248 bytes
high-water, which is not the bound and is not a hardware measurement.

At `b72014b`, `.ewram` was 15,824 bytes, ending at `0x02043DD0`; EWRAM BSS began
at `0x02044000`, and heap began at `0x021F1000`, leaving 60 KiB versus the stable
64 KiB. RTC static records plus alignment account for 4 KiB; three modern-path
heap path-string allocations were added. The dedicated stack occupies the
natural EWRAM gap before the cache (base `0x020F00F8`, end `0x020F08F8`, metadata
end `0x020F0904`, cache start `0x020F1000`). DTCM was full; ITCM ended at
`0x7FF8` (8 bytes remaining); VRAM-A ended at `0x06820000`, with region limits
unchanged. The linked ELF has no section overlap or stack-bound overrun. Peak
TLSF heap availability was not measured; runtime heap headroom remains an
accepted hardware limitation. Do not claim the stable 64 KiB heap availability
for this build.

| Final gate | Status |
|---|---|
| Frozen-candidate nightly and required linked checks | Pending final source SHA |
| Pinned reproducibility matrix | Pending memory audit and candidate freeze |
| Section and conservative stack audit | Pass at `b72014b`; repeat on frozen SHA |
| Peak TLSF heap use under application load | Not measured; accepted hardware limitation |
| Independent read-only review | Pending frozen candidate |
| NDS/DSi-compatible hardware | NOT RUN; HARDWARE VERIFICATION REQUIRED |
| Single hardware-check artifact and manifest | Pending all automated, memory, and review gates |

The final user package is planned to contain one application NDS, a manifest
with exact source SHA/NDS SHA-256/toolchain, this user guide, and
`tools/rtc_migrate.py` plus its usage instructions. Test NDS, ELF/MAP, logs,
ROMs, BIOS, save, and RTC files are excluded.

Host and linked tests use controlled filesystems and clocks. They do not prove
FAT power-cut atomicity or durability. An NDS build does not prove hardware
execution. Hardware procedure: [`RTC-PR16-HARDWARE.md`](RTC-PR16-HARDWARE.md).
