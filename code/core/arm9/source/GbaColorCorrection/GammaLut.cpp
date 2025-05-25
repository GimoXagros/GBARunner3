#include "GammaLut.h"
#include <cmath>

// Lambda LUT generators based from this example https://stackoverflow.com/a/62699172

// Fixed gamma encode table [TARGET_GAMMA]
[[gnu::section(".ewram")]]
constexpr std::array<u8, GammaLut::TableSize> GammaLut::encodeTable = []
{
    std::array<u8, TableSize> table = {};
    for (int i = 0; i < TableSize; ++i)
    {
        double x = static_cast<double>(i) / 255.0;
        table[i] = static_cast<u8>(std::clamp(std::pow(x, TargetGamma + DarkenScreen) * 255.0, 0.0, 255.0));
    }
    return table;
}();

// Precomputed gamma decode tables for gamma [DISPLAY_GAMMA]
[[gnu::section(".ewram")]]
constexpr std::array<std::array<u8, GammaLut::TableSize>, GammaLut::GammaSteps> GammaLut::decodeTables = []
{
    std::array<std::array<u8, TableSize>, GammaSteps> tables = {};
    for (int g = 0; g < GammaSteps; ++g)
    {
        double gamma = GammaMin + GammaStep * g;
        for (int i = 0; i < TableSize; ++i)
        {
            double x = static_cast<double>(i) / 255.0;
            tables[g][i] = static_cast<u8>(std::clamp(std::pow(x, gamma) * 255.0, 0.0, 255.0));
        }
    }
    return tables;
}();

// Apply target gamma, this is a precomputed table.
u8 GammaLut::Encode(u8 value)
{
    return encodeTable[value];
}

// Apply display gamma from precomputed gamma decode tables:
// Index 0 = gamma 0.5f,
// Index 1 = gamma 0.6f,
// Index 2 = gamma 0.7f,
// Index 3 = gamma 0.8f,
// Index 4 = gamma 0.9f
// Default 0
u8 GammaLut::Decode(u8 value, u8 index)
{
    if (index >= GammaSteps)
    {
        index = GammaSteps - 1;
    }
    return decodeTables[index][value];
}
