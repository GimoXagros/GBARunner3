# Custom RTC persistence

This custom branch keeps a GBA cartridge RTC independent from the Nintendo DS
system clock after a game changes it.

## Behaviour

- The RTC advances by elapsed host time between launches.
- If the host clock moves backwards, the emulated RTC does not jump backwards;
  its saved baseline is safely rebased instead.
- A game's RTC control/alarm registers and weekday adjustment are retained.
- The 100-year GBA RTC calendar wraps safely instead of overflowing the BCD
  conversion table.
- Incomplete date/time serial writes are discarded. A complete write is
  committed when chip select goes low.
- RTC file I/O is deferred to the existing VBlank save path, outside the GPIO
  transaction handler.
- Existing RTC state is loaded during the normal save-initialization phase;
  GPIO setup after the splash screen performs no filesystem operations.

The design follows GameYob's elapsed-host-time and backward-clock handling, and
mGBA's persisted RTC baseline/offset model. GBARunner3 stores the data in a
separate file because appending emulator metadata would change the expected GBA
save size. The custom extension also avoids overwriting another emulator's
generic `.rtc` sidecar.

## Files

For `game.gba`, regular save data remains in `game.sav`. RTC metadata is created
only after the game changes an RTC register and is stored in `game.g3rtc`.

The `.g3rtc` file is a versioned 28-byte record with a magic value, host and game
clock baselines, weekday offset, control/alarm registers, and an integrity
checksum. A missing, truncated, or invalid record falls back to the DS system
clock without altering the `.sav` file.

## Hardware check

1. Copy the custom `GBARunner3.nds` to the SD card used by DSpico.
2. Start an RTC-capable GBA game and change its clock.
3. Save in-game, return to the 3DS menu or power off normally, then wait at least
   two minutes.
4. Start the same game again and confirm that its clock advanced from the value
   set in step 2.
5. Confirm that both `game.sav` and `game.g3rtc` exist beside the ROM and that the
   existing save still loads.

The designated Korean-patched Kingdom Hearts ROM remains the save/ROM-hack
regression target. Rockman EXE 2 must not be used as a test ROM.
