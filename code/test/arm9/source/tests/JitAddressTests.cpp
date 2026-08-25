#include "common.h"
#include "gtest/gtest.h"
#include "MemoryEmulator/RomDefs.h"
#include "SdCache/SdCacheDefs.h"
#include "JitPatcher/JitCommon.h"

TEST(JitAddressTests, KeepsBranchInsideLinearRomWindow)
{
    EXPECT_EQ(0x02201000u, jit_resolveArmBranchTargetAddress(0x02200000u, 0x02201000u));
}

TEST(JitAddressTests, RestoresHighRomTargetFromRelocatedBranch)
{
    EXPECT_EQ(0x09ED4000u, jit_resolveArmBranchTargetAddress(0x02200000u, 0x040D4000u));
}

TEST(JitAddressTests, KeepsTargetForBranchAlreadyExecutingInHighRom)
{
    EXPECT_EQ(0x09ED5000u, jit_resolveArmBranchTargetAddress(0x09ED4000u, 0x09ED5000u));
}
