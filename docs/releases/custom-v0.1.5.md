# GBARunner3 Custom v0.1.5

Release tag: `custom-v0.1.5`. This stable release includes PR #16 RTC BCD
correction, explicit legacy RTC compatibility and the ARM946 L4 boot fix.
The game `.sav` format is unchanged; RTC data remains in separate sidecar files.

The launcher banner displays exactly:

```text
GBARunner3 Custom
A GBA Hypervisor for DS
GimoXagros
```

The original icon and palette are preserved, including transparent background
index 0. GBARunner3 was originally developed by Gericom and its contributors;
the custom banner identifies this distribution and does not replace their
copyright notices or licenses.

## Hardware evidence and limits

On 2026-09-30 the user reported normal game boot and saving on 3DS + DSpico
using the supplied `22fb48dfeca7575804476abf5f1a5c2a707ae606` RTC candidate.
In Pokemon Emerald, setting the clock, saving, exiting, waiting and restarting
showed the corresponding elapsed time. No exact wait duration, ROM hash or full
device/title matrix was supplied. This is user-reported evidence, not an
independently reproduced hardware run. The v0.1.5 banner itself has not been
tested on that device. Physical SD removal, power loss and legacy migration
failure scenarios were not hardware verified.

RTC legacy records are never silently converted or discarded. Old BCD data can
be lossy: migration adopts an explicitly selected stored snapshot; it cannot
recover the original clock or historical offline elapsed time. Read the included
`RTC-COMPATIBILITY.md` before migrating. Fresh installations and existing valid
modern RTC files need no migration. Save/storage/RTC errors retain the existing
fail-closed behavior; there is no automatic retry or recovery UI.

## Installation and rollback

1. Back up all `.sav`, `.g3rtc*` files and personal settings before updating.
2. Copy `GBARunner3.nds` to the launcher location. Compare `_gba/configs` with
   existing settings before copying; do not overwrite private settings blindly.
3. Check boot, save, normal exit, restart and load using backed-up saves.
4. To roll back, use the preserved `custom-v0.1.4` NDS and matching pre-upgrade
   backups. Version 0.1.4 does not read new `.g3rtc2` state. If it changes legacy
   RTC files, a subsequent v0.1.5 upgrade detects a lineage conflict. Keep both
   file sets and backups rather than deleting files to bypass that check.

After publication, custom-v0.1.5 is the recommended stable release;
[custom-v0.1.4](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.4)
remains available for rollback. No ROM, BIOS or save is included or requested.

## Reproducible identity

- Toolchain: `devkitpro/devkitarm:20241104`
- Application NDS SHA-256: `96b9db9efdc70aa43bb582078bf4dfc944826156e13365c8466119c6a507b7a4`
- Test NDS SHA-256 (not distributed): `50cce7e4ee4f5ae5fd814d0dea14edf39a0ecb4c017af5395cb327d32b713de7`
- libtwl: `e069645bed14a93e149e873e9273f04851e3a04e`
- Configs: 304; manifest SHA-256:
  `0ada1a9e67e36a6b9780d65ad6c39f3eb8922c1750691ac8db46f34ec9d078b8`

The package manifest records the exact source and binary identities. Release
requires pinned builds, regression/linked ARM tests, repeated j1/j2/j4 builds,
independent review and package dry-run. Final CI links, public ZIP hash and
asset digest are recorded on the release page after download verification.
Runtime matches the tested RTC candidate; banner metadata is changed. The RTC
candidate has a 56 KiB heap and a conservative pending-adoption boot stack bound
of 960/992 bytes. These are linked/static measurements, not hardware peak usage
or performance claims.

## Post-publication verification record

Recorded after the immutable release tag; no released binary was changed.
Source/tag target: `76fec2fd6a2a8422fc51b961374c0208d3932ef6`.
Release workflow [36589715672](https://github.com/GimoXagros/GBARunner3/actions/runs/36589715672)
succeeded. A fresh public download on 2026-09-30 KST matched the CI dry-run ZIP
and GitHub asset digest:
`bdc5cecdd166d2ebe0ada3f5e7479da7081c01f534a7f8607a82d428694df1c0`.
The 165568-byte ZIP contains 310 allowlisted files. All checksums, banner fields,
transparent icon, source manifest and 304 source-identical configs passed.

Exact-source [nightly](https://github.com/GimoXagros/GBARunner3/actions/runs/36588692142)
and [six-build/package validation](https://github.com/GimoXagros/GBARunner3/actions/runs/36588741933)
passed, with no actionable findings in the independent release review. The
optional latest-toolchain failure remained the known libtwl setVectorBase issue.
Hardware scope is unchanged from the user report above.
