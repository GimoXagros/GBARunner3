# Autonomous maintenance — 2026-09-05

Starting fork develop: `4bc4d73b59976e828af1ae7865cec98bb2abfb19`.
Release remains `custom-v0.1.2` at `dd3f44be5e9412ba29f3d831fc236dcc6016b71e`.
Runtime/build snapshot before this documentation-only update: `8a0ab62f9d7f7cd7c7211648b747bbf76181068a`.
The requested fork is the local remote named `fork`; `origin` is Gericom upstream.
Existing dirty investigation worktrees were preserved and separate task branches
were created from the latest fetched fork develop.

| Task | Result | PR / evidence | Remaining gate |
| --- | --- | --- | --- |
| strict JSON patch addresses | MERGED | [#4](https://github.com/GimoXagros/GBARunner3/pull/4), merge `db97744` | patch mapping and full target NDS hardware run remain separate |
| 4 KiB high-ROM save-function search | DRAFT PR | [#5](https://github.com/GimoXagros/GBARunner3/pull/5), 19 source signatures / 494 linked ARM cases | physical SD read-error propagation |
| save I/O fault injection and byte guards | DRAFT PR | [#6](https://github.com/GimoXagros/GBARunner3/pull/6), 22 passing scenarios / 14 expected failures | failure state, bounded retry/acknowledgment, interrupted initialization, linked byte-I/O and hardware gates |
| build/CI modernization | MERGED | [#7](https://github.com/GimoXagros/GBARunner3/pull/7), merge `8a0ab62`, repeated pinned serial/j2/j4 builds | latest-toolchain migration stays blocked |
| extended automatic regressions | MERGED | [#8](https://github.com/GimoXagros/GBARunner3/pull/8), merge `afe3a39`, 45 linked checks | full interworking instruction sequences and physical cache/timing |
| EEPROM V124 research | MERGED / RESEARCH COMPLETE | [#9](https://github.com/GimoXagros/GBARunner3/pull/9), merge `a2de095` | a reproducible variant; no signature or protocol fix claimed |
| source-profile lifecycle design | MERGED / RESEARCH COMPLETE | [#10](https://github.com/GimoXagros/GBARunner3/pull/10), merge `2c5ad0a` | logical-view ownership and upstream #205 integration evidence |

## Production and tests

The merged runtime change is strict JSON address validation with per-property
atomic replacement. All 304 shipped configurations and 2,513 addresses retain
their values. Host tests compile the real serializer; CI uses ASan/UBSan and
executes 19 pinned ARM parser cases. The six existing linked hicode dispatch
cases and negative controls remain, and 45 new linked JIT/address/cache checks
cover backing pointers, mirrors, metadata reset, stale-bit rejection and
unmap-before-invalidate ordering. EEPROM source selectors and byte wrappers have
12 selector/failure cases, three synthetic round trips and V124/V125 routing checks.

The two draft save branches are not part of develop or its nightly artifact.
Their green checks reproduce the stated limited evidence. In particular, the
save I/O suite's `--require-fixed` mode remains red for the 14 explicit failures.
No guessed JIT, DMA, timer/IRQ, RTC, EEPROM or FLASH protocol changes were added.

## Build and evidence

Pinned GCC 14.2.0 remains authoritative. Serial, -j2 and -j4 each completed two
clean builds with identical NDS hashes after dependency correction. Original
parallel failure: generated `gbarunner9_bin.h` unavailable at compilation and
duplicate recursive producers. The corrected graph declares producer/header
dependencies and preserves the complete recursive default target.

The corrected build's application SHA-256 is
`9968bb423430b2fcfc6aacec70a5c2e5603f711952c6eb2d6c57fbfac287a3b2`;
test NDS SHA-256 is
`50cce7e4ee4f5ae5fd814d0dea14edf39a0ecb4c017af5395cb327d32b713de7`.
These match the pre-build-change serial output (which already includes JSON
validation). This is artifact identity, not new hardware verification.

The latest GCC 16.1.0 image failed in vendored libtwl/calico at `setVectorBase`.
It is retained as a manual, non-blocking experiment. Official Actions use
supported Node 24 majors; artifact paths, names, defaults and permissions are
preserved. The release-upload module is tested with mocks and rejects replacing
an existing asset. No real release workflow was run during this task.

## Research and blockers

[EEPROM research](eeprom-v124-research.md) finds V124/V125 share a patcher and
all six prefixes match GBARunner2 lineage. No new pattern is independently proven.
Issue #198's JIT startup report is distinct from tag/function detection and save
I/O. The [source-profile design](romhack-source-profile-design.md) separates
source/effective identities, audits upstream #205 at `b683045d` and identifies a
first-cluster/header-consistency hypothesis that needs a synthetic reproducer.

Physical hardware gates remain: B8CJ after Save Slot, intro/gameplay,
save/restart/load, RTC cold start and interrupted-write recovery, wider ROM-hack
compatibility and actual 3DS audio/timing. Upstream/library gates are the latest
toolchain migration, first-cluster patch-view ownership and repository licensing
decisions. None is treated as a reason to invent a runtime workaround.

## Single hardware queue

1. Use one production-equivalent nightly identified by commit/run/NDS hash,
   exact config revision and private ROM/BIOS/save hashes; use isolated saves.
   Continue B8CJ through slot selection, intro, gameplay, save, restart and load.
2. Check RTC cold boot, date/time state and interrupted sidecar recovery.
3. Check Emerald, Crystal Dust, Lazarus, Elite Redux and Inclement Emerald routes.
4. For Dr. Mario & Puzzle League, record tag detection, function identification,
   JIT startup and JIT-off performance separately; then wider audio/timing.

Record date/environment, exact artifact/input identities, route and observations.
No speculative A/B/C/D candidate set or automatic diagnostic capture is required.

## Release and safety

No release created or modified.

No ROM, BIOS, save, NDS, ELF, ZIP, or diagnostic payload was committed.
No force push or history rewrite was performed.
No game-specific runtime workaround was added.

Merged branches are deleted only after merge-tip ancestry and absence of an open
PR are verified. Draft save branches remain. Existing investigation branches,
release tags and archive tags are preserved.

Recommended next step: design the bounded save-write error/acknowledgment and
retry state transition using PR #6's deterministic fault matrix, including ARM7
flush waiting, before extending any production save-recovery code.
