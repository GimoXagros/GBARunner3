# Kingdom Hearts save-selection H control-flow hardware test

This is a diagnostic-only build for the corrected transition:

`Main Menu -> New Game -> Save Slot initialization -> black screen`

It does not change JIT, DMA, VRAM, save, RTC, IRQ, or high-ROM emulation semantics. It is not a release candidate and must not replace a known-good GBARunner3 build.

## Before testing

1. Copy `GBARunner3-KH-H-control-flow-vblank-arm.nds` and the supplied `_gba/configs` directory to the DSpico SD card using the same layout as the current working installation.
2. Keep the same GBA BIOS and the same patched `B8CJ` ROM used for A-F.
3. Back up, then remove every older sidecar whose name ends in `.g3diag`, `.g3diag.a`, or `.g3diag.b` next to that ROM.
4. Do not rename or modify the ROM between the test and collection.

ROM and BIOS files are not included in the artifact or repository.

## Exact test procedure

1. Start the H diagnostic NDS and boot the patched `B8CJ` ROM.
2. Reach the game's **Main Menu**.
3. Press **Select once** to arm the diagnostic.
4. Wait about **one second** at the Main Menu.
5. Choose **New Game** normally.
6. When the black screen occurs, **do not press any more buttons**.
7. Wait at least **five seconds**.
8. Shut the console down normally. Do not remove power or the SD card while it is writing.
9. If the Save Slot screen appears successfully instead, still wait five seconds and shut down normally.
10. Submit both files created next to the ROM:
    - `<ROM name>.g3diag.a`
    - `<ROM name>.g3diag.b`

Both files are required because the build alternates checksummed checkpoints. One may be older or incomplete if execution stopped during a write; the decoder selects the newest valid copy.

H observes Select and A only from the DS VBlank diagnostic callback, using a dedicated diagnostic stack. It does not call diagnostic code from the emulated game's KEYINPUT load handler and performs no filesystem write when Select is pressed. The first periodic full checkpoint is delayed until 60 VBlanks after the first A press following arm.

## Expected files

- A 64-byte file is only a boot-time `ready` marker. It does not contain a trace.
- A complete G checkpoint is 12,352 bytes: 64-byte header plus 128 records of 96 bytes.
- At least one submitted file should report `status=checkpoint` or `status=emergency`.

Decode the pair on a PC with Python 3:

```text
python decode_g3cf.py "<ROM name>.g3diag.a" "<ROM name>.g3diag.b" -o kh-save-select.csv
```

Submit the two original files as well as the CSV. Do not submit the ROM or BIOS.

## What the diagnostic records

The 128-event RAM ring stores only control-flow and cache breadcrumbs, not every instruction: ARM/Thumb PC-changing instructions, raw/guest/execution targets, ARM/Thumb state, LR/CPSR, ROM/cache block identity, hicode state, MPU region 4, prefetch aborts, undefined dispatch, hicode maps, SD-cache loads/evictions, input checkpoints, and VBlank heartbeats.

The ring is persisted periodically after it is armed and immediately on selected emergency conditions. No input after the black screen is required.

**Root cause not proven.**

**Hardware verification required.**
