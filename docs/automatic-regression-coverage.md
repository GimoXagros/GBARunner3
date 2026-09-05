# Automatic regression coverage — 2026-09-05

Baseline for this test-only extension: `db97744` (strict JSON validation merged).
`test_jit_core_elf.py` adds 45 executed checks against the linked production ARM9
ELF, alongside the six existing dispatch checks and ARM-only negative controls.
It tests backing lookup at low/high boundaries, loaded and missing GamePak mirror
blocks, ARM PC+8 and signed branch offsets including `EA7B4FFE -> 09ED4000`,
dynamic JIT/aux metadata reset at first/second/last cache slots, invalid reset
preservation, stale JIT metadata rejection after block replacement, and
ARM/Thumb compilation backing pointers. CP15 writes prove that hicode unmapping
precedes whole instruction-cache invalidation.

The existing source is loaded from the ELF. CP15 operations and block-compilation
calls are controlled at their boundaries; the test does not emulate physical
cache locking, SD timing, DMA or instruction execution inside a generated block.
The existing target NDS tests also cover branch-address helpers; compiling that
NDS is not evidence of complete target-test execution.

| Area | Executed evidence | Remaining scope |
| --- | --- | --- |
| settings | real host serializer + sanitizers, 304 configs / 2,513 addresses, 19 linked ARM parser cases | allocation failure policy, full target NDS run |
| mapped ARM/Thumb | six existing dispatch cases and 20 new mirror/segment-edge cases | physical cache and IRQ timing |
| backing / dynamic aux | 45-case linked suite includes reset/lookup/eviction and stale-bit rejection | actual SD cache load failure propagation |
| branch addressing | low-low, low-high, high-high, high-low, signed displacement and boundary vectors | full ARM-to-Thumb and Thumb-to-ARM instruction sequences |
| patching / invalidation | linked ensure-block caller supplies correct ARM/Thumb backing and unmaps before invalidate | real compilation and physical cache coherence |
| save signature boundary | independent draft PR #5: host + linked helper/assembly tests | not merged; physical I/O gate |
| save I/O | independent draft PR #6: fault matrix and limited byte guards | 14 explicit integrity failures; not merged |

Do not import production changes from draft save branches merely to collect all
tests on develop. Their harnesses and results remain reviewable in those PRs;
after a gated merge, their test commands can join this baseline naturally.
No JIT, DMA, timer/IRQ, RTC, EEPROM or FLASH runtime behavior is changed here.
