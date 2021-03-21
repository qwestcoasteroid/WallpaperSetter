#ifndef WALLPAPER_MANAGER_H_
#define WALLPAPER_MANAGER_H_

#include "Windows.h"

#include <vector>
#include <string>
#include <filesystem>


class WallpaperManager {
public:
    explicit WallpaperManager(const std::filesystem::path &__path);

    ~WallpaperManager();

    std::wstring GetNextWallpaper();

    friend DWORD WINAPI MonitorDirectory(PVOID);

private:
    HANDLE hMutex{ NULL };
    std::vector<std::wstring> wallpaperSet;
    std::vector<std::wstring>::iterator nextWallpaper;
    std::wstring directory;
};

#endif // WALLPAPER_MANAGER_H_