# S-3511A protocol comparison — 2026-11-09

[`ws-rtc-test` at 071687a](https://codeberg.org/asie/ws-rtc-test/src/commit/071687a0ba75f864680f5745a1a49b160ca0e076/README.md)
targets the S-3511A on both GBA and WonderSwan cartridges. Its GBA adapter sends
commands through cartridge GPIO; the shared tests therefore provide relevant
high-level expectations for GBARunner3. The README credits real GBA testing,
but that is external evidence, not a hardware run of this change.

## Confirmed discrepancy and correction

[`run_test_datetime_bounds_sec`](https://codeberg.org/asie/ws-rtc-test/src/commit/071687a0ba75f864680f5745a1a49b160ca0e076/src/main.c#L246)
writes `E5` seconds, expects immediate `65`, then `00` with a minute carry on
the next tick. GBARunner3 instead immediately clamped the write to `00`.
Removing the clamp alone would convert `65` to an epoch offset and immediately
read back `01:05`, also incorrect.

The correction retains masked, out-of-range BCD seconds (`60`–`79`) in transient
state until the DS host second changes. Its canonical offset uses second `59`,
so the next host second naturally carries into the minute. Normal writes,
reset and initialization replace the transient state; status changes and
incomplete date/time transactions preserve it. Invalid BCD digits keep the
existing policy. The exact `E5` vector is externally grounded; the exhaustive
`60`–`79` checks verify the chosen extension, not independent hardware evidence
for every byte.

Existing 12/24-hour conversion, calendar arithmetic and explicit legacy state
adoption remain in use. No WSRTC source or WonderSwan register transport is
imported. The 200-byte V2 file format and original legacy sidecars are unchanged.

The rollover regression also exposed an existing weekday discontinuity at the
guest calendar's `2099` to `2000` wrap. Removing a 36,525-day cycle from epoch
seconds must not remove those days from weekday progression. A shared offset
correction now accounts for the removed days during reads, saving and loading
after offline elapsed time. This preserves both live rollover and restart
across the boundary without changing the file format or backward-host policy.
This is a calendar consistency finding, not a vector from the external suite.

## Verification

The existing host harness executes production RTC/GPIO method bodies using
controlled DS seconds and synthetic FatFs. It covers the external `E5` vector,
the BCD range extension, repeated reads, replacement/aborted commands, mode
changes, reset, date rollover and canonical persistence. The pinned pre-fix
source and an eager-clamp mutation must fail the immediate-read assertion.
Century tests cover ordinary valid seconds as well as delayed correction,
repeated reads and saving/restarting on either side of the boundary.

The existing linked ARM loader harness also drives the real `rio_write` entry
and reads the GPIO register window. CI builds both the application and test
ROM with the pinned devkitARM image, runs these checks and retains the existing
calendar, persistence, storage, boot/interworking and instruction regressions.
Consult the exact commit's nightly workflow for execution results.

## Remaining limits

- DS RTC IPC supplies whole seconds. The shared test's subsecond write-phase
  reset (rewrite after 750 ms, then read after 525 ms) remains unresolved. A
  write near a DS second boundary can tick earlier than one elapsed second.
  The DS host clock's own century wrap is not tracked; its existing
  backward-clock policy remains in place. Guest calendar rollover tests use a
  normally advancing host clock.
- Raw out-of-range seconds are not serialized. Saving and restarting within
  the same host second restores canonical `59`, not transient `65`. Continuing
  in the same process preserves the raw readback; later ticks carry normally.
- The GBA build excludes the suite's Alarm/IRQ tests, and its IRQ adapter is
  marked TODO. This change does not implement or certify Alarm/IRQ behavior.
- No physical DS/GBA run, cartridge timing measurement, power-loss durability
  test, or complete 12-test ROM pass is claimed. Existing release tags and
  binaries retain their previous scope.
