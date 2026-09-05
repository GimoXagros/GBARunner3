# EEPROM V124/V125 source audit — 2026-09-05

Status: **RESEARCH COMPLETE** for this source audit; compatibility remains
**BLOCKED — HARDWARE REPRODUCTION REQUIRED**. No new signature, mask or protocol
change is established. Baseline: `db97744`.

## Version selection and available signatures

| Tag | Read signature | Program signature | Save size |
| --- | --- | --- | --- |
| V111 | `sReadEepromDwordV111Sig` | `sProgramEepromDwordV111Sig` | 512 bytes |
| V120/V121/V122 | `sReadEepromDwordV120Sig` | `sProgramEepromDwordV120Sig` | 8 KiB |
| V124/V125 | `sReadEepromDwordV120Sig` | `sProgramEepromDwordV124Sig` | 8 KiB |
| V126 | `sReadEepromDwordV120Sig` | `sProgramEepromDwordV126Sig` | 8 KiB |

Selection is explicit in `SaveTagScanner.cpp` and `SaveEeprom.cpp`. A failed read
patch short-circuits program patching. A failed program patch returns false after
the read patch has already succeeded; pair-level rollback is not implemented.
The added host harness executes all four actual selectors with success, first
failure and second failure (12 cases), checks V124/V125 tag routing, and performs
three synthetic eight-byte read/program wrapper round trips including the last
8 KiB entry. These test existing selection/byte-order behavior, not an SDK or
hardware implementation.

## Instruction structure and mask limits

The six source patterns are 16-byte Thumb prefixes, searched at 4-byte alignment.
V111 read uses a frame-pointer-oriented prologue (push r4/r5/r7/LR and stack
adjustment). V120 read starts with push r4/r5/r6/LR, a different stack frame,
argument movement, shifts and a PC-relative literal load. V120 program similarly
includes argument shifts and a literal load. V124 program pushes r4-r7/LR,
subtracts 176 bytes from SP and uses a different argument/register arrangement;
V126 additionally saves a high-register value and changes register allocation.
These are prefix decodes only: full control flow, literal contents, return paths
and ABI preservation cannot be proved from 16 bytes.

The exact comparisons include register fields and literal-load displacement.
Compiler variation could change either, but no failing variant has been supplied
to establish which bits are variable. A broad prologue mask could match unrelated
functions. A future mask must have independently justified instruction-class,
register/data-flow and literal-target checks, bounded reads, uniqueness checks
and negative candidates. No guessed mask is committed.

## Prior art and issue separation

All six source signatures were compared in little-endian form against
[GBARunner2 at 6e4ce456](https://github.com/Gericom/GBARunner2/blob/6e4ce456597cabaaa45aac83bbd2f4c90580dca8/arm9/source/save/EepromSave.vram.cpp)
and are identical, including the pre-existing Moero!! Jaleco Collection warning.
This is lineage evidence, not independent proof that the V124 prefixes cover
every SDK/compiler build. No external source code was copied into the repository.

[Upstream issue #198](https://github.com/Gericom/GBARunner3/issues/198), still open
when queried, separately reports save detection/functionality trouble and a
Dr. Mario startup crash with JIT enabled. Disabling JIT reportedly avoids the
startup crash; it does not establish that saving works. The issue provides no
function prefix or clean source reproducer from which to derive a new pattern.

[mGBA savedata at fa977ccb](https://github.com/mgba-emu/mgba/blob/fa977ccbc815efc93aefad9acddc9af0577d7827/src/gba/savedata.c)
is a public MPL-2.0 protocol implementation. Its EEPROM routines process serial
commands, addresses, bit order and settling state rather than matching SDK
function prefixes. It is useful for a future independent protocol oracle, but
cannot validate a V124 Thumb signature or justify copying one from a ROM.
The reviewed devkitPro libgba source inventory did not provide a versioned V124
SDK reference. No SDK equivalence or general legal conclusion is inferred.

The high-ROM 4 KiB function-search defect can hide an otherwise exact signature
at a cache boundary. Draft PR #5 tests that generic case with existing patterns;
it does not establish the cause of #198 or the older source comment. Textual save
tag scanning, function identification, I/O integrity and JIT startup are separate
failure stages and must be recorded separately.

## Required next evidence and one hardware checklist

1. Record exact NDS/ROM/config/BIOS hashes privately, environment and effective
   ROM size. Preserve the current production-equivalent build and isolated saves.
2. Record textual tag detection and selected patcher; separately record whether
   read/program identification succeeded and each offset's alignment and distance
   from a cache boundary. Do not commit commercial payloads.
3. Reproduce the same startup/save route with the existing JIT setting recorded;
   use the issue's JIT-off observation only to separate the startup failure.
4. For a suspected pattern variant, obtain an independently authored public-source
   reproducer with provenance or a minimal permitted local disassembly analysis.
   Prove ABI/data flow, compare against near matches, then propose a draft mask.
5. Only after identification is established, test write, restart and load plus
   failure recovery. Until then do not say “V124 fixed”.
