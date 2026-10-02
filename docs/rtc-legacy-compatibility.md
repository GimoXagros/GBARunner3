# PR16 RTC compatibility contract

Status: implemented and included in stable custom-v0.1.5 via merged PR #16.
The user reported normal boot/save and Pokemon Emerald elapsed RTC on 3DS +
DSpico with candidate `22fb48d`; physical media faults and legacy migration
failure scenarios remain unverified on hardware. See the [user guide](releases/RTC-v0.1.5.md)
and [release record](releases/custom-v0.1.5.md). The contract below remains the
technical source of truth; its original PR implementation baseline was `53c9545`.

## Information we can and cannot preserve

Version1 uses a 44-byte record in `.g3rtc`, `.g3rtc.tmp`, `.g3rtc.bak`.
Both host and game timestamps can have been calculated by the lossy old BCD
conversion. Corrected experimental builds also wrote version1; the version does
not prove provenance. There is no unique inverse or trustworthy historical
offline elapsed time. Never use FAT modification times to reconstruct it.

We preserve the entire legacy file set byte for byte. A validated stored game
seconds value, weekday offset, status and interrupt registers can be explicitly
adopted as a new snapshot. This is not recovery of the original real clock.

## One explicit transition path

Implement an offline tool, `tools/rtc_migrate.py`. Its default operation is
inspect/dry-run with zero writes. An adoption command must explicitly choose
`adopt-stored-snapshot`, a source role (primary/temp/backup), expected source
SHA256, ROM identity input and a separate new output path. The tool reads only
the supplied synthetic/user-local inputs; it refuses existing outputs and any
output alias of a source. This Codex task never runs it on user ROM/save/RTC data.

The tool emits a pending version2 record. It does not invent a host anchor from
PC UTC or request a guessed DS clock. The production loader reads corrected DS
RTC via the existing ARM7 path, commits and rereads a ready record before guest
execution, and uses that DS value as the new anchor without adding past elapsed
time. The pending record itself never authorizes guest execution before commit.

Cancel/inspect means no write. Without an explicit pending adoption record,
legacy detection stops before VM execution with a readable action notice.
Startup errors and conflicts likewise cannot silently initialize from the host.
The legacy/error startup gate must precede `handleSave` or any other save-file
initialization mutation, so cancel/block cannot create or extend a `.sav`.
The notice is not a second migration UI. The offline tool is the only consent
entry point. Runtime I/O failures retain dirty/error and prevent repeated file
operations; an actionable terminal RTC notice must not claim recovery.

## New namespace and exact format

Modern candidates use the same ROM basename with `.g3rtc2`, `.g3rtc2.tmp`,
`.g3rtc2.bak`. The old executable never uses these names. These are three
persistent rotating slots, not the old destructive rename transaction.

Version2 is exactly 200 little-endian bytes. No packed/native-layout ambiguity:

| Offset | Field |
|---:|---|
| 0 | u32 magic `0x32523347` (G3R2) |
| 4 | u16 version2 |
| 6 | u16 payloadLength188 (bytes8..195) |
| 8,12,16 | u32 gameCode, romSize, complete192-byte-header FNV1a |
| 20 | u32 sequence |
| 24,28 | u32 corrected host anchor, stored game seconds |
| 32 | i16 weekday offset |
| 34,36,38 | u16 status, interrupt, flags0 |
| 40 | u16 policy: 0 fresh, 1 adopt stored snapshot without past elapsed |
| 42 | u8 phase: 0 pending, 1 ready |
| 43 | u8 legacy presence mask: primary1/temp2/backup4 |
| 44 | u8 selected legacy role0/1/2, or255 for fresh |
| 45..47 | reserved zero |
| 48 | u32 selected legacy sequence |
| 52,56,60 | u32 FNV1a of each exact44-byte legacy record, absent0 |
| 64..195 | three exact44-byte legacy snapshots, absent allzero |
| 196 | u32 FNV1a over bytes0..195 |

Validate magic/version/size/length/checksum/flags/reserved/identity before use.
Host and game seconds are in `[0,36525*86400)`; weekday offset is -6..6;
status uses only0xEA; the full u16 interrupt register is retained (no invented
device semantics). Legacy records receive the same semantic range checks.
Pending requires policy1, sequence0, host0 and copied selected game state.
Fresh requires ready, absent legacy set, selected role255 and zero provenance.
Adopted records require a present selected role, consistent hashes/exact bytes,
selected legacy sequence and matching identity in every present legacy record.
Ready records may legitimately wrap their sequence through0.

## Detection, selection and rollback

Keep separate outcomes MISSING, VALID_LEGACY, VALID_CURRENT, CORRUPT,
UNSUPPORTED_VERSION, IDENTITY_MISMATCH, IO_ERROR and explicit conflict states.
Only FR_NO_FILE is missing; absence of a directory or another failure is not
permission to create new state. Oversized/truncated files are corrupt.

Fresh defaults are allowed only when both entire namespaces are missing.
Any existing malformed/unsupported/mismatched legacy sibling prevents adoption.
Inspect reports every role; explicit source choice avoids inventing a winner
for ambiguous legacy sequences. New files are never quietly interpreted as v1.

For modern candidates, unsupported version, identity mismatch, I/O failure or
different lineage blocks automatic selection. Known corrupt slots are preserved;
a valid ready sibling may be used if its lineage matches the current legacy set.
Every recorded legacy presence and byte must still match on every boot and
before a write. Appearance, deletion or change after downgrade is a conflict.

Among ready records of the same lineage, select a unique strict newest record
under modular32-bit ordering. Identical equal-sequence records may tie by path
order. Different equal-sequence records, half-range differences and cyclic
three-way orderings are conflicts. Pending seq0 can coexist with ready records
of that same lineage; ready wins without applying migration again. If no valid
ready exists and any corrupt modern slot exists, pending adoption is blocked;
do not guess whether an earlier ready state was lost.

Downgrade leaves modern files untouched but uses its old legacy namespace.
If it changes legacy data, re-upgrade blocks rather than choosing a sequence
across namespaces. New progress is not promised to appear in old executables.
Do not erase any files to resolve conflicts automatically.

## Write safety

Preflight all slots and current legacy lineage. Write into a missing slot or
an older valid slot distinct from the selected newest record. Never truncate
the selected/only valid record, or overwrite corrupt/unsupported/unknown data.
If no eligible destination exists, report a blocked write instead of destroying
data. Pending is preserved until another slot contains a verified ready record.

Check open/write/full byte count/sync/close/reopen/readback/full record equality.
Only then advance in-memory sequence and clear dirty. Failed operations retain
dirty and a latched error. A FIL whose close failed or whose failed operation
left it live must not be cleared or reopened. No unbounded automatic retry.

This algorithm makes no rename/unlink calls; regression tests must prove zero
such calls even when those APIs are configured to fail. Eliminating these paths
replaces, rather than evades, old rotation failure cases. At least the previous
selected valid record survives every attempted write. A checksum-valid newer
slot may be recovered on restart after the write reported failure; never assert
FAT power-cut atomicity, durability or a commit point beyond the tested model.

## Deferred-I/O stack and provisional memory measurements

The previous IRQ stack budget is 288 bytes; the RTC call frame requires 292
bytes before entering FatFs, so that stack is insufficient for RTC file I/O.
The implementation runs deferred RTC persistence on a dedicated 2 KiB EWRAM
stack wrapper. The wrapper preserves CPSR, SP, and the callee registers, with a
guard, canary, and high-water counter. A linked synthetic-filesystem probe
observed 248 bytes. A source call-graph plus `.su` audit found a conservative
reachable bound of 224 bytes of RTC frames plus 528 bytes for FatFs/IPC, or 752
bytes total, below the 2 KiB wrapper. The synthetic 248-byte observation is not
the bound and is not a hardware measurement. The audited graph has no reachable
indirect RTC calls or recursion.

At linked application checkpoint `b72014b`, `.ewram` measured 15,824 bytes and
ended at `0x02043DD0`, below the EWRAM BSS start at `0x02044000`. The heap began
at `0x021F1000`, leaving 60 KiB in the configured heap region versus the stable
64 KiB. RTC static records and alignment account for 4 KiB of this reduction.
DTCM was full, ITCM ended at `0x7FF8` (8 bytes remaining), and VRAM-A ended at
`0x06820000`; region limits were unchanged. The dedicated stack occupies the
natural EWRAM gap before the cache: base `0x020F00F8`, end `0x020F08F8`, metadata
end `0x020F0904`, cache start `0x020F1000`. The linked ELF and stack audit found
no section overlap or stack-budget overrun.

Three modern-path heap allocations were added for sidecar paths (path length
plus suffix). Peak TLSF availability was not measured. The resulting heap
headroom and its effect under real game/application load remain an accepted
hardware limitation; the stable 64 KiB free-heap figure must not be claimed for
this build. These results apply to the linked `b72014b` production ELF, whose
production code is unchanged through `236e4ff`; repeat the required checks for
the frozen final candidate. Do not add arbitrary padding or relax linker limits
to conceal memory use.

## Verification obligations

Independent legacy fixtures use the frozen old masks/serializer, not new records
with a relabeled version. Calendar oracle is Python datetime. Preserve existing
calendar/GPIO coverage or explain changed cases. Test actual write/close/object
recreation/reload/GPIO-visible state, all statuses, multi-game identity, explicit
consent, unchanged legacy/save bytes, pending restart, corrupt recovery, conflict,
equal/wrap/half-range/cycle sequences and every I/O failure point. Negative
controls must fail the intended assertions. Production boot caller and linked
loader must be exercised; record controlled filesystem/clock seams honestly.

Before artifact creation, verify pinned builds, linked regressions, exact NDS
identity, cold-code/stack/heap/section budgets and independent read-only review.
Only one designated build owner runs a full matrix for a frozen candidate.
No hardware PASS without actual execution; artifact says HARDWARE VERIFICATION REQUIRED.
