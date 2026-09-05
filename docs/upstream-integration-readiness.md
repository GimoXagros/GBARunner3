# Upstream integration readiness

Fork baseline: `custom-v0.1.2` at
`dd3f44be5e9412ba29f3d831fc236dcc6016b71e`

Audit date: 2026-09-05

## PR #210

- Upstream PR: <https://github.com/Gericom/GBARunner3/pull/210>
- State: closed without merge on 2026-08-29
- Base/head at closure: `Gericom:develop` <-
  `GimoXagros:custom/rtc-romhack-compat-rc5`
- Reviews: none
- Closure reason recorded by the fork owner: the custom line was integrated
  into the fork's `develop` branch and preserved by the `custom-v0.1.1` tag and
  release.
- The former PR head branch is no longer published by the fork. Its history is
  retained by release tags; no history rewrite or attempt to reopen the closed
  PR is part of this maintenance work.

PR #210 did not receive upstream CI or maintainer approval and must not be
described as merged upstream. The fork's successful CI and hardware results are
supporting evidence for a future, separately scoped submission.

## Current fork delta

The fork `develop` branch is based on upstream `develop` commit
`ecaa817815d9761745606592f04affe5ee9c3731` and adds the custom RTC/GPIO,
high-ROM hicode/JIT, save-path, test, configuration, branding, and documentation
line. Fork PR #2 additionally fixes:

- saved ARM/Thumb state in already mapped high-ROM undefined dispatch;
- selection of the correct Thumb halfword from the instruction-cache word;
- dynamic Thumb JIT metadata and patch writes through the SD-cache backing
  block; and
- hicode unmapping before whole instruction-cache invalidation.

Fork PR #2 is merged at
`dd3f44be5e9412ba29f3d831fc236dcc6016b71e`; the hardware-tested implementation
commit is `9b991ac9c89e1952b8573f4bf8bc9708bcade92b`.

## PR #205 overlap

Upstream PR #205 is <https://github.com/Gericom/GBARunner3/pull/205>. It remains
open, targets `feature/cache-hicode`, and adds IPS/UPS-to-GPO patching. Its work
overlaps the custom line in hicode mapping and application initialization, so a
future submission must be rebased conceptually onto the maintainer's chosen
patching design rather than duplicating it.

## Future submission units

Any new upstream proposal should use new branches and small dependency-aware
changes. The recommended order is:

1. RTC/GPIO transaction support;
2. RTC persistence and recovery;
3. high-ROM/hicode baseline;
4. mapped Thumb dispatch and JIT cache coherence;
5. save path and save-signature scanning;
6. regression and linked-code tests; and
7. user and integration documentation.

The closed PR history should remain unchanged. Creating these upstream
submissions is outside the current maintenance scope.

## Licensing boundary

Upstream issues [#200](https://github.com/Gericom/GBARunner3/issues/200) and
[#208](https://github.com/Gericom/GBARunner3/issues/208) remain open. No
repository-wide license is selected or granted by this fork. Existing file- and
component-specific notices remain authoritative only for their own scope.

## Integration risks

- PR #205 can conflict with hicode mapping and initialization changes.
- RTC hardware cold-start and recovery coverage remains incomplete.
- Save-signature scanning across a 4 KiB cache boundary remains incomplete.
- The currently valid build evidence uses the pinned
  `devkitpro/devkitarm:20241104` toolchain; toolchain modernization remains
  separate work.
