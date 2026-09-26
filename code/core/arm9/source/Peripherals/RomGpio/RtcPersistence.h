#pragma once

#include <stddef.h>
#include <stdint.h>

namespace RtcPersistence
{

constexpr uint32_t STATE_MAGIC = 0x54523347; // "G3RT" in little endian
constexpr uint16_t STATE_VERSION = 1;
constexpr uint64_t CYCLE_SECONDS = 36525ULL * 24 * 60 * 60;
constexpr uint32_t STATE_V2_MAGIC = 0x32523347; // G3R2
constexpr uint16_t STATE_V2_VERSION = 2;
constexpr uint16_t STATE_V2_PAYLOAD_LENGTH = 188;

enum class FileStatus : uint8_t
{
    Missing, ValidLegacy, ValidCurrent, Corrupt, UnsupportedVersion,
    IdentityMismatch, IoError
};

enum class LoadStatus : uint8_t
{
    Ready, FreshInitialized, LegacyFound, Corrupt, UnsupportedVersion,
    IdentityMismatch, IoError, Conflict, WriteError
};

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

struct StateFileV2
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
    uint16_t policy;
    uint8_t phase;
    uint8_t legacyPresence;
    uint8_t selectedLegacyRole;
    uint8_t reserved[3];
    uint32_t selectedLegacySequence;
    uint32_t legacyHashes[3];
    uint8_t legacyBytes[3][sizeof(StateFile)];
    uint32_t checksum;
};

static_assert(sizeof(StateFileV2) == 200);
static_assert(offsetof(StateFileV2, checksum) == 196);
static_assert(offsetof(StateFileV2, legacyBytes) == 64);

constexpr uint16_t POLICY_FRESH = 0;
constexpr uint16_t POLICY_ADOPT_SNAPSHOT = 1;
constexpr uint8_t PHASE_PENDING = 0;
constexpr uint8_t PHASE_READY = 1;

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

[[gnu::section(".ewram"), gnu::noinline]] static inline bool ValidateLegacy(
    const StateFile& state, const Identity& identity)
{
    return Validate(state, identity) &&
        state.hostSecondsSince2000 < CYCLE_SECONDS &&
        state.rtcSecondsSince2000 < CYCLE_SECONDS &&
        state.weekDayOffset >= -6 && state.weekDayOffset <= 6 &&
        (state.statusRegister & ~uint16_t(0xEA)) == 0;
}

[[gnu::section(".ewram"), gnu::noinline]] static inline uint32_t CalculateV2Checksum(
    const StateFileV2& state)
{
    return CalculateFnv1a(&state, offsetof(StateFileV2, checksum));
}

[[gnu::section(".ewram"), gnu::noinline]] static inline bool SameLineage(
    const StateFileV2& left, const StateFileV2& right)
{
    if (left.gameCode != right.gameCode || left.romSize != right.romSize ||
        left.headerHash != right.headerHash || left.policy != right.policy ||
        left.legacyPresence != right.legacyPresence ||
        left.selectedLegacyRole != right.selectedLegacyRole ||
        left.selectedLegacySequence != right.selectedLegacySequence)
        return false;
    for (unsigned i = 0; i < 3; ++i)
    {
        if (left.legacyHashes[i] != right.legacyHashes[i]) return false;
        for (unsigned j = 0; j < sizeof(StateFile); ++j)
            if (left.legacyBytes[i][j] != right.legacyBytes[i][j]) return false;
    }
    return true;
}

[[gnu::section(".ewram"), gnu::noinline]] static inline bool ValidateV2(
    const StateFileV2& state, const Identity& identity)
{
    if (state.magic != STATE_V2_MAGIC || state.version != STATE_V2_VERSION ||
        state.payloadLength != STATE_V2_PAYLOAD_LENGTH || state.flags != 0 ||
        state.reserved[0] != 0 || state.reserved[1] != 0 || state.reserved[2] != 0 ||
        state.gameCode != identity.gameCode || state.romSize != identity.romSize ||
        state.headerHash != identity.headerHash ||
        state.checksum != CalculateV2Checksum(state) ||
        state.hostSecondsSince2000 >= CYCLE_SECONDS ||
        state.rtcSecondsSince2000 >= CYCLE_SECONDS ||
        state.weekDayOffset < -6 || state.weekDayOffset > 6 ||
        (state.statusRegister & ~uint16_t(0xEA)) != 0 ||
        state.phase > PHASE_READY || state.legacyPresence > 7)
        return false;

    if (state.policy == POLICY_FRESH)
    {
        if (state.phase != PHASE_READY || state.legacyPresence != 0 ||
            state.selectedLegacyRole != 0xFF || state.selectedLegacySequence != 0)
            return false;
        for (unsigned i = 0; i < 3; ++i)
        {
            if (state.legacyHashes[i] != 0) return false;
            for (unsigned j = 0; j < sizeof(StateFile); ++j)
                if (state.legacyBytes[i][j] != 0) return false;
        }
        return true;
    }
    if (state.policy != POLICY_ADOPT_SNAPSHOT || state.legacyPresence == 0 ||
        state.selectedLegacyRole > 2 ||
        (state.legacyPresence & (1u << state.selectedLegacyRole)) == 0 ||
        (state.phase == PHASE_PENDING &&
            (state.sequence != 0 || state.hostSecondsSince2000 != 0)))
        return false;

    StateFile selected { };
    for (unsigned i = 0; i < 3; ++i)
    {
        const bool present = (state.legacyPresence & (1u << i)) != 0;
        if (!present)
        {
            if (state.legacyHashes[i] != 0) return false;
            for (unsigned j = 0; j < sizeof(StateFile); ++j)
                if (state.legacyBytes[i][j] != 0) return false;
            continue;
        }
        if (state.legacyHashes[i] != CalculateFnv1a(state.legacyBytes[i], sizeof(StateFile)))
            return false;
        StateFile source { };
        auto* destination = reinterpret_cast<uint8_t*>(&source);
        for (unsigned j = 0; j < sizeof(StateFile); ++j)
            destination[j] = state.legacyBytes[i][j];
        if (!ValidateLegacy(source, identity)) return false;
        if (i == state.selectedLegacyRole) selected = source;
    }
    return state.selectedLegacySequence == selected.sequence &&
        (state.phase != PHASE_PENDING ||
            (state.rtcSecondsSince2000 == selected.rtcSecondsSince2000 &&
             state.weekDayOffset == selected.weekDayOffset &&
             state.statusRegister == selected.statusRegister &&
             state.intRegister == selected.intRegister));
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
