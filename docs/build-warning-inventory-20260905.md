# Pinned warning inventory — 2026-09-05

Source db97744, devkitARM GCC 14.2.0, baseline serial build run 33943180750.
Repeated include paths are normalized below; no warnings are suppressed.

| Location | Warning | Classification |
| --- | --- | --- |
| `/opt/devkitpro/devkitARM/arm-none-eabi/include/c++/14.2.0/bits/shared_ptr_base.h:182:(.text._ZN7testing8internal26ParameterizedTestSuiteInfoI12ArmLdrbImmRdED2Ev[_ZN7testing8internal26ParameterizedTestSuiteInfoI12ArmLdrbImmRdED5Ev]+0xd4):` | This implementation of __sync_synchronize is a stub with no effect.  Relink with | intentional low-level assembly/linker behavior; barrier selection remains open |
| `code/core/arm9/source/JitPatcher/JitArm.c:12:1:` | 'noreturn' function does return | intentional low-level assembly/linker behavior; debugger trap return contract needs review |
| `code/core/arm9/source/JitPatcher/JitThumb.c:14:1:` | 'noreturn' function does return | intentional low-level assembly/linker behavior; debugger trap return contract needs review |
| `code/core/arm9/source/Peripherals/DmaTransfer.c:407:23:` | passing argument 1 of 'dc_invalidateLine' discards 'volatile' qualifier from pointer target type [-Wdiscarded-qualifiers] | toolchain compatibility; cache API qualifier mismatch |
| `code/core/arm9/source/VirtualMachine/VMUndefinedArmTable.cpp:34:13:` | unused variable 'x' [-Wunused-variable] | real source defect: test/hygiene issue, no emulator failure inferred |
| `code/core/arm9/source/main.cpp:118:24:` | 'bool mountAgbSemihosting()' defined but not used [-Wunused-function] | false positive for this configuration: dormant optional mount helper |
| `code/libs/libtwl/common/include/libtwl/gfx/gfxStatus.h:4:9:` | "REG_VCOUNT" redefined | vendored/third-party |
| `code/libs/libtwl/common/include/libtwl/rtos/rtosIrq.h:70:9:` | "REG_IME" redefined | vendored/third-party |
| `code/libs/libtwl/common/include/libtwl/rtos/rtosIrq.h:72:9:` | "REG_IE" redefined | vendored/third-party |
| `code/libs/libtwl/common/include/libtwl/rtos/rtosIrq.h:73:9:` | "REG_IF" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl7/include/libtwl/sio/sio.h:64:9:` | "REG_KEYINPUT" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl7/include/libtwl/sio/sio.h:65:9:` | "REG_KEYCNT" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl7/include/libtwl/sound/sound.h:3:9:` | "REG_SOUNDCNT" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl7/include/libtwl/sound/sound.h:4:9:` | "REG_SOUNDBIAS" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl9/include/libtwl/math/mathDiv.h:11:9:` | "REG_DIVREM_RESULT" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl9/include/libtwl/math/mathDiv.h:3:9:` | "REG_DIVCNT" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl9/include/libtwl/math/mathDiv.h:5:9:` | "REG_DIV_NUMER" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl9/include/libtwl/math/mathDiv.h:7:9:` | "REG_DIV_DENOM" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl9/include/libtwl/math/mathDiv.h:9:9:` | "REG_DIV_RESULT" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl9/include/libtwl/math/mathSqrt.h:3:9:` | "REG_SQRTCNT" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl9/include/libtwl/math/mathSqrt.h:4:9:` | "REG_SQRT_RESULT" redefined | vendored/third-party |
| `code/libs/libtwl/libtwl9/include/libtwl/math/mathSqrt.h:6:9:` | "REG_SQRT_PARAM" redefined | vendored/third-party |
| `code/test/arm9/source/main.cpp:48:18:` | ignoring return value of 'int RUN_ALL_TESTS()' declared with attribute 'warn_unused_result' [-Wunused-result] | real source defect: test/hygiene issue, no emulator failure inferred |

The noreturn helpers contain a debugger `bkpt` and no C-level nonreturning path;
this is not silently changed because debugger continuation and JIT failure behavior
require a separate decision. The test main ignores RUN_ALL_TESTS status; the host
and linked CI suites return real failure codes and do not depend on that exit path.
