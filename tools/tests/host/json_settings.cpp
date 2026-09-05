// Compile the real serializer and settings; replace only platform/file facilities.
#include "Application/Settings/Json/JsonAppSettingsSerializer.cpp"
#include <cassert>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc == 2) {
        JsonAppSettingsSerializer serializer;
        AppSettings settings;
        assert(serializer.TryDeserialize(argv[1], settings));
        for (u32 i = 0; i < settings.runSettings.jitPatchAddressCount; ++i)
            std::cout << settings.runSettings.jitPatchAddresses[i] << '\n';
        for (u32 i = 0; i < settings.runSettings.selfModifyingPatchAddressCount; ++i)
            std::cout << settings.runSettings.selfModifyingPatchAddresses[i] << '\n';
        return 0;
    }
    const char* invalid[] = {"null", "false", "123", "{}", "[]", "\"\"", "\"0x\"", "\"0X\"",
        "\"123456789\"", "\"0x100000000\"", "\"-1\"", "\"+1\"", "\" 1\"", "\"1 \"",
        "\"1g\"", "\"g\"", "\"0x1z\"", "\"1\\u0000garbage\""};
    for (const char* key : {"jitPatchAddresses", "selfModifyingPatchAddresses"}) {
        for (auto bad : invalid) {
            for (bool array : {true, false}) {
                if (!array && std::string(bad) == "[]") continue; // empty array is valid
                AppSettings settings;
                auto& run = settings.runSettings;
                run.jitPatchAddresses = std::make_unique<u32[]>(1);
                run.jitPatchAddresses[0] = 0x1234;
                run.jitPatchAddressCount = 1;
                run.selfModifyingPatchAddresses = std::make_unique<u32[]>(1);
                run.selfModifyingPatchAddresses[0] = 0x5678;
                run.selfModifyingPatchAddressCount = 1;
                auto* jit = run.jitPatchAddresses.get();
                auto* sm = run.selfModifyingPatchAddresses.get();
                std::string value = array ? std::string("[\"1\",\"2\",") + bad + ",\"3\"]" : bad;
                std::string input = std::string("{\"runSettings\":{\"enableJit\":false,\"") + key + "\":" + value + "}}";
                DynamicJsonDocument json(4096);
                assert(deserializeJson(json, input) == DeserializationError::Ok);
                readJson(json, settings);
                assert(!run.enableJit); // independent valid setting still applies
                assert(run.jitPatchAddresses.get() == jit && run.jitPatchAddressCount == 1 && jit[0] == 0x1234);
                assert(run.selfModifyingPatchAddresses.get() == sm && run.selfModifyingPatchAddressCount == 1 && sm[0] == 0x5678);
            }
        }
        AppSettings settings;
        DynamicJsonDocument json(4096);
        std::string input = std::string("{\"runSettings\":{\"") + key + "\":[\"0\",\"1\",\"FFFFFFFF\",\"0x08000000\",\"0X08000000\",\"aBcDeF\"]}}";
        assert(deserializeJson(json, input) == DeserializationError::Ok);
        readJson(json, settings);
        auto& run = settings.runSettings;
        auto* values = key[0] == 'j' ? run.jitPatchAddresses.get() : run.selfModifyingPatchAddresses.get();
        assert((key[0] == 'j' ? run.jitPatchAddressCount : run.selfModifyingPatchAddressCount) == 6);
        const u32 expected[] = {0, 1, 0xFFFFFFFF, 0x08000000, 0x08000000, 0xABCDEF};
        for (int i = 0; i < 6; ++i) assert(values[i] == expected[i]);
        input = std::string("{\"runSettings\":{\"") + key + "\":[]}}";
        assert(deserializeJson(json, input) == DeserializationError::Ok);
        readJson(json, settings);
        assert((key[0] == 'j' ? run.jitPatchAddressCount : run.selfModifyingPatchAddressCount) == 0);
    }
    std::cout << "serializer: malformed elements/types, atomicity, preservation, valid and empty arrays PASS\n";
}
