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

## RTC persistence development candidate

- Commit: `a8ee9be2721ef7c66a2c899c62453139580fd3be`
- `GBARunner3.nds` SHA-256:
  `14FD5FB8AAB3A6236CAAAEBEECBB3E2615D981D054472896636F53DDB8F4FC32`
- CI: <https://github.com/GimoXagros/GBARunner3/actions/runs/33248527389>

This candidate persists per-game RTC state in `.g3rtc` sidecars and includes the
custom NDS banner icon. The application and test NDS compile successfully, but
hardware verification is required before replacing the rc5 release baseline.
