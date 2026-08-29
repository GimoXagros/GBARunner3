#include "common.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "Peripherals/RomGpio/RtcPersistence.h"

using namespace testing;

namespace
{

constexpr RtcPersistence::Identity IDENTITY_A
{
    0x45455042, // BPEE
    32 * 1024 * 1024,
    0x12345678
};

constexpr RtcPersistence::Identity IDENTITY_B
{
    0x45565841, // AXVE
    16 * 1024 * 1024,
    0x87654321
};

RtcPersistence::StateFile CreateValidState()
{
    return RtcPersistence::CreateState(
        IDENTITY_A, 7, 1000, 2500, -2, 0x40, 0x1234);
}

}

TEST(RtcPersistence, FormatHasStableSizeAndMetadata)
{
    auto state = CreateValidState();

    EXPECT_THAT(sizeof(state), Eq(44u));
    EXPECT_THAT(state.magic, Eq(RtcPersistence::STATE_MAGIC));
    EXPECT_THAT(state.version, Eq(RtcPersistence::STATE_VERSION));
    EXPECT_THAT(state.payloadLength, Eq(RtcPersistence::STATE_PAYLOAD_LENGTH));
}

TEST(RtcPersistence, ValidStateRoundTrips)
{
    auto state = CreateValidState();

    EXPECT_TRUE(RtcPersistence::Validate(state, IDENTITY_A));
    EXPECT_THAT(state.sequence, Eq(7u));
    EXPECT_THAT(state.hostSecondsSince2000, Eq(1000u));
    EXPECT_THAT(state.rtcSecondsSince2000, Eq(2500u));
    EXPECT_THAT(state.weekDayOffset, Eq(-2));
    EXPECT_THAT(state.statusRegister, Eq(0x40));
    EXPECT_THAT(state.intRegister, Eq(0x1234));
}

TEST(RtcPersistence, CorruptionIsRejected)
{
    auto state = CreateValidState();
    reinterpret_cast<u8*>(&state)[24] ^= 0x80;

    EXPECT_FALSE(RtcPersistence::Validate(state, IDENTITY_A));
}

TEST(RtcPersistence, InvalidFormatMetadataIsRejected)
{
    auto state = CreateValidState();
    state.payloadLength--;
    state.checksum = RtcPersistence::CalculateChecksum(state);
    EXPECT_FALSE(RtcPersistence::Validate(state, IDENTITY_A));

    state = CreateValidState();
    state.version++;
    state.checksum = RtcPersistence::CalculateChecksum(state);
    EXPECT_FALSE(RtcPersistence::Validate(state, IDENTITY_A));
}

TEST(RtcPersistence, DifferentGameIdentityIsRejected)
{
    auto state = CreateValidState();

    EXPECT_FALSE(RtcPersistence::Validate(state, IDENTITY_B));

    auto changedSize = IDENTITY_A;
    changedSize.romSize++;
    EXPECT_FALSE(RtcPersistence::Validate(state, changedSize));

    auto changedHeader = IDENTITY_A;
    changedHeader.headerHash ^= 1;
    EXPECT_FALSE(RtcPersistence::Validate(state, changedHeader));
}

TEST(RtcPersistence, ForwardHostTimeAdvancesGameClock)
{
    EXPECT_THAT(RtcPersistence::AdvanceGameSeconds(2500, 1000, 1060), Eq(2560u));
    EXPECT_THAT(RtcPersistence::RestoreOffset(2500, 1000, 1060), Eq(1500));
}

TEST(RtcPersistence, BackwardHostTimeRebasesWithoutForwardWrap)
{
    EXPECT_THAT(RtcPersistence::AdvanceGameSeconds(2500, 1000, 900), Eq(2500u));
    EXPECT_THAT(RtcPersistence::RestoreOffset(2500, 1000, 900), Eq(1600));
}

TEST(RtcPersistence, GameClockWrapsAtEndOfRtcCycle)
{
    const u32 lastSecond = static_cast<u32>(RtcPersistence::CYCLE_SECONDS - 1);
    EXPECT_THAT(RtcPersistence::AdvanceGameSeconds(lastSecond, 1000, 1002), Eq(1u));
}

TEST(RtcPersistence, NewestRecoverySequenceWins)
{
    EXPECT_TRUE(RtcPersistence::IsSequenceNewer(8, 7));
    EXPECT_FALSE(RtcPersistence::IsSequenceNewer(7, 8));
    EXPECT_TRUE(RtcPersistence::IsSequenceNewer(0, 0xFFFFFFFF));
}
