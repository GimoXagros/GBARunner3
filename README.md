# GBARunner3

GBARunner3 runs Game Boy Advance software on Nintendo DS-family hardware by
combining direct execution, instruction patching, and emulation of hardware that
cannot be exposed directly. It is still under development: compatibility varies
by title, console mode, storage device, launcher, and ROM revision.

This fork tracks the upstream [`Gericom/GBARunner3`](https://github.com/Gericom/GBARunner3)
project and adds the RTC and high-ROM compatibility work submitted upstream as
[pull request #210](https://github.com/Gericom/GBARunner3/pull/210).

## Current custom release

The current supported custom build is
[`custom-v0.1.0-rc5`](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.0-rc5).
The broken rc2 and rc4 release entries have been withdrawn.

- Source commit used for hardware verification:
  `967730a0db710f9d90dbd70907223d3f75e25a81`
- `GBARunner3.nds` SHA-256:
  `E33F2818E8946EED2DB4BF8B653F81B1D48A554E2C4E9A90F2D82210F87FA9B0`
- Verified environment: Nintendo 3DS in DS mode using DSpico

## Changes in this fork

- Enables the upstream high-ROM instruction-cache mapping work and fixes ARM
  B/BL/BX transitions across the 2 MiB linear ROM window.
- Resets high-ROM JIT metadata when an SD-cache block is reused.
- Implements cartridge GPIO RTC transactions using the DS clock through ARM7
  IPC.
- Searches high-ROM regions for save routines that need patching.
- Creates adjacent `.sav` files without modifying the launcher-owned ROM path,
  initializes new or short saves with `0xFF`, and checks initialization I/O.
- Uses UTF-8 FatFs long filenames, including Korean ROM filenames.
- Adds regression tests for ARM PC+8 branch calculation and high-ROM boundary
  handling.

## Installation

1. Download the rc5 ZIP from the current custom release.
2. Copy `GBARunner3.nds` to the location expected by your launcher.
3. Merge the included `_gba/configs` directory into `/_gba/configs` on the SD
   card.
4. Place a legally obtained GBA BIOS at `/_gba/bios.bin`.
5. Launch a `.gba` file through a frontend that passes the ROM path to
   GBARunner3. The tested setup used a DSpico/Pico Launcher file association.

GBARunner3 has no bundled commercial ROMs or BIOS. If a frontend does not pass
a ROM path, the current fallback path is `/rom.gba`.

## Configuration

Global settings are read from `/_gba/gbarunner3.json`. Per-title settings use
`/_gba/configs/GAMECODEVV.json`, where `GAMECODE` is the four-character GBA game
code and `VV` is the two-digit revision.

The current parser supports display placement and correction, JIT and cache
switches, manual JIT/self-modifying-code patch addresses, BIOS-intro skipping,
DS-mode ARM9 clock selection, and forced save type. See the JSON files in
[`configs`](configs) for per-title examples.

## Hardware verification

The following Korean-patched revisions were reported to start and run normally
with the exact rc5 NDS above:

| Game | Code | Tested revision |
| --- | --- | --- |
| Rhythm Tengoku | `BRIK` | Rev.1 / 1.34.1.1 |
| Made in Wario | `AZWJ` | Arumi / 2012-12-31 |
| Tales of the World: Narikiri Dungeon 3 | `B3TJ` | 1.1 |
| Fire Emblem: The Sacred Stones | `BE8K` | 0.621.1 / 2024-09-22 |
| Pokémon FireRed | `BPRE` | 2026-06-13 |
| Pokémon Emerald | `BPEE` | 2026-06-13 |
| Kingdom Hearts: Chain of Memories | `B8CJ` | Iyagi patch |

This evidence covers normal startup and observed runtime only. It is not a
full-playthrough, exhaustive save-format, or exhaustive RTC certification.
Tales of the World: Narikiri Dungeon 2 is excluded because the tested patched
image is suspected to be malformed; its save behavior belongs to a separate
investigation.

## Known limitations

- Per-game RTC offsets are not persisted across emulator restarts yet.
- Several save implementations and region/ROM-hack combinations still require
  issue-specific hardware retesting.
- Some upstream compatibility, timing, sound, JIT, DMA, and application-feature
  issues remain open.
- The upstream repository has not declared a repository-wide license. Read
  [`LICENSE.md`](LICENSE.md) before modifying or redistributing source or
  binaries.

The issue comparison and prioritized work list are maintained in
[`TODO.md`](TODO.md).

## Building

Clone recursively and use the same pinned devkitARM container as CI. Do not use
parallel `make` until the dependency-order issues are fixed.

```sh
git clone --recursive https://github.com/GimoXagros/GBARunner3.git
cd GBARunner3
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkitarm:20241104 make -C code debug
```

The application output is `code/bootstrap/GBARunner3.nds`; the `debug` target
also builds the GoogleTest NDS. The exact rc5 release commit was built
successfully by both the application and test targets in
[CI](https://github.com/GimoXagros/GBARunner3/actions/runs/32963024490).

## Contributing and issue reports

Upstream issues are tracked at
[`Gericom/GBARunner3/issues`](https://github.com/Gericom/GBARunner3/issues).
Reports should include the exact GBARunner3 commit or NDS hash, console and mode,
launcher/storage path, ROM game code and revision, save type, reproduction steps,
and the first observable failure. Do not upload commercial ROMs or BIOS files.
