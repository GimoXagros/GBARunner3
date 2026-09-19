#include "Save/SaveSignatureSearch.h"
extern "C" const uint32_t* mem_fastSearch16(const uint32_t*, uint32_t, const uint32_t*);
extern "C" __attribute__((noinline)) const void* probe_get_block(uint32_t address)
{
    asm volatile("" : : "r"(address) : "memory");
    return nullptr; // instruction harness replaces this platform operation
}
extern "C" void* memcpy(void* destination, const void* source, size_t size)
{
    auto* dst = static_cast<unsigned char*>(destination);
    const auto* src = static_cast<const unsigned char*>(source);
    for (size_t i = 0; i < size; ++i) dst[i] = src[i];
    return destination;
}
extern "C" int memcmp(const void* lhs, const void* rhs, size_t size)
{
    const auto* a = static_cast<const unsigned char*>(lhs);
    const auto* b = static_cast<const unsigned char*>(rhs);
    for (size_t i = 0; i < size; ++i) if (a[i] != b[i]) return a[i] - b[i];
    return 0;
}
extern "C" uint32_t search_probe(const uint32_t* signature, uint32_t start, uint32_t end)
{
    return sav_findSignature16(signature, start, end, probe_get_block, mem_fastSearch16);
}
