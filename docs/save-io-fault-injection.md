# Save I/O fault injection — follow-up 2026-09-05

This draft now passes the original 14 failed invariants at the FatFs seam.
The expanded host suite compiles actual Save.cpp, ARM7 save service and exit
handling code. It reports 73 PASS with **no accepted failures**; CI runs
`test_save_io_host.py --require-fixed`. It models sticky FIL errors, combined
operation/close failures and a finite operation schedule. It is not a physical
SD, FAT metadata durability or simultaneous CPU model.

## Behavior in this candidate

- Append 0xFF bytes before building the fast-seek cluster map. Removing the
  seek-to-final-size preallocation means a failed append does not leave an
  apparently initialized full-size hole. Existing prefixes and oversized saves
  are preserved. A power cut during FAT metadata updates is still unproven.
- Retain a live file object when cleanup close fails; do not memset or reopen
  over it on another initialization attempt. Check rewind and buffer bounds.
- Check seek, write result, byte count and sync before declaring CLEAN. A failed
  attempt ends in ERROR (4); the existing CLEAN/DIRTY/WAIT/WRITE values and shared
  struct offsets do not change. ERROR disables automatic VBlank retries.
- Keep failed byte writes latched across later successful syncs; a successful
  sync cannot reconstruct a byte payload that was never written.
- The explicit `sav_retryFailedWrite(originalSavePath)` entry point supports
  only the buffered SRAM path, while emulation is stopped. It closes/reopens
  the existing file, checks size and cluster map, then writes and syncs RAM.
  It never clears FIL.err by hand, truncates or reloads disk over pending RAM.
  This API is not wired to a user-facing recovery control yet and may be removed
  from the production link by dead-code elimination. No automatic retry or
  EEPROM/FLASH byte-payload reconstruction is claimed.
- ARM7 returns Clean/Pending/Error separately. A failed save cancels the current
  reset/power-off request, restores the existing normal volume and retains the
  error instead of waiting indefinitely or treating failure as a durable save.
  This changes the error-path exit policy and **requires hardware review**.

## Scenario / Expected / Before / After

| Scenario | Expected | Prior draft d7d9693 | Follow-up |
| --- | --- | --- | --- |
| normal create, short extension, oversized existing | initialized suffix / preserved data | PASS | PASS |
| deferred seek/write/short-write/sync/full/read-only | terminal error, retained RAM | FAIL (6) | PASS (6) |
| retry after each failure | explicit reopen/write/sync without losing RAM | FAIL (6) | PASS (6), stopped-emulation API |
| interrupted append then retry | 0xFF suffix with old prefix unchanged | FAIL | PASS |
| standalone sync error | visible error | FAIL | PASS |
| byte error, short read, invalid offset | deterministic 0xFF / no wrong write | PASS | PASS |
| successful flush after lost byte | retain error | untested | PASS |
| init read+close failure then repeated init | retain live handle until successful close | untested | PASS |
| retry close/open/map/write/sync failures | preserve error and RAM, finite attempt | untested | PASS |
| 120 VBlank / ARM7 updates after failure | no automatic I/O retry | error lost before | PASS, ERROR retained |
| ARM7 Clean/Pending/Error, buffered and file-backed | explicit acknowledgment; no false clean exit | error unavailable | PASS |
| init/Nitro/shared-size bounds | no memory overrun | incomplete | PASS |

`test_save_io_elf.py` executes actual linked ARM9 byte/deferred functions and
ARM7 Update/Flush methods with controlled FatFs outcomes. It checks stack and
callee-saved registers, range rejection and error latching. CP15 and nested IRQ
boundaries are mocked; no physical cache coherence claim follows. Full retry
reopening and initialization are tested at the source/FatFs host seam, not as
complete target FAT transactions.

## Remaining gates — keep DRAFT

The follow-up in PR #5 compiles actual ARM7 storage handlers, ARM9 FsIpc,
FatFs diskio and SdCache. It demonstrates that DLDI/DSi read/write errors are
still discarded below this suite's FatFs seam. Four driver-to-diskio failure
propagations and two stale-signature rejection cases fail. Physical errors
therefore cannot yet reliably reach this candidate's ERROR state.

Fixing that requires a separately reviewed transaction-result channel: publish
ARM7 result before completion acknowledgment, capture it in the matching ARM9
wait token before nested transactions reuse the command, translate it in
diskio and prevent failed cache publication/permanent patch allocation. JIT,
DMA and ordinary cache callers must have an explicit non-null failure policy.
No guessed shared storage/IRQ protocol change is included here.

Other gates: stopped-emulation recovery UI and original-path/medium identity,
actual simultaneous ARM7/ARM9 behavior, reset/power-off cancellation on hardware,
FAT metadata/power-cut recovery, and complete target retry/cleanup transactions.
The 73 passing tests do not close these gates. Save protocol and SWI signatures
are unchanged. This candidate is not part of develop or custom-v0.1.2.
