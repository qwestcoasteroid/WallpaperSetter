#include "Windows.h"

#include <vector>
#include <string>

#include "wallpaper_manager.h"
#include "configuration.h"

DWORD wallpaperSetTime{};
size_t wallpaperSetIndex = 0;

HANDLE hExitEvent{ NULL };

VOID WINAPI SetWallpaper(PVOID __param, BOOLEAN __timedOut) {
    auto &manager = *reinterpret_cast<WallpaperManager *>(__param);

    try {
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0,
            const_cast<WCHAR *>(manager.GetNextWallpaper().c_str()),
            SPIF_SENDCHANGE);
    }
    catch (std::runtime_error &error) {
        SetEvent(hExitEvent);
    }
}

int main() {
    const auto &configuration = Configuration::GetInstance();
    wallpaperSetTime = configuration->GetParameters().period;

    WallpaperManager manager(configuration->GetParameters().folder);

    HANDLE timerQueue = CreateTimerQueue();
    HANDLE wallpaperTimer = NULL;
    hExitEvent = CreateEventW(NULL, FALSE, FALSE, NULL);

    CreateTimerQueueTimer(&wallpaperTimer, timerQueue, SetWallpaper,
        &manager, wallpaperSetTime, wallpaperSetTime, WT_EXECUTEDEFAULT);

    WaitForSingleObject(hExitEvent, INFINITE);

    DeleteTimerQueueTimer(timerQueue, wallpaperTimer, NULL);
    DeleteTimerQueueEx(timerQueue, NULL);

    return 0;
}