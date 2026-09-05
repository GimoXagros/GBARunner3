#include "common.h"
#include <gtest/gtest.h>
#include "Application/Settings/Json/HexAddress.h"

TEST(HexAddress, RejectsMalformedAndPreservesOutput)
{
    const char* invalid[] = {nullptr, "", "0x", "0X", "123456789", "0x100000000", "-1", "+1", " 1", "1 ", "1g", "z"};
    for (auto text : invalid)
    {
        uint32_t value = 0x12345678;
        EXPECT_FALSE(tryParseHexString(text, value));
        EXPECT_EQ(value, 0x12345678u);
    }
    uint32_t value = 42;
    EXPECT_FALSE(tryParseHexString("1\0bad", 5, value));
    EXPECT_EQ(value, 42u);
}

TEST(HexAddress, AcceptsFullUint32RangeAndBothPrefixes)
{
    const char* texts[] = {"0", "1", "FFFFFFFF", "0x08000000", "0X08000000", "aBcDeF"};
    const uint32_t expected[] = {0, 1, 0xFFFFFFFF, 0x08000000, 0x08000000, 0xABCDEF};
    for (unsigned i = 0; i < 6; ++i)
    {
        uint32_t value = 42;
        EXPECT_TRUE(tryParseHexString(texts[i], value));
        EXPECT_EQ(value, expected[i]);
    }
}
