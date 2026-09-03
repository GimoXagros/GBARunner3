#pragma once
#include "common.h"
#ifdef __cplusplus
extern "C" {
#endif
void diag_setEnvironment(u32 romHeaderHash, u32 saveSize, u32 mountDevice, u32 dsiMode, u32 clockControl);
void diag_recordConfig(const char* path, bool loaded);
void diag_recordSdLoad(u32 oldBlock, u32 newBlock, u32 cacheBlock);
void diag_recordLowTarget(u32 kind, u32 source, u32 target, u32 nativeSpsr);
extern volatile u32 gDiagTraceKinds;
#ifdef __cplusplus
}
#endif
