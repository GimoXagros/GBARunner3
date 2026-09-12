#pragma once
#include "common.h"
#include "AppSettings.h"
#include <stddef.h>

// Hash numeric values in a specified byte order, never the pointer or ROM data.
inline u32 settingsAddressHash(const u32* addresses, u32 count)
{
    u32 hash = 2166136261u;
    if (!addresses && count)
        return 0;
    for (u32 i = 0; i < count; ++i)
        for (unsigned shift = 0; shift < 32; shift += 8)
            hash = (hash ^ ((addresses[i] >> shift) & 255)) * 16777619u;
    return hash;
}

// A title path contains bytes from the ROM header. Escape them so a malformed
// header cannot inject a second setting into this text report.
inline bool settingsEscapePath(char* output, size_t capacity, const char* path)
{
    const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    for (; *path; ++path)
    {
        const unsigned char c = *path;
        const bool escape = c < 32 || c >= 127 || c == '\\';
        const size_t length = escape ? 4 : 1;
        if (used + length >= capacity)
            return false;
        if (escape)
        {
            output[used++] = '\\';
            output[used++] = 'x';
            output[used++] = hex[c >> 4];
            output[used++] = hex[c & 15];
        }
        else
            output[used++] = c;
    }
    if (used >= capacity)
        return false;
    output[used] = 0;
    return true;
}

template <typename Print>
int formatSettingsSnapshot(char* output, size_t capacity, const AppSettings& settings,
    u32 gameCode, unsigned revision, bool globalLoaded, const char* escapedTitlePath,
    bool titleLoaded, Print print)
{
    const auto& r = settings.runSettings;
    const auto& d = settings.displaySettings;
    const int count = print(output, capacity,
        "GBARunner3 settings snapshot v1\n"
        "scope=boot-only; no frame trace; flicker cause unconfirmed\n"
        "gameCodeLE=0x%08X\nrevision=%u\n"
        "globalConfigPath=/_gba/gbarunner3.json\nglobalConfigLoaded=%u\n"
        "titleConfigPath=%s\ntitleConfigLoaded=%u\n"
        "enableJit=%u\njitPatchAddressCount=%u\njitPatchAddressesFNV1aLE=0x%08X\n"
        "selfModifyingPatchAddressCount=%u\nselfModifyingPatchAddressesFNV1aLE=0x%08X\n"
        "enableRomICache=%u\nenableWramICache=%u\nenableIWramDCache=%u\nenableEWramDCache=%u\n"
        "forceDSModeArm9Clock=%u\nskipBiosIntro=%u\n"
        "gbaScreen=%u\nenableCenterAndMask=%u\nborderImage=%u\n"
        "gbaColorCorrection=%u\ngamma=%u\nbrightness=%u\n"
        "centerOffsetX=%u\ncenterOffsetY=%u\nmaskWidth=%u\nmaskHeight=%u\nsaveType=%u\n",
        unsigned(gameCode), revision, unsigned(globalLoaded), escapedTitlePath, unsigned(titleLoaded),
        unsigned(r.enableJit), unsigned(r.jitPatchAddressCount),
        unsigned(settingsAddressHash(r.jitPatchAddresses.get(), r.jitPatchAddressCount)),
        unsigned(r.selfModifyingPatchAddressCount),
        unsigned(settingsAddressHash(r.selfModifyingPatchAddresses.get(), r.selfModifyingPatchAddressCount)),
        unsigned(r.enableRomInstructionCache), unsigned(r.enableWramInstructionCache),
        unsigned(r.enableIWramDataCache), unsigned(r.enableEWramDataCache),
        unsigned(r.forceDSModeArm9ClockSpeed), unsigned(r.skipBiosIntro), unsigned(d.gbaScreen),
        unsigned(d.enableCenterAndMask), unsigned(d.borderImage), unsigned(d.gbaColorCorrection),
        unsigned(d.gbaDisplayGamma), unsigned(d.gbaScreenBrightness), unsigned(d.centerOffsetX),
        unsigned(d.centerOffsetY), unsigned(d.maskWidth), unsigned(d.maskHeight),
        unsigned(settings.gameSettings.saveType));
    // mini_snprintf reports bytes actually stored, unlike the C99 formatter.
    // Treat a completely filled buffer as truncated with either implementation.
    return count >= 0 && capacity > 0 && size_t(count) < capacity - 1 ? count : -1;
}
