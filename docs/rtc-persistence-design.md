# Per-game RTC persistence design

Baseline: `custom-v0.1.0-rc5` at
`967730a0db710f9d90dbd70907223d3f75e25a81`

Status: implementation specification; hardware verification required

## Reproduction and root cause

`RomGpioRtc` keeps the following game-visible state only in ARM9 memory:

- the signed difference between the DS clock and the game clock;
- weekday correction;
- RTC status/control bits; and
- alarm/interrupt register state.

The constructor resets those fields on every GBARunner3 launch. A game can set
its RTC correctly during one run, but a full emulator exit discards the
difference and the next cold boot starts from the host clock again.

The supplied `GBARunner3(RTC NEW).nds` has SHA-256
`7CAC508A8D9AA9BD7E52BDD2EE5709EBDB757FAA8FE5206AC04FE557B42DB6E6`.
Its ARM9/ARM7 images differ from RC5 and it contains the current RTC GPIO code
paths, but it contains no RTC sidecar path or persistent-state marker. It is
therefore evidence for the runtime RTC protocol, not evidence that cold-boot
offset persistence is already implemented.

## Storage boundary

Use a sidecar adjacent to each ROM:

- primary: replace the ROM extension with `.g3rtc`;
- transaction file: `.g3rtc.tmp`;
- recovery file: `.g3rtc.bak`.

This leaves `.sav` size and cluster mapping unchanged. Different ROM paths get
different files. Each record also contains the GBA game code, ROM size, and an
FNV-1a hash of the complete 192-byte GBA header. A valid record whose identity
does not match the current ROM is ignored.

The path plus header identity prevents ordinary cross-game reuse without a
second full-ROM pass during boot. Replacing a ROM in place with another image
that has an identical full header and size remains a theoretical collision;
effective post-patch ROM identity belongs to the separate PR #205/Priority 6
lifecycle work and must not be guessed here.

## Version 1 record

The fixed 44-byte little-endian record contains:

- magic and format version;
- payload length;
- game code, ROM size, and header hash;
- monotonically increasing sequence number;
- host reference seconds since 2000-01-01;
- game RTC reference seconds since 2000-01-01;
- weekday offset, status/control, and alarm/interrupt state; and
- FNV-1a checksum over every preceding byte.

Invalid magic, version, length, identity, or checksum causes a safe fallback to
the host RTC defaults. A file error must not block ROM startup.

## Cold-boot restore

For a valid record:

1. If the current host time is at or after the stored host reference, advance
   the stored game reference by the elapsed host seconds.
2. If the host clock moved backwards, advance by zero seconds and rebase the
   game offset without moving the game clock backwards.
3. Normalize the game reference over the RTC's 2000-2099 cycle.
4. Reconstruct the signed game-minus-host offset and restore the weekday,
   status, and interrupt state.

This follows the elapsed-host-time and backward-clock behavior used by GameYob
and the reference-time/offset reconstruction used by mGBA, while retaining the
existing GBARunner3 RTC protocol implementation.

## Recoverable update

FatFs `f_rename` refuses to replace an existing destination. Use this
recoverable sequence:

1. write the complete record to `.g3rtc.tmp`;
2. `f_sync`, close, reopen, and validate the transaction file;
3. remove a stale `.g3rtc.bak`;
4. rename the primary to `.g3rtc.bak` when it exists;
5. rename `.g3rtc.tmp` to the primary;
6. remove the backup only after the new primary is in place.

On startup, validate primary, temporary, and backup records and select the
valid matching record with the greatest sequence number. Loading a recovery
record schedules one deferred rewrite of the canonical primary. This is a
recoverable two-phase replacement on FAT; it must not be described as a
filesystem-level atomic replace.

RTC GPIO writes only mark state dirty. Filesystem work is deferred through the
existing VBlank save writer, and initial state is loaded beside ordinary save
initialization before VM execution. No hicode/JIT file is modified.

## Automated regression cases

The RTC persistence change must add tests for:

- exact format size, magic, version, and payload length;
- valid encode/validate round trip;
- rejection of corruption and truncated/incorrect format metadata;
- rejection of game-code, ROM-size, and header-hash mismatches;
- forward host-time progression;
- backward host-time rebasing without a large forward wrap;
- RTC-cycle wrapping;
- sequence selection across primary/temp/backup candidates; and
- independent identities for two games.

File replacement and real power-loss behavior require target filesystem and
hardware validation; the pure format and time math are unit-testable.

## Hardware verification

Hardware verification required on Nintendo 3DS + DSpico:

1. Pokémon Ruby, Sapphire, and Emerald: set/read RTC, save, fully exit, wait,
   cold boot, and confirm progression.
2. Move the host clock forward and backward, then repeat the cold boot.
3. Cross midnight, a month boundary, and a leap-day boundary where practical.
4. Confirm two RTC games use different `.g3rtc` files and do not exchange
   offsets.
5. Corrupt the sidecar checksum and confirm the game boots with host/default
   RTC rather than hanging.
6. Re-run all seven RC5 hardware baselines, including the Kingdom Hearts
   `EA7B4FFE -> 0x09ED4000` high-ROM case.
