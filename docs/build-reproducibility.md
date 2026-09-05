# Build and CI reproducibility — 2026-09-05

The required release/application path remains
`devkitpro/devkitarm:20241104` (devkitARM GCC 14.2.0). The nightly workflow builds
the application and test NDS serially and runs repository, host sanitizer and
linked ARM semantic checks. Local Windows has no usable Docker/WSL setup, so the
authoritative full builds in this audit ran on GitHub's pinned container.

The separate Build reproducibility workflow runs two clean builds at each of
`-j1`, `-j2` and `-j4`, compares repeated NDS hashes, and uploads build logs,
compiler versions and ARM9 allocated-section sizes. Parallel configurations stay
independent of the serial release path. No sleep/retry or forced serialization
is used to hide missing dependencies.

## Reproduced dependency defects

Baseline `52198db` / source `db97744`, run
[33943180750](https://github.com/GimoXagros/GBARunner3/actions/runs/33943180750):
both clean serial builds succeeded with equal hashes. Both `-j2` and `-j4` failed
because `gbarunner9_bin.h` was missing when bootstrap main.cpp compiled. Logs also
showed duplicate recursive test links. The Makefiles allowed phony checks and
ELF recipes to launch the same sub-build simultaneously, and did not connect
bootstrap ARM9 compilation to embedded-core/header generation.

The correction gives each recursive ELF one prerequisite producer, makes
bootstrap ARM9 wait for its embedded core, makes tests wait for libtwl, and
declares generated binary/graphics header dependencies for main.o. Grouped grit
outputs avoid generating the same source/header pair concurrently. Empty wrapper
targets explicitly verify the recursively produced ELF to avoid ds_rules'
implicit relink, while the recursive default target remains the complete ELF.
GNU make 4.3 or newer is required for grouped targets; the pinned CI validates it.

## Latest toolchain experiment

The current `devkitpro/devkitarm:latest` attempted in the baseline matrix reported
GCC 16.1.0 and failed in vendored libtwl `rtosIrq.c`: `setVectorBase` is no longer
declared by the current calico/libnds headers. This is a dependency/API migration
blocker, not a reason to replace the pinned release toolchain or suppress core
warnings. After recording this repeatable failure, the experiment is available
only through manual workflow dispatch and is non-blocking. Routine PR revisions
do not repeatedly retry the known external compatibility failure.

## Official Actions and artifact contract

Official release metadata was checked on 2026-09-05:
[checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1),
[upload-artifact v7.0.1](https://github.com/actions/upload-artifact/releases/tag/v7.0.1),
[download-artifact v8.0.1](https://github.com/actions/download-artifact/releases/tag/v8.0.1),
and [github-script v9.0.0](https://github.com/actions/github-script/releases/tag/v9.0.0).
The workflows use their corresponding supported major tags and Node 24 runtimes.

Existing artifact names and paths (`GBARunner3` from `out`, dispatch/parser/JIT
results) are preserved. Uploads retain the default archive, compression, hidden
file and repository retention policies; no workflow token permissions are
expanded. New reproducibility artifacts are separate evidence records.

The prior third-party release uploader is replaced by official github-script
calling a small checked-in module. It only uploads the existing ZIP to the
release from the `published` event, checks the matching tag, and refuses to
overwrite an existing asset. It has no release/tag creation or deletion operation.
This intentionally makes rerunning an already populated release fail rather than
replace its binary. Mocked tests cover upload, wrong event/tag and existing-asset
rejection without network mutations. The real release workflow was not dispatched.

## Warning policy

No global suppression or `-w` is added, and no runtime source is modified by this
build task. See the [complete warning inventory](build-warning-inventory-20260905.md)
for the measured pinned baseline and classification. Warnings that require JIT,
DMA or test-runtime semantics remain visible for a separate focused change.
