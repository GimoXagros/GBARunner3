#include "Application/Settings/Json/HexAddress.h"

extern "C" bool hex_parse(const char* text, unsigned length, uint32_t* output)
{
    return tryParseHexString(text, length, *output);
}
