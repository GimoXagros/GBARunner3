# Kingdom Hearts save-selection K runtime-state hardware test

This diagnostic follows the verified J saved-SPSR Thumb-dispatch fix. It does
not change VRAM, DMA, JIT, RTC, save-path, or high-ROM compatibility settings.
Its only purpose is to distinguish a live black display from a guest polling or
peripheral-completion failure before the save-selection screen.

Hardware verification is required.

## Files written

The build alternates two checksummed sidecars beside the selected ROM:

- `<ROM name>.g3diag.a`
- `<ROM name>.g3diag.b`

Each complete file is 15,680 bytes and contains emulator state only. It does
not contain ROM, BIOS, VRAM, or save payload data.

## Procedure

1. Keep the ROM and its existing `.sav` in their normal locations. Back up and
   remove older `.g3diag`, `.g3diag.a`, and `.g3diag.b` files beside the ROM.
2. Start `GBARunner3-KH-K-runtime-state.nds` through the same 3DS + DSpico path
   used for J.
3. Reach the Kingdom Hearts main menu and highlight `New Game`.
4. Press and release **Select once**. This only arms the in-memory ring; it does
   not write a file and must not pause the game.
5. Press **A once** to select `New Game`.
6. When the screen becomes black before the save-selection screen, do not press
   any more buttons. Wait at least 10 seconds.
7. Power the console down normally. Submit both new `.g3diag.a` and `.g3diag.b`
   files and report whether any audio was audible during the black screen.

The diagnostic writes a complete alternating checkpoint once per second after
step 5, so no dump hotkey is needed after the failure.

## Optional local validation

```text
python decode_g3diag.py "<ROM name>.g3diag.a" "<ROM name>.g3diag.b" -o kh-k-runtime.csv
```

The decoder selects the newest valid sequence, verifies its checksum, writes a
CSV, and summarizes PC variation, display modes, DMA starts, and live timers.

