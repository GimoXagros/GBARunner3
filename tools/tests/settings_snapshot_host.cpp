#include <cassert>
#include <cstring>
#include <string>
#include "Application/Settings/SettingsSnapshot.h"
#include "mini-printf.h"

int main()
{
    AppSettings settings;
    char output[2048];
    auto render = [&](size_t capacity = sizeof(output)) {
        return formatSettingsSnapshot(output, capacity, settings, 0x44434241, 2,
            false, "/_gba/configs/ABCD02.json", true, mini_snprintf);
    };
    assert(render() > 0);
    std::string initial = output;
    for (auto expected : {"gameCodeLE=0x44434241\n", "revision=2\n",
        "globalConfigLoaded=0\n", "titleConfigLoaded=1\n", "enableCenterAndMask=1\n",
        "jitPatchAddressCount=0\n", "jitPatchAddressesFNV1aLE=0x811C9DC5\n"})
        assert(initial.find(expected) != std::string::npos);
    const u32 values[] = {0x12345678u, 0x9ABCDEF0u};
    // Independent known FNV-1a vector for bytes 78 56 34 12 F0 DE BC 9A.
    assert(settingsAddressHash(values, 2) == 0xD117DCB5u);
    assert(settingsAddressHash(nullptr, 1) == 0);
    settings.runSettings.jitPatchAddresses = std::make_unique<u32[]>(2);
    std::memcpy(settings.runSettings.jitPatchAddresses.get(), values, sizeof(values));
    settings.runSettings.jitPatchAddressCount = 2;
    settings.displaySettings.enableCenterAndMask = false;
    assert(render() > 0);
    assert(std::string(output).find("enableCenterAndMask=0\n") != std::string::npos);
    assert(std::string(output).find("jitPatchAddressCount=2\n") != std::string::npos);
    assert(std::string(output).find("12345678") == std::string::npos);
    assert(settings.runSettings.jitPatchAddresses[0] == values[0]);
    assert(render(16) == -1);
    assert(render(0) == -1);
    char path[80];
    assert(settingsEscapePath(path, sizeof(path), "/A\nB\\C\xFF"));
    assert(std::string(path) == "/A\\x0AB\\x5CC\\xFF");
    assert(!settingsEscapePath(path, 3, "abcd"));
    assert(!settingsEscapePath(path, 0, ""));
}
