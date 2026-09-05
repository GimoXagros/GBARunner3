# Save-function search across SD-cache blocks

The baseline `searchHiCode` passes each 4 KiB block separately to
`mem_fastSearch16`. A synthetic signature at block end minus 12 bytes is missed
before this change. The regression compiles that exact function and fails on
baseline `4bc4d73`; after the change it passes all 19 existing source signatures.

The helper retains the assembly fast path for ranges of at least 44 bytes.
Short ranges use bounded comparisons because the assembly starts with an
unconditional 32-byte load. After each block's internal candidates, a 24-byte
scratch buffer joins its last 12 bytes to the next block's first 12 bytes.
The tail is copied before fetching another block, including one-slot eviction.
Only complete signatures inside the half-open range are compared. Existing
4-byte alignment is preserved: offsets end minus 12/8/4 and end match; the other
byte offsets in the requested end-minus-15 through end-plus-1 matrix do not.

The result is a logical primary GamePak address. Only after a match does
`searchHiCode` request its permanent patch backing. The existing two-byte SWI
write fits entirely within the starting block. Linear-first and ascending
high-ROM range order are unchanged. Searches do not join separate linear and
high-ROM search ranges or change mirror normalization.

Tests compile the exact production function with noncontiguous/reused fake cache
slots, short final blocks, range bounds, false prefixes, first-match order and
permanent patch pointer checks. CI additionally executes the production helper
and original assembly compiled by the pinned toolchain with instruction-level
read bounds checking. Signatures come from repository source; ROM payloads are
not fixtures.

## Remaining merge gates

This is a draft until the pinned build, linked semantics, section-size comparison
and review are recorded. Cache loading currently does not expose all underlying
FatFs failures to this caller; a null callback is tested, but that is not proof
of all physical SD I/O failure paths. Do not infer hardware verification from
synthetic results.

`SaveTagScanner::FindSaveTag` is a separate textual tag scanner with a circular
buffer. The previous maintenance report called the function-signature boundary
problem a SaveTagScanner defect; that label was inaccurate. This change targets
`Save.cpp::searchHiCode`, not textual tags or EEPROM protocol compatibility.


## Follow-up: actual storage failure propagation (2026-09-05)

The boundary helper now also tests first-fetch, next-fetch and repeated-fetch
failure with immediate termination and no permanent pin. A failed permanent
patch lookup returns null. The linked ARM corpus includes first-block failure
for all 19 signatures (513 cases total).

`tools/tests/test_storage_failure_host.py` compiles the actual ARM7 DLDI/DSi
handlers, ARM9 FsIpc (default non-IRQ-yielding configuration), diskio and SdCache
loader. Only hardware registers, cache-maintenance calls and device drivers are
fake. The production search helper runs over actual one-slot cache replacement.
It observes **13 passing checks and 6 explicitly tracked failures**:

| Device | Disk read error reaches FatFs | Disk write error reaches FatFs | Failed cached read rejects stale boundary signature |
| --- | --- | --- | --- |
| DLDI | FAIL | FAIL | FAIL |
| DSi SD | FAIL | FAIL | FAIL |

The driver reports failure but ARM7 unconditionally acknowledges completion.
FsWaitToken records completion only; diskio returns RES_OK. SdCache finishFetch
publishes stale bytes and the search can accept a stale cross-boundary pattern.
Normal read/write and pending/completed handshakes remain positive controls.
Default CI requires these exact six observations; `--require-fixed` deliberately
fails. Green observation CI is not proof of error propagation or safe media I/O.

### Required prerequisite, not a search-helper workaround

A transaction-result channel must be reviewed before changing the runtime:

1. ARM7 publishes a DLDI/DSi result before acknowledging the same transaction.
2. ARM9 invalidates/reads that result and captures it in the matching wait token
   before nested transactions reuse the command. Completion and success remain
   distinct; both aligned and bounce-buffer paths need checks.
3. diskio maps unsuccessful operations to RES_ERROR. Failed cache loads must
   not publish a valid map entry or consume a permanent patch slot.
4. All ordinary/JIT/DMA cache consumers need an explicit failure policy before
   a loader that currently guarantees a pointer can start returning null.
5. Verify cache ownership, nested completion ordering, ABI/cache-line placement,
   target-linked callers and physical SD failures. Do not infer these from a
   host synchronous driver model.

This reaches outside save-function search into shared storage/IRQ/cache policy.
No guessed protocol or JIT/DMA failure behavior is added to this draft. PR #6's
FatFs-seam ERROR handling also depends on this prerequisite. Keep both drafts;
no hardware verification or release promotion is claimed.
