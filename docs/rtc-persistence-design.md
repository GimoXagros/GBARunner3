# Per-game RTC persistence history

This document records the original version-1 sidecar design and its limitations.
The current PR #16 compatibility policy is defined in
[`rtc-legacy-compatibility.md`](rtc-legacy-compatibility.md); that contract takes
precedence wherever this historical record differs.

## Version 1 record and path

The original persistence implementation uses a 44-byte version-1 record beside
the ROM: `.g3rtc`, `.g3rtc.tmp`, and `.g3rtc.bak`. The record contains the GBA
game code, ROM size, an FNV-1a hash of the complete 192-byte GBA header, a
sequence, host and game seconds since 2000-01-01, weekday correction, status,
interrupt state, and a checksum.

Version 1 has an important compatibility limit: old and corrected builds can
both produce version-1 records, and the format does not identify which BCD time
conversion created them. Old timestamps may have lost tens bits in seconds,
minutes, or hours. There is no unique inverse for those timestamps and no
reliable record of elapsed time while the emulator was closed.

PR #16 therefore preserves every legacy record byte for byte. It never subtracts
an old host timestamp from a corrected host timestamp, guesses the original
clock from a FAT modification time, silently rewrites v1, or starts a fresh RTC
when a legacy or error state is present. See the compatibility contract for the
explicit snapshot-adoption path, v2 namespace, startup gate, rollback behavior,
and current write-selection rules.

## Original implementation notes

The original implementation reconstructed game time by adding nonnegative
elapsed host seconds to the saved game seconds, rebasing without advancement
when the host clock moved backwards, and normalizing within the RTC 2000–2099
cycle. It rotated the transaction and recovery files through FatFs rename and
delete operations. Those rules describe the v1 implementation and are not the
current v2 write contract.

The RTC GPIO protocol remains separate from persistence. GPIO writes mark RTC
state dirty; filesystem work is scheduled through the existing save writer.
Building a target NDS does not establish a hardware cold-boot, SD durability, or
power-loss guarantee.

## Verification status

PR #16 adds a new compatibility implementation and independent tests. Its
current candidate results are recorded in
[`rtc-pr16-validation.md`](rtc-pr16-validation.md). NDS/DSi-compatible hardware
testing must be recorded separately in
[`RTC-PR16-HARDWARE.md`](RTC-PR16-HARDWARE.md); until performed, hardware
verification remains NOT RUN.
