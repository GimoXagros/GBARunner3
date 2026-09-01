#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

void cfdiag_initialize(
    const char* pathA, const char* pathB, u32 gameCode, u32 romSize);
void cfdiag_observeKeys(u16 keyInput);
void cfdiag_sampleVBlank(void);

void cfdiag_recordJitPatchArm(const u32* instructionPtr, u32 instruction);
void cfdiag_recordJitPatchThumb(const u16* instructionPtr, u16 instruction);
void cfdiag_recordArmUndefined(
    u32 sourceExecutionPc, u32 instruction, u32 cpsr, u32 lr);
void cfdiag_recordControlFlowTarget(u32 rawTarget);
void cfdiag_recordThumbControlFlow(
    u32 sourceExecutionPc, u32 instruction, u32 rawTarget, u32 cpsr, u32 lr);
void cfdiag_recordPrefetchAbort(u32 faultAddress, u32 cpsr, u32 lr);
void cfdiag_recordHicodeMiss(u32 faultAddress);
void cfdiag_recordHicodeMap(u32 gbaAddress, u32 mapMode);
void cfdiag_recordSdCache(u32 oldRomBlock, u32 newRomBlock, u32 cacheBlock);
void cfdiag_recordNotImplemented(void);

#ifdef __cplusplus
}
#endif
