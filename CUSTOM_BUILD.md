# GBARunner3 RTC + ROM Hack Compatibility Build

The authoritative user and build documentation now lives in
[`README.md`](README.md), and the issue comparison lives in
[`TODO.md`](TODO.md).

## custom-v0.1.3-rc1 release candidate

This is a Pre-release candidate for configuration and build hardening.
`custom-v0.1.2` remains the current stable release. The release-preparation
change updates documentation only; runtime, configs and submodule revisions
are unchanged from audited develop `504a2d67177d6e4432c51addfeabaa07b9996654`.

- Included runtime change since v0.1.2: strict 1–8-digit hexadecimal JSON
  patch-address strings with optional `0x`/`0X`, complete rejection of malformed
  arrays, and atomic replacement preserving previous/default values on rejection.
- Test/build changes: 304 configs / 2,513 unchanged addresses, serializer
  ASan/UBSan, 19 linked ARM parser cases, 45 linked JIT/address/cache checks,
  existing hicode dispatch and negative controls, EEPROM source-selection tests,
  repeated pinned serial/`-j2`/`-j4` builds, modernized Actions and guarded upload.
- EEPROM V124 and ROM-hack source-profile work is research/design only.
- Draft PR #5 at `245a0dba82cef259f897002555221376f9ab97f1` and Draft PR #6 at
  `7cdc3bf35032515573ad1888f9eeb80bfea19150` are excluded from the audited base;
  neither head is an ancestor, their changed production files retain the
  v0.1.2 blobs, and their added production/test files are absent.
- Reference toolchain: `devkitpro/devkitarm:20241104`.
- Baseline Nightly: [33944804396](https://github.com/GimoXagros/GBARunner3/actions/runs/33944804396), success.
- Reference application NDS SHA-256:
  `9968bb423430b2fcfc6aacec70a5c2e5603f711952c6eb2d6c57fbfac287a3b2`
- Reference test NDS SHA-256:
  `50cce7e4ee4f5ae5fd814d0dea14edf39a0ecb4c017af5395cb327d32b713de7`

Published and independently verified public release:

- [Release tag `custom-v0.1.3-rc1`](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.3-rc1):
  Pre-release (`prerelease: true`, `draft: false`).
- Release-preparation [PR #12](https://github.com/GimoXagros/GBARunner3/pull/12)
  merged normally; exact source and tag commit:
  `6503f9bd1e5143904be3769f930db9fd3fd8f466`.
- Preparation Nightly: [33954976173](https://github.com/GimoXagros/GBARunner3/actions/runs/33954976173), success.
- Build reproducibility: [33954977580](https://github.com/GimoXagros/GBARunner3/actions/runs/33954977580),
  all six pinned builds passed and retained the two reference NDS hashes above.
- Release-source Nightly: [33955459904](https://github.com/GimoXagros/GBARunner3/actions/runs/33955459904), success.
- Build release: [33955691014](https://github.com/GimoXagros/GBARunner3/actions/runs/33955691014),
  exact tag/source, build, package and automatic upload all passed.
- Public `GBARunner3.zip` SHA-256:
  `a2cbd9c0a6b70f1409e80e403bae71803b017fc4fc1bb9fd000696cb97dbe394`.
- Packaged `GBARunner3.nds` SHA-256:
  `9968bb423430b2fcfc6aacec70a5c2e5603f711952c6eb2d6c57fbfac287a3b2`.
- Fresh public-asset download: GitHub API digest matches; 304 config files are
  byte-identical to tagged source; only the top-level application NDS and
  `_gba/configs` files are present. No ROM, BIOS, save, ELF, MAP, test NDS,
  temporary log, diagnostic payload or source tree is included.

This integrity record was added after the RC tag through a separate
documentation PR. The immutable tag remains at the source commit above;
its release-preparation documents recorded publication as pending. This later
record changes no runtime, config, submodule or released binary.
The full pinned builds and sanitizer tests ran on GitHub Actions because the
local Windows host has no Docker/WSL or host C++ compiler; downloaded artifacts
and linked ARM routines were also checked locally. The optional latest-image
failure is the previously documented libtwl/calico `setVectorBase` mismatch.

Hardware scope has not expanded. The earlier v0.1.2 B8CJ observation is
`Main Menu -> New Game -> Save Slot`; no new RC hardware pass is complete.
Slot selection, intro, gameplay, save/restart/load, RTC cold start,
interrupted-write recovery and wider compatibility remain unverified.
Physical SD-error propagation and complete save recovery are not claimed.
Existing `LICENSE.md`, file notices and submodule terms continue to apply;
this RC does not add a repository-wide license.

## Verified build identity

- Tag: `custom-v0.1.0-rc5`
- Commit: `967730a0db710f9d90dbd70907223d3f75e25a81`
- `GBARunner3.nds` SHA-256:
  `E33F2818E8946EED2DB4BF8B653F81B1D48A554E2C4E9A90F2D82210F87FA9B0`
- Environment: Nintendo 3DS in DS mode using DSpico

The reported hardware pass covers normal startup and observed runtime for the
seven Korean-patched revisions listed in the README. It does not establish a
full-playthrough, every save protocol, or every RTC transition.

Tales of the World: Narikiri Dungeon 2 is excluded from this emulator regression
claim because the supplied patched image is suspected to be malformed. Its save
behavior will be investigated separately. No commercial ROM, patched ROM, save,
or BIOS is included in this repository or release.

## custom-v0.1.1 release identity

- Tag: `custom-v0.1.1`
- Binary-producing implementation commit:
  `a8ee9be2721ef7c66a2c899c62453139580fd3be`
- `GBARunner3.nds` SHA-256:
  `14FD5FB8AAB3A6236CAAAEBEECBB3E2615D981D054472896636F53DDB8F4FC32`
- CI: <https://github.com/GimoXagros/GBARunner3/actions/runs/33248527389>

This release persists per-game RTC state in `.g3rtc` sidecars and includes the
custom NDS banner icon. The application and test NDS compile successfully. The
rc5 binary remains the immutable hardware regression baseline until the new RTC
cold-start and recovery paths complete DSpico/3DS verification.

## custom-v0.1.2 release identity

- Tag: `custom-v0.1.2`
- Tag/release merge commit:
  `dd3f44be5e9412ba29f3d831fc236dcc6016b71e`
- Binary-producing implementation commit:
  `9b991ac9c89e1952b8573f4bf8bc9708bcade92b`
- `GBARunner3.zip` SHA-256:
  `13AE1E2252ECF2245AD2236FF13EBEA3BA558C7B4E6EA7FB4F021CB25834CE77`
- Packaged and hardware-tested `GBARunner3.nds` SHA-256:
  `CC09916848C6FB92092DB15D5D8EBDA21F4543A63589804F44268D2D810601CE`
- Candidate CI: <https://github.com/GimoXagros/GBARunner3/actions/runs/33936711193>
- Final PR CI: <https://github.com/GimoXagros/GBARunner3/actions/runs/33938862970>
- `develop` CI: <https://github.com/GimoXagros/GBARunner3/actions/runs/33939033107>
- Release CI: <https://github.com/GimoXagros/GBARunner3/actions/runs/33939047544>

This release fixes mapped high-ROM Thumb undefined dispatch, resolves Thumb JIT
metadata and patch writes to the loaded SD-cache backing block, and unmaps the
hicode MPU region before whole instruction-cache invalidation. Nintendo 3DS +
DSpico hardware testing passed `[B8CJ][K]` from Main Menu through New Game to the
Save Slot screen. The black scanlines, screen flicker, and irregular audio clicks
introduced by the rejected diagnostic build were absent. Extended progression
and wider compatibility checks are tracked in `TODO.md`. The implementation
commit identifies the tested runtime change; the release tag points to the later
merge commit that adds release documentation without changing the produced NDS.
