# GBARunner3

![GBARunner3 custom logo](logo.png)

GBARunner3 runs Game Boy Advance software on Nintendo DS-family hardware by
combining direct execution, instruction patching, and emulation of hardware that
cannot be exposed directly. It is still under development: compatibility varies
by title, console mode, storage device, launcher, and ROM revision.

This fork tracks the upstream [`Gericom/GBARunner3`](https://github.com/Gericom/GBARunner3)
project and adds the RTC and high-ROM compatibility work submitted upstream as
[pull request #210](https://github.com/Gericom/GBARunner3/pull/210).

## Current custom release

The current stable custom release is
[`custom-v0.1.2`](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.2).
The broken rc2 and rc4 release entries have been withdrawn.

- Release tag and merge commit: `custom-v0.1.2` at
  `dd3f44be5e9412ba29f3d831fc236dcc6016b71e`
- High-ROM fix implementation commit:
  `9b991ac9c89e1952b8573f4bf8bc9708bcade92b`
- `GBARunner3.zip` SHA-256:
  `13AE1E2252ECF2245AD2236FF13EBEA3BA558C7B4E6EA7FB4F021CB25834CE77`
- `GBARunner3.nds` SHA-256:
  `CC09916848C6FB92092DB15D5D8EBDA21F4543A63589804F44268D2D810601CE`
- Automated verification: ARM7, ARM9, bootstrap, application NDS, GoogleTest
  NDS, and linked high-ROM dispatch semantics build successfully
- Hardware verification: Nintendo 3DS in DS mode with DSpico passed the B8CJ
  route `Main Menu -> New Game -> Save Slot`

The immutable hardware regression baseline remains `custom-v0.1.0-rc5` at
commit `967730a0db710f9d90dbd70907223d3f75e25a81`, with NDS SHA-256
`E33F2818E8946EED2DB4BF8B653F81B1D48A554E2C4E9A90F2D82210F87FA9B0`.
That baseline was verified on Nintendo 3DS in DS mode using DSpico.

## Current release candidate

[`custom-v0.1.3-rc1`](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.3-rc1)
is the published Pre-release candidate for configuration and build hardening.
Its public ZIP has passed independent download, hash and content verification;
the exact source, workflow and hashes are recorded in
[CUSTOM_BUILD.md](CUSTOM_BUILD.md#custom-v013-rc1-release-candidate).
`custom-v0.1.2` remains the current stable release.

The RC includes strict external JSON patch-address validation and atomic
rejection of malformed arrays, expanded automated regressions, reproducible
pinned-toolchain builds, and guarded release uploads. EEPROM V124 and ROM-hack
source-profile documents are research/design only. Draft PRs
[#5](https://github.com/GimoXagros/GBARunner3/pull/5) and
[#6](https://github.com/GimoXagros/GBARunner3/pull/6), including their tests, are
excluded.

Hardware validation is pending; no new RC hardware pass has been completed.
The previous v0.1.2 B8CJ result ends at Save Slot. Slot selection, intro,
gameplay, save/restart/load, RTC cold start, interrupted-write recovery, and
wider compatibility remain open. See [RC build identity](CUSTOM_BUILD.md#custom-v013-rc1-release-candidate)
and [remaining work](TODO.md#custom-v013-rc1-release-status).

## Changes in this fork

- Enables the upstream high-ROM instruction-cache mapping work and fixes ARM
  B/BL/BX transitions across the 2 MiB linear ROM window.
- Resets high-ROM JIT metadata when an SD-cache block is reused.
- Preserves ARM/Thumb state for undefined instructions in mapped high-ROM code
  and selects the correct Thumb halfword.
- Resolves dynamic Thumb JIT metadata and patch writes to the loaded SD-cache
  backing block.
- Unmaps the hicode MPU region before whole instruction-cache invalidation.
- Implements cartridge GPIO RTC transactions using the DS clock through ARM7
  IPC.
- Persists RTC offset, weekday, status, and interrupt state in a validated
  per-ROM `.g3rtc` sidecar, with temporary and backup-file recovery.
- Searches high-ROM regions for save routines that need patching.
- Creates adjacent `.sav` files without modifying the launcher-owned ROM path,
  initializes new or short saves with `0xFF`, and checks initialization I/O.
- Uses UTF-8 FatFs long filenames, including Korean ROM filenames.
- Adds regression tests for ARM PC+8 branch calculation and high-ROM boundary
  handling.

## Installation

1. For the stable release, download `GBARunner3.zip` from `custom-v0.1.2`.
   For RC hardware testing, explicitly choose the published
   `custom-v0.1.3-rc1` Pre-release ZIP instead. Check that version's integrity
   information before copying the files.
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

`custom-v0.1.3-rc1` validates external patch-address strings
strictly: 1–8 hexadecimal digits with optional `0x`/`0X`. A malformed address or
non-string entry rejects that entire array and preserves its previous/default
value. See [patch-address format](docs/config-patch-addresses.md). The published
`custom-v0.1.2` binary retains its original behavior.

## Hardware verification

The v0.1.2 NDS above was verified on Nintendo 3DS in DS mode using DSpico with
Kingdom Hearts: Chain of Memories `[B8CJ][K]`:

```text
Main Menu -> New Game -> Save Slot
```

The black horizontal lines, screen flicker, and irregular audio clicks seen in
a rejected diagnostic build were absent in the other games checked during this
run. B8CJ exposed and verified a generic mapped high-ROM Thumb/JIT error; the
production fix contains no title-specific path.

The following B8CJ steps have not yet been verified with v0.1.2: slot selection,
intro completion, gameplay, save, restart, load, and a full playthrough.

### Historical rc5 regression baseline

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

The rc5 evidence covers normal startup and observed runtime only. It is not a
full-playthrough, exhaustive save-format, or exhaustive RTC certification.
Tales of the World: Narikiri Dungeon 2 is excluded because the tested patched
image is suspected to be malformed; its save behavior belongs to a separate
investigation.

## Known limitations

- RTC persistence now has format/corruption/time-transition regression coverage,
  but cold-start behavior and write recovery still require hardware verification
  on DSpico/3DS.
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

Clone recursively and use the official reference toolchain,
`devkitpro/devkitarm:20241104`, as in CI. The RC includes dependency-order fixes:
two clean builds each at serial (`-j1`), `-j2`, and `-j4` produced identical
application and test NDS files in the
[build audit](docs/build-reproducibility.md). These parallel-build results apply
to the corrected RC source. The latest toolchain remains an optional,
non-blocking experiment.

```sh
git clone --recursive https://github.com/GimoXagros/GBARunner3.git
cd GBARunner3
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkitarm:20241104 make -C code debug
```

The application output is `code/bootstrap/GBARunner3.nds`; the `debug` target
also builds the GoogleTest NDS. The v0.1.2 release source was built successfully
by the application and test targets in the pinned container. The `develop` build
and semantic test passed in
[CI build 33939033107](https://github.com/GimoXagros/GBARunner3/actions/runs/33939033107),
and the release packaging passed in
[CI build 33939047544](https://github.com/GimoXagros/GBARunner3/actions/runs/33939047544).

## Contributing and issue reports

Upstream issues are tracked at
[`Gericom/GBARunner3/issues`](https://github.com/Gericom/GBARunner3/issues).
Reports should include the exact GBARunner3 commit or NDS hash, console and mode,
launcher/storage path, ROM game code and revision, save type, reproduction steps,
and the first observable failure. Do not upload commercial ROMs or BIOS files.
