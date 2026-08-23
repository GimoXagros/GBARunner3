# RTC boot regression

Issue ID: `RTC-BOOT-001`

## Reproduction baseline

- Environment: DSpico on Nintendo 3DS hardware
- Passing build: `custom-v0.1.0-rc1` (`ef16647`), per user report that
  previously working ROMs launched normally
- Failing build: `custom-v0.1.0-rc2` (`d6c701e`)
- Observation: the GBARunner3 logo appears, then the display remains black and
  previously working ROMs do not start
- Test policy: the Korean-patched Kingdom Hearts ROM is the ROM-hack/save
  regression target; Rockman EXE 2 is excluded

## Boundary and hypotheses

The failure first appeared with RTC persistence. Two rc2 paths could affect a
non-RTC game before its first visible frame:

1. The `.g3rtc` file was opened from late GPIO initialization after the splash
   screen and VM memory setup.
2. The save-check VBlank path called a C++ RTC function every frame while an
   ordinary SRAM write was waiting for its debounce interval.

Neither hypothesis is confirmed without a hardware comparison. Build success
does not establish runtime behavior on DSpico.

## Candidate correction

- Move RTC state loading next to ordinary `.sav` initialization, before the
  splash screen is removed.
- Restore the rc1 VBlank save-state check exactly for non-RTC games.
- When RTC state actually changes, request one write through the existing
  `GBA_SAVE_STATE_WRITE` path; non-RTC frames do not call RTC code.
- Keep `.sav` and `.g3rtc` separate.

## Closure requirement

Do not mark this issue fixed or publish the candidate as rc3 until the exact
candidate NDS launches at least one previously working ROM on DSpico/3DS. Then
check the Korean-patched Kingdom Hearts save path and an RTC-capable game as
separate regressions.
