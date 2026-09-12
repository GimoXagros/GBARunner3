#ifdef GBAR3_DISPLAY_FLICKER_DIAGNOSTICS
#include "common.h"
#include "Application/Settings/SettingsSnapshot.h"
#include "Fat/ff.h"
#include "mini-printf.h"

// Boot-only: this function is called after the title config has been processed,
// before the VM starts. No hook is installed in IRQ, DMA, display or save code.
[[gnu::section(".ewram")]]
void displayDiagnosticsWriteSettings(const AppSettings& settings, u32 gameCode,
    unsigned revision, bool globalLoaded, const char* titlePath, bool titleLoaded)
{
    [[gnu::section(".ewram.bss")]] static char report[2048];
    [[gnu::section(".ewram.bss")]] static char escapedPath[512];
    [[gnu::section(".ewram.bss")]] static FIL file;
    if (!settingsEscapePath(escapedPath, sizeof(escapedPath), titlePath))
        return;
    const int length = formatSettingsSnapshot(report, sizeof(report), settings,
        gameCode, revision, globalLoaded, escapedPath, titleLoaded, mini_snprintf);
    if (length < 0)
        return;
    // CREATE_NEW never truncates an earlier result. No writes to settings/save.
    char path[32];
    for (unsigned index = 0; index < 1000; ++index)
    {
        mini_snprintf(path, sizeof(path), "/_gba/diag%03u.txt", index);
        FRESULT result = f_open(&file, path, FA_CREATE_NEW | FA_WRITE);
        if (result == FR_EXIST)
            continue;
        if (result != FR_OK)
            return;
        UINT written = 0;
        result = f_write(&file, report, unsigned(length), &written);
        // An explicit end marker identifies a complete report. A failed or
        // short write is retained for diagnosis and never reported as success.
        if (result == FR_OK && written == unsigned(length))
        {
            static const char end[] = "END SETTINGS SNAPSHOT\n";
            f_write(&file, end, sizeof(end) - 1, &written);
        }
        f_close(&file);
        return;
    }
}
#endif
