#include "common.h"
#include <libtwl/mem/memSwap.h>
#include <algorithm>
#include <memory>
#include "ArduinoJson.h"
#include "../AppSettings.h"
#include "JsonAppSettingsSerializer.h"
#include "HexAddress.h"

#pragma GCC optimize("Os")

#define JSON_SETTINGS_EWRAM [[gnu::section(".ewram"), gnu::noinline]]

#define KEY_DISPLAY_SETTINGS                        "displaySettings"
#define KEY_DISPLAY_SETTINGS_GBA_SCREEN             "gbaScreen"
#define KEY_DISPLAY_SETTINGS_GBA_COLOR_CORRECTION   "gbaColorCorrection"
#define KEY_DISPLAY_SETTINGS_GBA_DISPLAY_GAMMA      "gbaDisplayGamma"
#define KEY_DISPLAY_SETTINGS_GBA_SCREEN_BRIGHTNESS  "gbaScreenBrightness"
#define KEY_DISPLAY_SETTINGS_ENABLE_CENTER_AND_MASK "enableCenterAndMask"
#define KEY_DISPLAY_SETTINGS_CENTER_OFFSET_X        "centerOffsetX"
#define KEY_DISPLAY_SETTINGS_CENTER_OFFSET_Y        "centerOffsetY"
#define KEY_DISPLAY_SETTINGS_MASK_WIDTH             "maskWidth"
#define KEY_DISPLAY_SETTINGS_MASK_HEIGHT            "maskHeight"
#define KEY_DISPLAY_SETTINGS_BORDER_IMAGE           "borderImage"

#define KEY_RUN_SETTINGS                                    "runSettings"
#define KEY_RUN_SETTINGS_ENABLE_JIT                         "enableJit"
#define KEY_RUN_SETTINGS_JIT_PATCH_ADDRESSES                "jitPatchAddresses"
#define KEY_RUN_SETTINGS_ENABLE_ROM_ICACHE                  "enableRomICache"
#define KEY_RUN_SETTINGS_ENABLE_WRAM_ICACHE                 "enableWramICache"
#define KEY_RUN_SETTINGS_ENABLE_IWRAM_DCACHE                "enableIWramDCache"
#define KEY_RUN_SETTINGS_ENABLE_EWRAM_DCACHE                "enableEWramDCache"
#define KEY_RUN_SETTINGS_ARM9_CLOCK_SPEED                   "forceDSModeArm9Clock"
#define KEY_RUN_SETTINGS_SELF_MODIFYING_PATCH_ADDRESSES     "selfModifyingPatchAddresses"
#define KEY_RUN_SETTINGS_SKIP_BIOS_INTRO                    "skipBiosIntro"

#define KEY_GAME_SETTINGS                           "gameSettings"
#define KEY_GAME_SETTINGS_SAVE_TYPE                 "saveType"

#define ENUM_STRING_GBA_SCREEN_TOP                  "top"
#define ENUM_STRING_GBA_SCREEN_BOTTOM               "bottom"

#define ENUM_STRING_GBA_COLOR_CORRECTION_NONE       "none"
#define ENUM_STRING_GBA_COLOR_CORRECTION_AGB_001    "agb001"
#define ENUM_STRING_GBA_COLOR_CORRECTION_AGS_101    "ags101"
#define ENUM_STRING_GBA_COLOR_CORRECTION_OXY_001    "oxy001"
#define ENUM_STRING_GBA_COLOR_CORRECTION_NTR_001    "ntr001"
#define ENUM_STRING_GBA_COLOR_CORRECTION_USG_001    "usg001"
#define ENUM_STRING_GBA_COLOR_CORRECTION_PSP_01G    "psp01g"
#define ENUM_STRING_GBA_COLOR_CORRECTION_NSO_IPS    "nswIps"
#define ENUM_STRING_GBA_COLOR_CORRECTION_NSO_OLED   "nswOle"
#define ENUM_STRING_GBA_COLOR_CORRECTION_VBA        "vbaEmu"
#define ENUM_STRING_GBA_COLOR_CORRECTION_NOCASH     "noCash"
#define ENUM_STRING_GBA_COLOR_CORRECTION_MGBA       "mGba01"

#define ENUM_STRING_GBA_BORDER_IMAGE_NONE           "none"
#define ENUM_STRING_GBA_BORDER_IMAGE_DEFAULT        "default"
#define ENUM_STRING_GBA_BORDER_IMAGE_GAME           "game"

#define ENUM_STRING_GBA_SAVE_TYPE_AUTO              "auto"
#define ENUM_STRING_GBA_SAVE_TYPE_NONE              "none"

JSON_SETTINGS_EWRAM static bool tryParseGbaScreen(const char* gbaScreenString, GbaScreen& gbaScreen)
{
    if (!gbaScreenString)
        return false;

    if (!strcasecmp(gbaScreenString, ENUM_STRING_GBA_SCREEN_TOP))
        gbaScreen = GbaScreen::Top;
    else if (!strcasecmp(gbaScreenString, ENUM_STRING_GBA_SCREEN_BOTTOM))
        gbaScreen = GbaScreen::Bottom;
    else
        return false;

    return true;
}

JSON_SETTINGS_EWRAM static bool tryParseGbaColorCorrection(const char* gbaColorCorrectionString, GbaColorCorrection& gbaColorCorrection)
{
    if (!gbaColorCorrectionString)
        return false;

    if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_NONE))
        gbaColorCorrection = GbaColorCorrection::None;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_AGB_001))
        gbaColorCorrection = GbaColorCorrection::Agb001;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_AGS_101))
        gbaColorCorrection = GbaColorCorrection::Ags101;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_OXY_001))
        gbaColorCorrection = GbaColorCorrection::Oxy001;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_NTR_001))
        gbaColorCorrection = GbaColorCorrection::Ntr001;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_USG_001))
        gbaColorCorrection = GbaColorCorrection::Usg001;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_PSP_01G))
        gbaColorCorrection = GbaColorCorrection::Psp01g;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_NSO_IPS))
        gbaColorCorrection = GbaColorCorrection::NswIps;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_NSO_OLED))
        gbaColorCorrection = GbaColorCorrection::NswOle;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_VBA))
        gbaColorCorrection = GbaColorCorrection::VbaEmu;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_NOCASH))
        gbaColorCorrection = GbaColorCorrection::NoCash;
    else if (!strcasecmp(gbaColorCorrectionString, ENUM_STRING_GBA_COLOR_CORRECTION_MGBA))
        gbaColorCorrection = GbaColorCorrection::mGba01;
    else
        return false;

    return true;
}

JSON_SETTINGS_EWRAM static bool tryParseGbaBorderImage(const char* gbaBorderImageString, GbaBorderImage& gbaBorderImage)
{
    if (!gbaBorderImageString)
        return false;

    if (!strcasecmp(gbaBorderImageString, ENUM_STRING_GBA_BORDER_IMAGE_NONE))
        gbaBorderImage = GbaBorderImage::None;
    else if (!strcasecmp(gbaBorderImageString, ENUM_STRING_GBA_BORDER_IMAGE_DEFAULT))
        gbaBorderImage = GbaBorderImage::Default;
    else if (!strcasecmp(gbaBorderImageString, ENUM_STRING_GBA_BORDER_IMAGE_GAME))
        gbaBorderImage = GbaBorderImage::Game;
    else
        return false;

    return true;
}

JSON_SETTINGS_EWRAM static bool tryParseGbaSaveType(const char* gbaSaveTypeString, GbaSaveType& gbaSaveType)
{
    if (!gbaSaveTypeString)
        return false;

    if (!strcasecmp(gbaSaveTypeString, ENUM_STRING_GBA_SAVE_TYPE_AUTO))
        gbaSaveType = GbaSaveType::Auto;
    else if (!strcasecmp(gbaSaveTypeString, ENUM_STRING_GBA_SAVE_TYPE_NONE))
        gbaSaveType = GbaSaveType::None;
    else
        return false;

    return true;
}

JSON_SETTINGS_EWRAM static void readBoolSetting(const JsonVariantConst& jsonValue, bool16& setting)
{
    setting = jsonValue | static_cast<bool>(setting);
}

JSON_SETTINGS_EWRAM static void readDisplaySettings(const JsonObjectConst& json, DisplaySettings& displaySettings)
{
    if (json.isNull())
        return;

    tryParseGbaScreen(json[KEY_DISPLAY_SETTINGS_GBA_SCREEN], displaySettings.gbaScreen);
    tryParseGbaColorCorrection(json[KEY_DISPLAY_SETTINGS_GBA_COLOR_CORRECTION], displaySettings.gbaColorCorrection);
    if (json[KEY_DISPLAY_SETTINGS_GBA_DISPLAY_GAMMA].is<int>())
    {
        displaySettings.gbaDisplayGamma = std::clamp(json[KEY_DISPLAY_SETTINGS_GBA_DISPLAY_GAMMA].as<int>(),
            DISPLAY_SETTINGS_GBA_DISPLAY_GAMMA_MIN, DISPLAY_SETTINGS_GBA_DISPLAY_GAMMA_MAX);
    }
    if (json[KEY_DISPLAY_SETTINGS_GBA_SCREEN_BRIGHTNESS].is<int>())
    {
        displaySettings.gbaScreenBrightness = std::clamp(json[KEY_DISPLAY_SETTINGS_GBA_SCREEN_BRIGHTNESS].as<int>(),
            DISPLAY_SETTINGS_GBA_SCREEN_BRIGHTNESS_MIN, DISPLAY_SETTINGS_GBA_SCREEN_BRIGHTNESS_MAX);
    }

    readBoolSetting(json[KEY_DISPLAY_SETTINGS_ENABLE_CENTER_AND_MASK], displaySettings.enableCenterAndMask);
    displaySettings.centerOffsetX
        = json[KEY_DISPLAY_SETTINGS_CENTER_OFFSET_X] | displaySettings.centerOffsetX;
    displaySettings.centerOffsetY
        = json[KEY_DISPLAY_SETTINGS_CENTER_OFFSET_Y] | displaySettings.centerOffsetY;
    displaySettings.maskWidth
        = json[KEY_DISPLAY_SETTINGS_MASK_WIDTH] | displaySettings.maskWidth;
    displaySettings.maskHeight
        = json[KEY_DISPLAY_SETTINGS_MASK_HEIGHT] | displaySettings.maskHeight;
    tryParseGbaBorderImage(json[KEY_DISPLAY_SETTINGS_BORDER_IMAGE], displaySettings.borderImage);
}

// Validate before allocating or modifying either the pointer or its count.
// A rejected property preserves its previous/default value; other settings apply.
JSON_SETTINGS_EWRAM static bool tryParsePatchAddresses(const JsonVariantConst& value,
    std::unique_ptr<u32[]>& addresses, u32& count, const char* key)
{
    if (!value.is<JsonArrayConst>())
    {
        gLogger->Log(LogLevel::Debug, "Invalid patch address array: %s\n", key);
        return false;
    }
    const auto array = value.as<JsonArrayConst>();
    for (JsonVariantConst element : array)
    {
        u32 parsed;
        const auto text = element.as<JsonString>();
        if (!element.is<const char*>() || !tryParseHexString(text.c_str(), text.size(), parsed))
        {
            gLogger->Log(LogLevel::Debug, "Invalid patch address in %s; array preserved\n", key);
            return false;
        }
    }
    auto parsedAddresses = std::make_unique<u32[]>(array.size());
    u32 i = 0;
    for (JsonVariantConst element : array)
    {
        const auto text = element.as<JsonString>();
        tryParseHexString(text.c_str(), text.size(), parsedAddresses[i++]);
    }
    addresses = std::move(parsedAddresses);
    count = i;
    return true;
}

JSON_SETTINGS_EWRAM static void readRunSettings(const JsonObjectConst& json, RunSettings& runSettings)
{
    if (json.isNull())
        return;
    if (json.containsKey(KEY_RUN_SETTINGS_JIT_PATCH_ADDRESSES))
        tryParsePatchAddresses(json[KEY_RUN_SETTINGS_JIT_PATCH_ADDRESSES],
            runSettings.jitPatchAddresses, runSettings.jitPatchAddressCount, KEY_RUN_SETTINGS_JIT_PATCH_ADDRESSES);
    readBoolSetting(json[KEY_RUN_SETTINGS_ENABLE_JIT], runSettings.enableJit);
    readBoolSetting(json[KEY_RUN_SETTINGS_ENABLE_ROM_ICACHE], runSettings.enableRomInstructionCache);
    readBoolSetting(json[KEY_RUN_SETTINGS_ENABLE_WRAM_ICACHE], runSettings.enableWramInstructionCache);
    readBoolSetting(json[KEY_RUN_SETTINGS_ENABLE_IWRAM_DCACHE], runSettings.enableIWramDataCache);
    readBoolSetting(json[KEY_RUN_SETTINGS_ENABLE_EWRAM_DCACHE], runSettings.enableEWramDataCache);
    readBoolSetting(json[KEY_RUN_SETTINGS_ARM9_CLOCK_SPEED], runSettings.forceDSModeArm9ClockSpeed);
    if (json.containsKey(KEY_RUN_SETTINGS_SELF_MODIFYING_PATCH_ADDRESSES))
        tryParsePatchAddresses(json[KEY_RUN_SETTINGS_SELF_MODIFYING_PATCH_ADDRESSES],
            runSettings.selfModifyingPatchAddresses, runSettings.selfModifyingPatchAddressCount,
            KEY_RUN_SETTINGS_SELF_MODIFYING_PATCH_ADDRESSES);
    readBoolSetting(json[KEY_RUN_SETTINGS_SKIP_BIOS_INTRO], runSettings.skipBiosIntro);
}

JSON_SETTINGS_EWRAM static void readGameSettings(const JsonObjectConst& json, GameSettings& gameSettings)
{
    if (json.isNull())
        return;

    tryParseGbaSaveType(json[KEY_GAME_SETTINGS_SAVE_TYPE], gameSettings.saveType);
}

JSON_SETTINGS_EWRAM static void readJson(const JsonDocument& json, AppSettings& appSettings)
{
    readDisplaySettings(json[KEY_DISPLAY_SETTINGS], appSettings.displaySettings);
    readRunSettings(json[KEY_RUN_SETTINGS], appSettings.runSettings);
    readGameSettings(json[KEY_GAME_SETTINGS], appSettings.gameSettings);
}

JSON_SETTINGS_EWRAM bool JsonAppSettingsSerializer::TryDeserialize(const TCHAR* filePath, AppSettings& appSettings)
{
    if (_settingsFile.Open(filePath, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return false;

    u32 fileSize = _settingsFile.GetSize();
    if (fileSize == 0)
        return false;

    std::unique_ptr<u8[]> fileData(new(cache_align) u8[fileSize]);

    u32 bytesRead = 0;
    if (_settingsFile.Read(fileData.get(), fileSize, bytesRead) != FR_OK ||
        bytesRead != fileSize ||
        _settingsFile.Close() != FR_OK)
    {
        return false;
    }

    auto result = deserializeJson(_jsonDocument, fileData.get(), fileSize);
    if (result != DeserializationError::Ok)
    {
        gLogger->Log(LogLevel::Debug, "Json deserialization error: %d\n", result);
        return false;
    }

    readJson(_jsonDocument, appSettings);

    return true;
}
