#include "common.h"
#include "gtest/gtest.h"
#include "MemoryEmulator/RomDefs.h"
#include "SdCache/SdCacheDefs.h"
#include "JitPatcher/JitCommon.h"

TEST(JitAddressTests, KeepsBranchInsideLinearRomWindow)
{
    EXPECT_EQ(0x02201000u, jit_resolveArmBranchTargetAddress(0x02200000u, 0x02201000u));
}

TEST(JitAddressTests, KeepsLastAddressInsideLinearRomWindow)
{
    EXPECT_EQ(0x023FFFFCu, jit_resolveArmBranchTargetAddress(0x023FFFF8u, 0x023FFFFCu));
}

TEST(JitAddressTests, RestoresFirstAddressOutsideLinearRomWindow)
{
    EXPECT_EQ(0x08200000u, jit_resolveArmBranchTargetAddress(0x023FFFFCu, 0x02400000u));
}

TEST(JitAddressTests, RestoresHighRomTargetFromRelocatedBranch)
{
    EXPECT_EQ(0x09ED4000u, jit_resolveArmBranchTargetAddress(0x02200000u, 0x040D4000u));
}

TEST(JitAddressTests, KeepsTargetForBranchAlreadyExecutingInHighRom)
{
    EXPECT_EQ(0x09ED5000u, jit_resolveArmBranchTargetAddress(0x09ED4000u, 0x09ED5000u));
}

TEST(JitAddressTests, AppliesArmPcPlusEightWithoutOffByFour)
{
    EXPECT_EQ(0x02200008u, jit_calculateArmBranchTargetAddress(0x02200000u, 0xEA000000u));
}

TEST(JitAddressTests, ResolvesLinearBoundaryBranch)
{
    EXPECT_EQ(0x08200000u, jit_calculateArmBranchTargetAddress(0x023FFFF8u, 0xEA000000u));
}

TEST(JitAddressTests, ResolvesKingdomHeartsEntryBranch)
{
    EXPECT_EQ(0x09ED4000u, jit_calculateArmBranchTargetAddress(0x02200000u, 0xEA7B4FFEu));
}
