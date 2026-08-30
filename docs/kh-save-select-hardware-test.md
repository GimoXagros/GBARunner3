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
2. The build creates a 64-byte armed marker beside the ROM during boot.
3. After the black screen appears, wait at least five seconds.
4. Press `Select` once. The build keeps sampling for another two seconds and
   then writes the ring. Wait another five seconds before powering down.
5. Power down normally, then inspect `.g3diag`. A 55,360-byte file is a complete
   capture. A 64-byte file proves that the path was writable but the trigger
   was not captured. A different size is an incomplete write.
6. Copy the `.g3diag` file beside the ROM back to the
   developer. The file contains the last 256 VBlank samples (about four
   seconds), register state, DMA event counts, and the last observed byte-wide
   SRAM access addresses. It contains no ROM, BIOS, VRAM,
   or save payload.
7. Rename or copy that `.g3diag` before starting the next variant. The decoder
   also reads the embedded variant flags to prevent A/B results being mixed up.

Decode it with:

```text
python tools/diagnostics/decode_g3diag.py path/to/game.g3diag
```

Hardware verification required.
