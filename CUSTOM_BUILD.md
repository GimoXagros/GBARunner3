# GBARunner3 RTC + ROM Hack Compatibility Build

This branch is a custom GBARunner3 build for testing Korean-patched GBA ROM hacks
through Pico Launcher/DSpico on Nintendo 3DS systems running in DS mode.

## Changes

- Integrates the upstream `feature/rom-gpio` RTC implementation with the current
  `develop` branch.
- Resets JIT and auxiliary metadata whenever a 4 KiB high-ROM cache block is
  reused. This prevents code from a newly mapped 32 MiB ROM region from inheriting
  the processed state of an evicted region.
- Builds the `.sav` path in owned memory instead of overwriting the launcher-provided
  ROM argument.
- Opens or creates save files in one operation, initializes new or short saves with
  `0xFF`, and checks seek, write, sync, read, and cluster-map results.
- Retains the current UTF-8 FatFs configuration for Korean long filenames.

## Test target

- Game code: `B8CJ`, version `00`
- ROM size: 32 MiB
- SHA-256: `0B3670F3A08BD763D153ECE802FEDF71D03DF40D0B2E004DE2835856266586C3`
- Detected save tag: `SRAM_F_V103` at ROM offset `0x01D46640`
- Expected save: adjacent `.sav`, 32 KiB, initially filled with `0xFF`

The test ROM is copyrighted and is intentionally not included in this repository
or any release artifact. Rockman EXE 2 diagnostic ROMs are explicitly excluded from
the test matrix and no game-specific configuration is supplied for them.

## Hardware verification

The build must be launched from a `.gba` Pico Launcher file association so the ROM
path is supplied as `argv[1]`. Verify that the Korean-patched target boots and that
an adjacent 32 KiB `.sav` is created, updated in-game, and loaded after a cold
restart. Emulator-only checks do not substitute for this DSpico hardware test.

## RTC limitation

The integrated upstream RTC branch emulates the cartridge GPIO RTC against the DS
clock. Per-game RTC offsets are not yet persisted across emulator restarts.
