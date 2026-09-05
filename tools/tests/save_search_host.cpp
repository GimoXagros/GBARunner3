#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
using u32 = uint32_t;
constexpr u32 SDC_BLOCK_SIZE = 4096;
#if __has_include("Save/SaveSignatureSearch.h")
#include "Save/SaveSignatureSearch.h"
#endif
std::vector<uint8_t> rom;
alignas(32) uint8_t slots[3][8192], permanent[4096];
unsigned slot = 0, loads = 0;
bool reuse = false, rejectPin = false;
unsigned rejectLoad = UINT32_MAX;
u32 pinnedAddress = 0, rejectBlock = UINT32_MAX;
const void* sdc_getRomBlock(u32 address) {
    u32 offset = (address & 0x01FFFFFF) & ~4095u;
    assert(offset < rom.size());
    ++loads;
    if (offset == rejectBlock || loads == rejectLoad) return nullptr;
    auto* result = slots[reuse ? 0 : slot++ % 3];
    std::memset(result, 0xCD, 8192);
    std::memcpy(result, rom.data() + offset, std::min<size_t>(4096, rom.size() - offset));
    return result;
}
void* sdc_loadRomBlockForPatching(u32 address) {
    pinnedAddress = address;
    if (rejectPin) return nullptr;
    u32 offset = address & 0x01FFFFFF;
    std::memcpy(permanent, rom.data() + (offset & ~4095u), std::min<size_t>(4096, rom.size() - (offset & ~4095u)));
    return permanent + (offset & 4095);
}
const u32* mem_fastSearch16(const u32* data, u32 length, const u32* pattern) {
    for (u32 i = 0; i + 16 <= length; i += 4)
        if (!std::memcmp(reinterpret_cast<const uint8_t*>(data) + i, pattern, 16))
            return reinterpret_cast<const u32*>(reinterpret_cast<const uint8_t*>(data) + i);
    return nullptr;
}
#include "production_search.h"
#include "signatures.h"
void check(const u32* sig, u32 start, u32 end, u32 expected) {
    pinnedAddress = 0; loads = 0;
    auto* result = searchHiCode(sig, 0x08000000 + start, 0x08000000 + end);
    if (expected == UINT32_MAX) assert(!result && !pinnedAddress);
    else {
        assert(result && pinnedAddress == 0x08000000 + expected);
        assert(reinterpret_cast<uint8_t*>(result) == permanent + (expected & 4095));
        assert(!std::memcmp(result, sig, 2)); // the SWI patch fits in the pinned block
    }
}
int main() {
    for (bool mode : {false, true}) {
        reuse = mode;
        for (const auto& sig : corpus) {
            for (u32 pos = 4096 - 15; pos <= 4097; ++pos) {
                rom.assign(8192, 0xA5);
                std::memcpy(rom.data() + pos, sig, 16);
                check(sig, 0, rom.size(), pos % 4 == 0 ? pos : UINT32_MAX);
            }
            for (u32 pos : {0u, 4u, 100u, 4000u, 4080u, 4096u, 8000u}) {
                rom.assign(8192, 0xA5);
                std::memcpy(rom.data() + pos, sig, 16);
                check(sig, 0, rom.size(), pos);
                check(sig, pos + 1, rom.size(), UINT32_MAX);
                check(sig, pos, pos + 15, UINT32_MAX);
                check(sig, pos, pos + 16, pos);
            }
            rom.assign(4107, 0xA5);
            std::memcpy(rom.data() + 4092, sig, 15);
            check(sig, 0, rom.size(), UINT32_MAX);
            rom.push_back(reinterpret_cast<const uint8_t*>(sig)[15]);
            check(sig, 0, rom.size(), 4092);
            rom.assign(8192, 0xA5);
            std::memcpy(rom.data() + 4084, sig, 8); // false first two words
            std::memcpy(rom.data() + 4096, sig, 16);
            std::memcpy(rom.data() + 4200, sig, 16);
            check(sig, 0, rom.size(), 4096);
            std::memcpy(rom.data() + 4084, sig, 16);
            check(sig, 0, rom.size(), 4084); // boundary precedes next-block match
            rejectBlock = 4096;
            check(sig, 0, rom.size(), UINT32_MAX);
            rejectBlock = UINT32_MAX;
            rom.assign(8192, 0xA5);
            std::memcpy(rom.data() + 8000, sig, 16);
            for (unsigned failedLoad : {1u, 2u, 3u}) {
                rejectLoad = failedLoad;
                check(sig, 0, rom.size(), UINT32_MAX);
                assert(loads == failedLoad); // stop immediately, no pin or later reads
            }
            rejectLoad = UINT32_MAX;
            rejectPin = true; pinnedAddress = 0;
            assert(searchHiCode(sig, 0x08000000, 0x08002000) == nullptr);
            assert(pinnedAddress == 0x08000000 + 8000);
            rejectPin = false;

        }
    }
    std::cout << "search boundary, 4-byte alignment, bounded ranges, cache replacement and permanent patch backing PASS\n";
}
