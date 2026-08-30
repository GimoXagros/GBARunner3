#pragma once

#include "common.h"
#include "GbaDma.h"

#ifdef __cplusplus
extern "C" {
#endif

extern volatile u32 gDiagDataAbortCount;
extern volatile u32 gDiagPrefetchAbortCount;

typedef struct
{
    u32 readCount;
    u32 reserved;
    u32 lastReadAddress;
    u32 writeCount;
    u32 lastWriteAddress;
    u32 lastWriteValue;
} DiagnosticSramState;

// The assembly instrumentation uses the fixed field offsets in this object so
// it does not add a function call to every SRAM access.
extern volatile DiagnosticSramState gDiagSramState;

void diag_initialize(const char* filePath, u32 gameCode, u32 romSize);
void diag_sampleVBlank(void);
void diag_recordDmaStart(u32 channel, const GbaDmaChannel* dma, u32 control);

#ifdef __cplusplus
}
#endif
