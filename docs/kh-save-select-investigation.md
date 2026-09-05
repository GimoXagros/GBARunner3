# B8CJ Save Slot transition investigation

Status: resolved for the reported transition in `custom-v0.1.2`

Last updated: 2026-09-05

This document preserves the durable findings from the diagnostic branches. It
does not include ROM, BIOS, save, full trace, or local-path data.

## Reported symptom

On Nintendo 3DS in DS mode using DSpico, the Korean-patched B8CJ revision faded
to black after selecting New Game and did not reach the Save Slot screen. The
same patched revision passed that transition in other GBA emulators.

## Eliminated initial hypotheses

The A-F hardware matrix established the following:

- disabling BG VRAM abort handling did not change the failure;
- disabling the VRAM write buffer did not change the failure;
- disabling both did not change the failure;
- disabling JIT prevented the title from reaching the reproduction point; and
- the tested safe-DMA path did not change the failure.

These results lowered the priority of the two tested VRAM settings and excluded
only the exact safe-DMA experiment. They did not exclude JIT or DMA as whole
subsystems.

## First proven control-flow defect

The H/I traces reached mapped high-ROM Thumb code through a prefetch abort and
then an undefined Thumb instruction. The mapped undefined handler had replaced
the exception stack pointer before testing it as though it still contained the
saved SPSR. It therefore dispatched a Thumb instruction to the ARM decoder.

The first production fix reloads the saved SPSR from DTCM, preserves the guest
ARM/Thumb state, and selects the correct 16-bit halfword from the cached
instruction word. Linked ARM semantic tests cover mapped ARM, both Thumb
halfwords, hicode misses, and negative controls that reproduce the former
ARM-only dispatch.

This correction removed the original not-implemented termination, but the J
hardware build still showed a black screen. It was a proven generic defect, not
the complete symptom fix.

## Remaining generic coherence defects

The subsequent source audit found two related high-ROM Thumb JIT errors:

1. undefined Thumb handlers used the guest `0x09...` execution address to read
   auxiliary JIT bits and write dynamic patches. High-ROM data and metadata
   reside in a loaded SD-cache backing block, so the guest address selected dummy
   metadata and was not a valid patch destination;
2. JIT paths invalidated the whole ARM946E-S instruction cache without first
   disabling the active hicode MPU mapping. Whole invalidation also discards the
   locked hicode lines, leaving the MPU region active without valid mapped tags.

Fork PR [#2](https://github.com/GimoXagros/GBARunner3/pull/2) resolves guest
addresses to the static-ROM or SD-cache backing address for metadata and patch
writes, and unmaps hicode before whole instruction-cache invalidation. It adds no
B8CJ game-code, ROM-hash, PC-specific, or Save Slot bypass.

## Rejected diagnostic build

The automatic M diagnostic build synchronously opened, wrote, synchronized, and
closed a 30,528-byte trace file from the VBlank interrupt. Hardware testing then
showed irregular black scanlines, screen flicker, and mechanical audio clicks in
multiple games. M was rejected as a production candidate. Its timing-heavy file
writer is absent from PR #2 and `custom-v0.1.2`.

M still supplied useful evidence: the first retained low-address control-flow
event occurred between the first and last input samples, and the guest display,
timers, and sound DMA continued to change after the bad return address appeared.
The comparator's phase classification was corrected in the final diagnostic
history.

## Final hardware result

The N hardware build used implementation commit
`9b991ac9c89e1952b8573f4bf8bc9708bcade92b` and contained no automatic diagnostic
writer.

- `GBARunner3.nds` SHA-256:
  `CC09916848C6FB92092DB15D5D8EBDA21F4543A63589804F44268D2D810601CE`
- Environment: Nintendo 3DS in DS mode using DSpico
- B8CJ result: `Main Menu -> New Game -> Save Slot` passed
- Adjacent result: the scanlines, flicker, and audio clicks from M did not recur
  in the other games checked

The same NDS is packaged in release
[`custom-v0.1.2`](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.2),
whose tag points to merge commit
`dd3f44be5e9412ba29f3d831fc236dcc6016b71e`.

## Evidence limits

The reported failure is resolved. The following remain unverified and are
tracked in `TODO.md`: slot selection, intro completion, gameplay, save, restart,
load, full playthrough, and a wider title matrix. The combined N change does not
separately establish whether either backing-address repair or unmap ordering
alone would have been sufficient.

The full diagnostic history is preserved by annotated tag
`archive/kh-save-select-investigation-2026-09-05`; it is not part of the release
source.
