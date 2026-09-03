#include "common.h"
#include "AppSettingsService.h"
#ifdef GBAR3_DIAG_AUTOCAPTURE
#include "Diagnostics/AutoCaptureDiagnostics.h"
#endif

[[gnu::section(".ewram.bss")]]
static JsonAppSettingsSerializer sJsonAppSettingsSerializer;

AppSettingsService gAppSettingsService(&sJsonAppSettingsSerializer);

bool AppSettingsService::TryLoadAppSettings(const TCHAR* settingsFilePath)
{
    const bool loaded = _appSettingsSerializer->TryDeserialize(settingsFilePath, _appSettings);
#ifdef GBAR3_DIAG_AUTOCAPTURE
    diag_recordConfig(settingsFilePath, loaded);
#endif
    return loaded;
}
