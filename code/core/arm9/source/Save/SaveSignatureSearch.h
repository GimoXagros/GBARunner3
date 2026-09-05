#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>

// Addresses are logical primary GamePak addresses in a half-open search range.
// getBlock returns a block base that may be invalidated by the next getBlock.
// The return value never exposes a temporary cache pointer.
template<class GetBlock, class FastSearch>
uint32_t sav_findSignature16(const uint32_t* signature, uint32_t start, uint32_t end,
    GetBlock getBlock, FastSearch fastSearch)
{
    constexpr uint32_t blockSize = 4096;
    constexpr uint32_t notFound = UINT32_MAX;
    if (start >= end || end - start < 16) return notFound;
    start = (start + 3) & ~3u;
    for (uint32_t blockAddress = start & ~(blockSize - 1); blockAddress < end; blockAddress += blockSize)
    {
        const uint32_t first = std::max(start, blockAddress);
        const uint32_t blockEnd = std::min(end, blockAddress + blockSize);
        const auto* block = static_cast<const uint8_t*>(getBlock(blockAddress));
        if (!block) return notFound;
        const uint32_t length = blockEnd - first;
        if (length >= 44)
        {
            // mem_fastSearch16 requires a full 32-byte load plus 12-byte lookahead.
            const auto* data = reinterpret_cast<const uint32_t*>(block + first - blockAddress);
            const auto* found = fastSearch(data, length & ~3u, signature);
            if (found)
                return first + static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(found)
                    - reinterpret_cast<const uint8_t*>(data));
        }
        else if (length >= 16)
        {
            for (uint32_t address = first; address <= blockEnd - 16; address += 4)
                if (std::memcmp(block + address - blockAddress, signature, 16) == 0)
                    return address;
        }

        // Copy before fetching the next block: even a one-slot cache is safe.
        const uint32_t boundary = blockAddress + blockSize;
        const uint32_t tailStart = std::max(first, boundary - 12);
        if (boundary >= end || end - tailStart < 16) continue;
        alignas(4) uint8_t overlap[24];
        const uint32_t tailLength = boundary - tailStart;
        std::memcpy(overlap, block + tailStart - blockAddress, tailLength);
        const auto* next = static_cast<const uint8_t*>(getBlock(boundary));
        if (!next) return notFound;
        const uint32_t headLength = std::min<uint32_t>(12, end - boundary);
        std::memcpy(overlap + tailLength, next, headLength);
        for (uint32_t offset = 0; offset < tailLength && offset + 16 <= tailLength + headLength; offset += 4)
            if (std::memcmp(overlap + offset, signature, 16) == 0)
                return tailStart + offset;
    }
    return notFound;
}
