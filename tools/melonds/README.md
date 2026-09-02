# Local melonDS compatibility probe (experimental)

This runs the **GBARunner3 NDS application inside melonDS**, with an isolated
DLDI FAT image and copies of locally supplied GBA inputs. It does not replace
GBARunner3 with a native GBA emulator. No ROM, BIOS, save, or framebuffer data is
distributed here. The frontend and lab patch are GPL-3.0-or-later; see COPYING.
This applies only to this tool, not to the GBARunner3 repository as a whole.

## Validation boundary

This is an experimental NTR/DS interpreter environment, **not 3DS + DSpico**.
It cannot certify hardware timing, cache coherence, audio synchronization, or
the real DSpico storage path. The first-access STM abort experiment does not
establish complete ARM946 abort/writeback semantics. Do not ship the patched
melonDS core as a generally validated emulator.

`frames.csv` reports native DS CPU state, not necessarily a guest GBA PC.
`invalid-pc-ring.csv` is a bounded 256-native-instruction exception/guard aid,
not a game-specific root-cause assertion. Large low ITCM-mirror addresses are
valid GBARunner3 dispatch trampolines. The in-application `.g3diag` format is
unchanged and its existing decoder must verify checkpoint checksums.

## Pinned source and lab-only changes

Use the official [melonDS source](https://github.com/melonDS-emu/melonDS) at
`cce3e49252392801da758ad41ed66d5f730d9a07` from its cache-development history.
Stock melonDS 1.1 does not implement the cache-debug machinery needed here.
The newer cache-development head `2d7c9f4097b58eec8eb6f699b2a817801c6af250`
was also inspected/tested, but it is **not** the final successful pinned run.

Apply `cache-lab.patch` only to a separate clean melonDS checkout. It includes:

- 4 GiB MPU region arithmetic without 32-bit byte-size overflow;
- termination of cache-debug tag-write switch cases (no data/tag fallthrough);
- restoration of cache-row address bits in melonDS's internal full-address
  tags when a CP15 debug tag is written;
- an interpreter-only unwind after a data abort, preventing the remaining
  words of the denied STM from executing in the new privileged exception mode;
- the bounded instruction-observation callback used by this frontend.

Tag/index semantics were checked against the official
[ARM946E-S TRM, sections 2.3.16 and 3.1](https://documentation-service.arm.com/static/5e8e3ee588295d1e18d3aa82).
No GBARunner3 high-ROM mapping, PC+8 calculation, JIT dispatch, RTC, or save
handling is altered by this patch.

## Build and tests

The local Windows run used w64devkit 2.9.1 and CMake 4.4. Use ASCII checkout /
build paths when GNU Make cannot handle the workspace path. Add the toolchain
`bin` directory to PATH so that GCC finds its assembler and linker.

```text
git clone https://github.com/melonDS-emu/melonDS melonDS-lab-source
git -C melonDS-lab-source checkout cce3e49252392801da758ad41ed66d5f730d9a07
git -C melonDS-lab-source apply /absolute/path/to/tools/melonds/cache-lab.patch
cmake -S /absolute/path/to/tools/melonds -B build -G "MinGW Makefiles" -DMELONDS_SOURCE=/absolute/path/to/melonDS-lab-source -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
build/gbar3-melonds-probe --selftest
```

The ROM-free tests cover MPU access, independent I/D-cache tags and data,
nonzero-row cache-debug lookup, and first-word-denied STM preserving DS IME.
The MPU, tag fallthrough, and nonzero-row tests were observed to fail before
their corresponding corrections. For the STM case, the pre-correction evidence
is the runtime trace (a post-abort STM write cleared DS IME); the synthetic test
was added after the correction.

## Isolated inputs and commands

Supply copies, never the live SD directory. GBARunner3 can modify `.sav` and
sidecars; the FAT image syncs changes back to the supplied copy on clean exit.
Use a new working directory for each run because logs are overwritten. The
Windows command-line wrapper currently expects ASCII argument paths; use
isolated ASCII-path copies if necessary (UTF-8 filesystem operations alone
do not convert the Windows narrow `argv` encoding).

```text
sd/rom.gba
sd/rom.sav                 # copy of the selected save, if present
sd/_gba/bios.bin
sd/_gba/configs/           # same configuration set as the tested card
bios/biosnds9.bin          # 4096 bytes, user-supplied
bios/biosnds7.bin          # 16384 bytes, user-supplied
bios/firmware.bin          # user-supplied DS firmware
```

Run `gbar3-melonds-probe NDS_FILE SD_DIRECTORY BIOS_DIRECTORY`, then send
commands on stdin. `run N MASK` holds the given buttons for N emulated frames;
mask is hexadecimal: A=1, B=2, Select=4, Start=8. `screen path.bmp` saves both
screens; `state` and `read ADDRESS LENGTH` aid native-state inspection; `quit`
stops and synchronizes the FAT image. The existing `incomplete SD write
command?? len=0` warning from the experimental core was observed; exported
diagnostic files must still pass the checksum test. Do not hide this warning
or equate it with a proven failure in GBARunner3 or DSpico.

`kh-save-slot.commands` is the exact timed input sequence used with L. It is
an observed-path recipe, not a universal automatic assertion: inspect the
saved menu/save-slot images and decode the sidecars before claiming a pass.
It presses Select only once at the visible main-menu checkpoint.

## 2026-09-02 results

See [the investigation record](../../docs/kh-save-select-investigation.md).
K and L both displayed the Save Slot screen in this patched melonDS lab.
K subsequently displayed intro footage. **The hardware black screen was not
reproduced or fixed. Hardware verification required.**

An exact J comparison stopped making observable frame progress after the
frame-780 heartbeat, before the first 900-frame screenshot/inputs. This does
not reproduce J's reported post-menu hardware symptom. The remaining host
failure is not diagnosed. A 50-million-ARM9-instructions-per-frame guard only
bounds the ARM9 interpreter path; it does not bound all scheduler/ARM7 stalls.
Use an external run timeout for unattended experiments and retain failed-run
evidence; an externally stopped run may not synchronize its isolated FAT image.
