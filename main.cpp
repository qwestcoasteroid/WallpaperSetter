#ifdef _WIN32
#include "Windows.h"
#elif defined(__linux__)

#endif

#include <string>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "wallpaper_manager.h"
#include "configuration.h"

void SetWallpaper(const std::wstring &wallpaper);
void StartWallpaperSetter(WallpaperManager &manager);

int main() {
    const auto &configuration = Configuration::GetInstance();

    WallpaperManager manager(configuration->GetParameters().folder);

    StartWallpaperSetter(manager);

    return 0;
}

void StartWallpaperSetter(WallpaperManager& manager) {
    auto period = Configuration::GetInstance()->GetParameters().period;
    std::chrono::milliseconds waitTime(period);

    while (true) {
        try {
            SetWallpaper(manager.GetNextWallpaper());
        }
        catch (std::runtime_error &) {
            return;
        }

        std::this_thread::sleep_for(waitTime);
    }
}

#ifdef _WIN32
void SetWallpaper(const std::wstring& wallpaper) {
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0,
        const_cast<WCHAR *>(wallpaper.c_str()),
        SPIF_SENDCHANGE);
}
#elif defined(__linux__)

#else
static_assert(false, "Unknown target OS!");
#endif