#include "common.h"
#include <string.h>
#include "SdCache/SdCache.h"
#include "cp15.h"
#include "MemoryEmulator/RomDefs.h"
#include "VirtualMachine/VMIrq.h"
#include "JitArm.h"
#include "JitThumb.h"
#include "JitCommon.h"

[[gnu::section(".ewram.bss")]]
jit_state_t gJitState;

static bool sJitEnabled;

[[gnu::section(".itcm"), gnu::optimize("Oz")]]
u32 jit_getJitBitsOffset(const void* ptr)
{
    u32 jitBitsOffset;
    u32 offset;

    if ((u32)ptr >= ROM_LINEAR_DS_ADDRESS && (u32)ptr < ROM_LINEAR_END_DS_ADDRESS)
    {
        // static rom region
        jitBitsOffset = offsetof(jit_state_t, staticRomJitBits);
        offset = (u32)ptr - ROM_LINEAR_DS_ADDRESS;
    }
    else if ((u32)ptr >= 0x03000000 && (u32)ptr < 0x04000000)
    {
        // IWRAM
        jitBitsOffset = offsetof(jit_state_t, iWramJitBits);
        offset = (u32)ptr & 0x7FFF;
    }
    else if ((u32)ptr >= (u32)sdc_cache && (u32)ptr < (u32)sdc_cache[SDC_BLOCK_COUNT])
    {
        // sd cache
        jitBitsOffset = offsetof(jit_state_t, dynamicRomJitBits);
        offset = (u32)ptr - (u32)sdc_cache;
    }
    else if ((u32)ptr >= 0x02000000 && (u32)ptr < 0x02040000)
    {
        // EWRAM
        jitBitsOffset = offsetof(jit_state_t, eWramJitBits);
        offset = (u32)ptr - 0x02000000;
    }
    else if ((u32)ptr >= 0x06000000 && (u32)ptr < 0x06018000)
    {
        // VRAM
        jitBitsOffset = offsetof(jit_state_t, vramJitBits);
        offset = (u32)ptr - 0x06000000;
    }
    else
    {
        jitBitsOffset = offsetof(jit_state_t, dummyJitBits);
        offset = 0;
    }

    return jitBitsOffset + (offset / 2 / 8);
}

void* jit_findBlockStart(const void* ptr)
{
    if ((u32)ptr >= (u32)sdc_cache && (u32)ptr < (u32)sdc_cache[SDC_BLOCK_COUNT])
    {
        // sd cache
        return (void*)((u32)ptr & ~SDC_BLOCK_MASK);
    }
    else if ((u32)ptr >= ROM_LINEAR_DS_ADDRESS && (u32)ptr < ROM_LINEAR_END_DS_ADDRESS)
    {
        // static rom region
        return (void*)ROM_LINEAR_DS_ADDRESS;
    }
    // no significant block boundary
    return (void*)0;
}

void* jit_findBlockEnd(const void* ptr)
{
    if ((u32)ptr >= (u32)sdc_cache && (u32)ptr < (u32)sdc_cache[SDC_BLOCK_COUNT])
    {
        // sd cache
        return (void*)(((u32)ptr & ~SDC_BLOCK_MASK) + SDC_BLOCK_SIZE);
    }
    else if ((u32)ptr >= ROM_LINEAR_DS_ADDRESS && (u32)ptr < ROM_LINEAR_END_DS_ADDRESS)
    {
        // static rom region
        return (void*)ROM_LINEAR_END_DS_ADDRESS;
    }
    // no significant block boundary
    return (void*)0xFFFFFFFF;
}

[[gnu::section(".itcm")]]
bool jit_isBlockJitted(void* ptr)
{
    u32 address = (u32)ptr;
    if (address >= ROM_LINEAR_GBA_ADDRESS && address < ROM_LINEAR_END_GBA_ADDRESS)
    {
        ptr = (void*)(address - ROM_LINEAR_GBA_ADDRESS + ROM_LINEAR_DS_ADDRESS);
    }
    else if (address >= ROM_LINEAR_END_GBA_ADDRESS && address < 0x0E000000)
    {
        u32 romBlock = ((address << 7) >> 7) >> SDC_BLOCK_SHIFT;
        void* cacheBlock = sdc_romBlockToCacheBlock[romBlock];
        if (!cacheBlock)
            return false;
        ptr = (void*)((u32)cacheBlock + (address & SDC_BLOCK_MASK));
    }

    const u8* const jitBits = jit_getJitBits(ptr);
    u32 bitIdx = ((u32)ptr & 0xF) >> 1;
    return (*jitBits >> bitIdx) & 1;
}

// Preparing an uncached block may perform SD I/O and is not latency-sensitive
// enough to consume scarce ITCM space. Keep only the JIT bit lookup hot path in
// ITCM; callers receive a linker veneer transparently.
[[gnu::section(".ewram"), gnu::optimize("Oz")]]
void* jit_ensureBlockJitted(void* ptr)
{
    void* const executablePtr = ptr;
    u32 address = (u32)ptr;
    if (address >= ROM_LINEAR_GBA_ADDRESS && address < ROM_LINEAR_END_GBA_ADDRESS)
    {
        ptr = (void*)(address - ROM_LINEAR_GBA_ADDRESS + ROM_LINEAR_DS_ADDRESS);
    }
    else if (address >= ROM_LINEAR_END_GBA_ADDRESS && address < 0x0E000000)
    {
        const void* cacheBlock = sdc_getRomBlock(address);
        ptr = (void*)((u32)cacheBlock + (address & SDC_BLOCK_MASK));
    }

    const u8* const jitBits = jit_getJitBits(ptr);
    u32 bitIdx = ((u32)ptr & 0xF) >> 1;
    if ((*jitBits >> bitIdx) & 1)
        return executablePtr;
    if ((u32)ptr & 1)
    {
        jit_processThumbBlock((u16*)((u32)ptr & ~1));
    }
    else
    {
        jit_processArmBlock((u32*)ptr);
    }
    dc_drainWriteBuffer();
    ic_invalidateAll();
    return executablePtr;
}

[[gnu::section(".ewram")]]
u32 jit_calculateArmBranchTarget(u32 instructionPtr, u32 instruction)
{
    return jit_calculateArmBranchTargetAddress(instructionPtr - 4, instruction);
}

void jit_init(void)
{
    memset(&gJitState, 0, sizeof(gJitState));
    gJitState.dummyJitBits = ~0u;
    sJitEnabled = true;
}

void jit_disable(void)
{
    sJitEnabled = false;
    memset(gJitState.staticRomJitBits, 0xFF, sizeof(gJitState.staticRomJitBits));
    memset(gJitState.dynamicRomJitBits, 0xFF, sizeof(gJitState.dynamicRomJitBits));
    memset(gJitState.iWramJitBits, 0xFF, sizeof(gJitState.iWramJitBits));
    memset(gJitState.eWramJitBits, 0xFF, sizeof(gJitState.eWramJitBits));
    memset(gJitState.vramJitBits, 0xFF, sizeof(gJitState.vramJitBits));

    for (u32 i = 0; i < VM_JUMP_TO_IRQ_HANDLER_COMMON_INSTRUCTION_COUNT; i++)
    {
        vm_jumpToIrqHandler[i] = vm_jumpToIrqHandlerCommon[i];
    }

    vm_jumpToIrqHandler[2] -= 8; // fix the ldr r4, DTCM(vm_irqSavedR4)
}

void jit_resetDynamicRomBlock(void* cacheBlock)
{
    const u32 cacheOffset = (u32)cacheBlock - (u32)sdc_cache;
    if (cacheOffset >= SDC_SIZE || (cacheOffset & SDC_BLOCK_MASK) != 0)
        return;

    const u32 jitBitsOffset = cacheOffset / 2 / 8;
    const u32 jitAuxBitsOffset = cacheOffset / 2 / 4;
    memset((u8*)gJitState.dynamicRomJitBits + jitBitsOffset,
        sJitEnabled ? 0x00 : 0xFF, SDC_BLOCK_SIZE / 2 / 8);
    memset((u8*)gJitState.dynamicRomJitAuxBits + jitAuxBitsOffset,
        0, SDC_BLOCK_SIZE / 2 / 4);
}
