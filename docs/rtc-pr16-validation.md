# PR #16 validation record

**Status: IN PROGRESS.** This note separates historical PR #16 evidence from
the current implementation candidate. It is not a final acceptance report.

## Historical arithmetic candidate

The earlier candidate at `53c9545c47499a97016885894da3b92770366a22` reported
428,351 calendar checks and 424 GPIO/recovery checks, plus pinned CI. Those
results apply to that older candidate only. They did not exercise v2 migration,
the production persistence write/restart/reload path, or the now-required
failure matrix. Do not use them as evidence for the current candidate.

## Current candidate

Candidate SHA: **update after the implementation is frozen**.

| Check | Command / evidence | Result |
|---|---|---|
| Independent v1 fixtures | `python3 tools/tests/test_rtc_legacy_fixtures.py` | Pending final candidate run |
| Migration inspect/adoption | `python3 tools/tests/test_rtc_migrate.py` | Pending final candidate run |
| Calendar with fatal sanitizers | `SANITIZE=1 UBSAN_OPTIONS=halt_on_error=1 python3 tools/tests/test_rtc_calendar_host.py` | Pending final candidate run |
| GPIO/recovery and negative controls | `SANITIZE=1 UBSAN_OPTIONS=halt_on_error=1 python3 tools/tests/test_rtc_recovery_host.py` and its documented negative controls | Pending repair and final candidate run |
| Production persistence/restart/reload | `SANITIZE=1 UBSAN_OPTIONS=halt_on_error=1 python3 tools/tests/test_rtc_v2_host.py` | Pending repair and final candidate run |
| Repository nightly | `.github/workflows/nightly.yml` on exact frozen SHA | Pending |
| Pinned build matrix | `.github/workflows/build-matrix.yml`, one designated owner, exact frozen SHA | Pending |
| Linked ARM path and section/stack/heap budgets | Build evidence for exact frozen SHA | Pending |
| Independent read-only review | Frozen candidate SHA and test evidence | Pending |
| NDS/DSi-compatible hardware | Manual procedure in `RTC-PR16-HARDWARE.md` | NOT RUN; hardware verification required |

## Hardware-check artifact

The final single-candidate package is not built yet. Its planned contents are
one application NDS, a manifest with exact source SHA/NDS SHA-256/toolchain,
this user guide, and `tools/rtc_migrate.py` with its usage instructions. Test
NDS files, ELF/MAP files, logs, ROMs, BIOS files, saves, and RTC sidecars do not
belong in the user package. Build the package only after the final candidate,
memory audit, automated checks, and independent review are complete.

Interim checkpoints built and packaged an application/test NDS, but the
automated workflow did not pass: one host harness still assumed the old
production function layout, and a later production RTC assertion failed. These
checkpoints are not the final candidate and do not count as passing results.
The test harness and implementation are being repaired. Re-run the required
checks on the frozen SHA and replace this table with exact commands, run links,
counts, and conclusions.

## Claims boundary

Host tests with synthetic files and controlled filesystem/clock seams establish
only the behaviors they execute. An NDS build establishes compilation and
packaging, not execution on a DS-family device. Normal shutdown/restart evidence
does not establish FAT power-cut atomicity or durability. No hardware result is
claimed here.
