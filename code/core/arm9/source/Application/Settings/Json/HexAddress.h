#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// The length overload also rejects embedded NULs in JSON strings.
inline bool tryParseHexString(const char* text, std::size_t length, uint32_t& value)
{
    if (!text) return false;
    if (length >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        text += 2;
        length -= 2;
    }
    if (length == 0 || length > 8) return false;
    uint32_t parsed = 0;
    for (std::size_t i = 0; i < length; ++i)
    {
        const char c = text[i];
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else return false;
        parsed = (parsed << 4) | digit;
    }
    value = parsed;
    return true;
}

inline bool tryParseHexString(const char* text, uint32_t& value)
{
    return text && tryParseHexString(text, std::strlen(text), value);
}
