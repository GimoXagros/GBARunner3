# Kingdom Hearts save-selection I high-ROM Thumb dispatch hardware test

This diagnostic build tests the generic high-ROM undefined-dispatch correction
identified from the checksummed H hardware trace. It is not a release candidate.

## Fix under test

The H trace reached a JIT-patched Thumb `BX r0` at guest address `0x09ED3572`.
The high-ROM mapping handler treated that regular Thumb undefined trap as a
32-bit ARM instruction at `0x09ED3570`, producing `0xB100BC01` and ending in
`armJitNotImplemented()`. The I build dispatches a non-miss high-ROM undefined
exception according to the saved guest Thumb bit.

No game-code check, ROM-offset skip, save workaround, or video workaround is
included. Existing RTC, save-path, low-ROM mapping, high-ROM mapping, and SD
cache behavior remain unchanged.

## Exact hardware procedure

1. Copy `GBARunner3-KH-I-hicode-thumb-dispatch.nds` and `_gba/configs` to the
   same DSpico layout used for H.
2. Back up and remove older `.g3diag`, `.g3diag.a`, and `.g3diag.b` sidecars
   next to the patched `B8CJ` ROM. Keep its `.sav` and `.g3rtc` files.
3. Boot the I NDS and the same patched `B8CJ` ROM.
4. At the Main Menu, press **Select once**, wait one second, and choose
   **New Game** normally.
5. Record whether the Save Slot selection screen appears.
6. If it appears, select a slot and verify that the next screen or gameplay
   transition continues. If it remains black, wait at least five seconds.
7. Shut the console down normally, then submit both new `.g3diag.a` and
   `.g3diag.b` files plus the observed screen result.

Also report whether the title/menu still booted normally and whether Select
caused any stall. Do not submit the ROM, BIOS, save, or RTC files.

**Hardware verification required.**
