# Kingdom Hearts save-selection diagnostic

These builds investigate the B8CJ black screen after choosing `New Game`, at
the point where the save-file selection screen should appear. They are not a
release and do not contain a ROM or BIOS.

## Test order

Use the same ROM, BIOS, DSpico setup, SD card, and save file for each build.
Start with A, then test B through F one at a time:

| Build | Diagnostic change |
| --- | --- |
| A | Current `develop` behavior plus state capture |
| B | BG VRAM abort disabled |
| C | VRAM write buffer disabled |
| D | BG VRAM abort and VRAM write buffer disabled |
| E | JIT disabled |
| F | DMA fast path disabled |

For each build, record whether BIOS, title, menu, the first save-selection
frame, audio, and input continue to work. Do not mix files from different
variant directories.

## Capturing the black-screen state

1. Boot the game and choose `New Game`.
2. After the black screen appears, wait at least five seconds.
3. Press `L + R + Select` together once, then wait another five seconds.
4. Power down normally and copy the `.g3diag` file beside the ROM back to the
   developer. The file contains the last 256 VBlank samples (about four
   seconds) and register/counter state only. It contains no ROM, BIOS, VRAM,
   or save payload.

Decode it with:

```text
python tools/diagnostics/decode_g3diag.py path/to/game.g3diag
```

Hardware verification required.
