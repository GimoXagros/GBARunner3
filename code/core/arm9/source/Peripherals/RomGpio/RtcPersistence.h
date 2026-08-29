#pragma once

#include <stddef.h>
#include <stdint.h>

namespace RtcPersistence
{

constexpr uint32_t STATE_MAGIC = 0x54523347; // "G3RT" in little endian
constexpr uint16_t STATE_VERSION = 1;
constexpr uint64_t CYCLE_SECONDS = 36525ULL * 24 * 60 * 60;

struct Identity
{
    uint32_t gameCode;
    uint32_t romSize;
    uint32_t headerHash;
};

struct StateFile
{
    uint32_t magic;
    uint16_t version;
    uint16_t payloadLength;
    uint32_t gameCode;
    uint32_t romSize;
    uint32_t headerHash;
    uint32_t sequence;
    uint32_t hostSecondsSince2000;
    uint32_t rtcSecondsSince2000;
    int16_t weekDayOffset;
    uint16_t statusRegister;
    uint16_t intRegister;
    uint16_t flags;
    uint32_t checksum;
};

static_assert(sizeof(StateFile) == 44);

constexpr uint16_t STATE_PAYLOAD_LENGTH =
    offsetof(StateFile, checksum) - offsetof(StateFile, gameCode);

[[gnu::section(".ewram"), gnu::noinline]] static inline uint32_t CalculateFnv1a(const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i)
    {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

[[gnu::section(".ewram"), gnu::noinline]] static inline uint32_t CalculateChecksum(const StateFile& state)
{
    return CalculateFnv1a(&state, offsetof(StateFile, checksum));
}

[[gnu::section(".ewram"), gnu::noinline]] static inline bool MatchesIdentity(const StateFile& state, const Identity& identity)
{
    return state.gameCode == identity.gameCode &&
        state.romSize == identity.romSize &&
        state.headerHash == identity.headerHash;
}

[[gnu::section(".ewram"), gnu::noinline]] static inline bool Validate(const StateFile& state, const Identity& identity)
{
    return state.magic == STATE_MAGIC &&
        state.version == STATE_VERSION &&
        state.payloadLength == STATE_PAYLOAD_LENGTH &&
        state.flags == 0 &&
        MatchesIdentity(state, identity) &&
        state.checksum == CalculateChecksum(state);
}

[[gnu::section(".ewram"), gnu::noinline]] static inline StateFile CreateState(
    const Identity& identity,
    uint32_t sequence,
    uint32_t hostSecondsSince2000,
    uint32_t rtcSecondsSince2000,
    int16_t weekDayOffset,
    uint16_t statusRegister,
    uint16_t intRegister)
{
    StateFile state { };
    state.magic = STATE_MAGIC;
    state.version = STATE_VERSION;
    state.payloadLength = STATE_PAYLOAD_LENGTH;
    state.gameCode = identity.gameCode;
    state.romSize = identity.romSize;
    state.headerHash = identity.headerHash;
    state.sequence = sequence;
    state.hostSecondsSince2000 = hostSecondsSince2000;
    state.rtcSecondsSince2000 = rtcSecondsSince2000;
    state.weekDayOffset = weekDayOffset;
    state.statusRegister = statusRegister;
    state.intRegister = intRegister;
    state.checksum = CalculateChecksum(state);
    return state;
}

[[gnu::section(".ewram"), gnu::noinline]] static inline uint32_t AdvanceGameSeconds(
    uint32_t storedGameSeconds,
    uint32_t storedHostSeconds,
    uint32_t currentHostSeconds)
{
    const uint64_t elapsed = currentHostSeconds >= storedHostSeconds
        ? static_cast<uint64_t>(currentHostSeconds - storedHostSeconds)
        : 0;
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(storedGameSeconds) + elapsed) % CYCLE_SECONDS);
}

[[gnu::section(".ewram"), gnu::noinline]] static inline int64_t RestoreOffset(
    uint32_t storedGameSeconds,
    uint32_t storedHostSeconds,
    uint32_t currentHostSeconds)
{
    return static_cast<int64_t>(AdvanceGameSeconds(
        storedGameSeconds, storedHostSeconds, currentHostSeconds)) -
        currentHostSeconds;
}

[[gnu::section(".ewram"), gnu::noinline]] static inline bool IsSequenceNewer(uint32_t candidate, uint32_t selected)
{
    return static_cast<int32_t>(candidate - selected) > 0;
}

}
