# Licensing status notice

As of 2026-09-05, neither the upstream
[`Gericom/GBARunner3`](https://github.com/Gericom/GBARunner3) repository nor the
related `Gericom/GBARunner2` repository publishes a repository-wide open-source
license. The question remains open in upstream issues
[#200](https://github.com/Gericom/GBARunner3/issues/200) and
[#208](https://github.com/Gericom/GBARunner3/issues/208).

This notice does **not** grant a new license to upstream code and does not
relicense work owned by other contributors. Unless a file, directory, submodule,
or contributor supplies an explicit license or separate written permission,
copyright remains with its respective author and no additional permission is
granted by this fork.

## Files with explicit terms

Some components carry their own license notices. Those notices continue to
govern only their applicable files or components. Examples in the current tree
include:

- the ARM7, ARM9, and test linker scripts: Mozilla Public License 2.0 notices;
- `code/core/arm7/source/mmc`: MIT License in its own `LICENSE.txt`;
- the vendored ArduinoJson header: MIT License notice in the file;
- the TLSF allocator: its BSD-style license notice in the source;
- FatFs: its redistribution notice in the source; and
- the `code/libs/libtwl` submodule and other third-party components: their own
  upstream terms.

This list is descriptive and is not a substitute for reading every applicable
file and submodule notice.

## Custom branding assets

`logo.png` was supplied for inclusion in this fork. The NDS banner asset at
`code/bootstrap/icon.bmp` is a 32x32, 16-color conversion of that image. This
status notice does not assert a separate open-source license for either branding
asset. Reuse outside this fork requires permission from the applicable asset
rightsholder. Release binaries embed the converted banner icon.

## ROMs, BIOS, and release artifacts

Commercial ROMs, patched commercial ROM images, saves, and BIOS files are not
licensed or distributed by this repository. A compiled emulator binary does not
grant rights to those materials.

The `custom-v0.1.2` release includes source and binaries under this same
license-status notice; creating the tag or release does not add a new license
grant.

Before publishing a modified source tree or distributing compiled binaries,
obtain clarification or permission from the relevant upstream copyright holders
and comply with every third-party component license. This is a factual project
status notice, not legal advice.
