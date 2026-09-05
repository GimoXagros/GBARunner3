# GBARunner3 RTC + ROM Hack Compatibility Build

The authoritative user and build documentation now lives in
[`README.md`](README.md), and the issue comparison lives in
[`TODO.md`](TODO.md).

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
