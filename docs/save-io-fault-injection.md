# Save I/O fault injection — 2026-09-05

The harness compiles the exact scheduler and I/O functions extracted from
`Save.cpp`. It substitutes a synthetic in-memory FatFs API, environment/IPC
stubs and the real shared-state definitions. No real save or ROM is used.
The stubs are a deterministic failure model, not emulation of SD timing,
power-loss durability, FAT metadata or the ARM7/ARM9 concurrency protocol.

Run `python3 tools/tests/test_save_io_host.py --require-fixed` for a failing
invariant gate. Default mode checks the exact explicitly listed remaining
failures; an unexpected pass or failure also fails CI so the matrix cannot drift
silently. Green CI in default mode means the documented observations reproduced,
not that save integrity is fixed. `SANITIZE=1` enables host ASan/UBSan.

| Scenario | Expected | Before 4bc4d73 | Candidate after |
| --- | --- | --- | --- |
| normal create | full 0xFF initialization | PASS | PASS |
| short-file extension | preserve prefix, append 0xFF | PASS | PASS |
| oversized existing file | no truncate | PASS | PASS |
| open/seek/map/read failure at init | reject and close | PASS | PASS |
| short read at init | reject and close | PASS | PASS |
| short write/write/sync/full/read-only at create | reject and close | PASS | PASS |
| whole write/close/reopen | data preserved | PASS | PASS |
| deferred seek/write/short-write/sync/full/read-only | retain failure/dirty state | FAIL (6) | FAIL (6) |
| retry each deferred failure | persist data and perform a new sync | FAIL (6) | FAIL (6) |
| byte read seek failure | return 0xFF, not wrong-position data | FAIL | PASS |
| byte write seek failure | do not modify wrong-position byte | FAIL | PASS |
| byte write outside file | do not extend the file | FAIL | PASS |
| byte read error/short read/out-of-range | deterministic 0xFF | uninitialized/unchecked source path | PASS (3) |
| interrupted initialization then retry | fill extension with 0xFF | FAIL | FAIL |
| standalone flush failure | visible error state | FAIL | FAIL |
| close failure | observable failure and still-open handle | PASS (fake API observation) | PASS |
| repeated VBlank after error | bounded calls | PASS, but error lost | same limitation |

Baseline: 16 passing scenarios, 17 failing invariants. Candidate: 22 passing
scenarios including three added read checks, 14 explicitly tracked failures.
The byte helper changes initialize reads, validate range/seek, reject incomplete
reads and prevent failed-seek writes. Save protocols, SWI signatures, shared-state
ABI and retry scheduling are unchanged. A write or flush still has no result in
the public void API; this candidate does not claim to make those failures visible.

## Blockers and next evidence

- `sav_writeSaveToFile` unconditionally marks CLEAN after write/sync failure.
  Simply leaving WRITE set can cause continuous VBlank I/O. ARM7's
  `FlushSaveIfDirty` waits for CLEAN, so changing this needs an explicit bounded
  retry/error/acknowledgment design and linked/concurrency verification. No such
  state-machine change is guessed here.
- Preallocation extends the file before fill. A failed fill can leave
  unspecified bytes in an apparently full-size file; next startup cannot infer
  which bytes belong to a valid save. Transactional initialization requires a
  recovery identity/sidecar or allocation-order design, not blanket overwriting.
- The fake close failure is observable but does not prove production recovery
  after a failed cleanup close. Add combined failure scheduling before changing
  cleanup lifecycle. Physical read-only/full-media and interrupted writes remain
  hardware checks.
- The production byte-access candidate remains a draft: linked target byte-I/O
  semantics, all failure paths and hardware/performance gates are not complete.
  Sanitizers do not detect every uninitialized read; the baseline seek failure
  is deterministic evidence and source audit identifies the uninitialized local.
