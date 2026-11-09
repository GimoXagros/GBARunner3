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
state until a full second elapses on the ARM7 clock. Its canonical offset uses
second `59`, so the next tick naturally carries into the minute. Normal writes,
reset and initialization replace the live state; status changes and
incomplete date/time transactions preserve it. Invalid BCD digits keep the
existing policy. The exact `E5` vector is externally grounded; the exhaustive
`60`–`79` checks verify the chosen extension, not independent hardware evidence
for every byte.

Existing 12/24-hour conversion, calendar arithmetic and explicit legacy state
adoption remain in use. No WSRTC source or WonderSwan register transport is
imported. Original legacy sidecars remain untouched; V2 records remain readable.

The rollover regression also exposed an existing weekday discontinuity at the
guest calendar's `2099` to `2000` wrap. Removing a 36,525-day cycle from epoch
seconds must not remove those days from weekday progression. A shared offset
correction now accounts for the removed days during reads, saving and loading
after offline elapsed time. This preserves both live rollover and restart
across the boundary without changing the file format or backward-host policy.
This is a calendar consistency finding, not a vector from the external suite.

## Write phase and restart state

The shared `run_test_subsecond_reset` vector writes `08:20:30`, waits 750 ms,
rewrites `08:20:31`, then expects `31` after another 525 ms. The former DS
integer-second offset could advance too soon. ARM7 now supplies a monotonic
clock using previously unused timer 2 at system-clock/1024. Timers 0, 1 and 3
remain owned by sound; ARM9 guest timers are untouched. The overflow counter
and pending IRQ snapshot extend the hardware counter, and both CPUs share a
checked 16-byte date/time plus tick sample. Fractional elapsed time uses the
33513982 Hz clock ratio, not a rounded 32768 Hz hardware rate.

Complete time/date writes and reset establish a new clock origin. Reads,
status changes and incomplete writes do not reset it. During the process,
elapsed time follows this clock even if ARM9 pauses or the host wall clock
changes. On restart, a new monotonic origin is established; saved ticks from
the previous boot are never compared with the new boot's counter.

Ordinary saves write version 3 using the existing `G3R2` magic, 200-byte layout
and three `.g3rtc2` journal paths. Byte 45 stores a pending BCD second or `FF`;
bytes 46–47 store elapsed fractional phase in units of 1/32768 second. The
legacy-adoption phase at byte 42 is separate and unchanged. V3 requires a ready
record, valid metadata and a canonical second of 59 when raw seconds are pending.
Expired raw metadata is cleared before saving.

Version 2 remains strict and readable; explicit legacy adoption still creates
V2 pending/ready records. Same-second restart from a verified V3 save restores
raw seconds and the remaining fraction; offline whole seconds advance the
canonical timestamp and expire pending raw seconds. Existing sequence,
readback, identity, lineage and terminal I/O-error rules remain in force.

Old V2-only builds classify any V3 slot as unsupported and stop before writing,
rather than silently selecting an older sibling. Keep pre-upgrade RTC backups
when rolling back to such a build; do not delete a newer slot to bypass this gate.

## Verification

The existing host harness executes production RTC/GPIO method bodies using
controlled DS seconds, monotonic ticks and synthetic FatFs. It covers the external `E5` vector,
the BCD range extension, repeated reads, replacement/aborted commands, mode
changes, reset, date rollover and canonical persistence. The pinned pre-fix
source and an eager-clamp mutation must fail the immediate-read assertion.
Century tests cover ordinary valid seconds as well as delayed correction,
repeated reads and saving/restarting on either side of the boundary.
Phase checks include the 750/525 ms vector, host-second crossings, preservation
across status/partial writes, same-second restart, metadata validation and an
old reader's unsupported-version gate. The old integer-clock implementation
must fail the write-phase assertion.

The existing linked ARM loader harness also drives the real `rio_write` entry
and reads the GPIO register window. CI builds both the application and test
ROM with the pinned devkitARM image, runs these checks and retains the existing
calendar, persistence, storage, boot/interworking and instruction regressions.
An additional linked ARM7 test executes timer setup, overflow handling and IPC
snapshots with pending/racing overflow flags and preserved IRQ state.
Consult the exact commit's nightly workflow for execution results.

## Remaining limits

- Offline elapsed time still comes from the DS wall clock's whole seconds.
  Exact fractional time while powered off is unknowable from that input: the
  saved fraction is rebased at boot, with up to about one second of offline
  phase uncertainty. Q15 persistence quantizes the saved phase by less than
  1/32768 second; it is not an oscillator calibration.
- The DS host clock's own century wrap is not tracked; its existing
  backward-clock policy remains in place. Guest calendar rollover tests use a
  normally advancing host clock.
- Timer overflow must be serviced before a second hardware wrap (about two
  seconds between wraps). Multiple wraps during an unusually long IRQ blackout
  cannot be recovered from the single pending flag. Physical interrupt latency,
  oscillator accuracy and console sleep have not been measured.
- The GBA build excludes the suite's Alarm/IRQ tests, and its IRQ adapter is
  marked TODO. This change does not implement or certify Alarm/IRQ behavior.
- No physical DS/GBA run, cartridge timing measurement, power-loss durability
  test, or complete 12-test ROM pass is claimed. Existing release tags and
  binaries retain their previous scope.
