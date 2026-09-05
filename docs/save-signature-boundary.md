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
