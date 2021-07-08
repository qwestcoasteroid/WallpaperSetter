#ifndef WALLPAPER_MANAGER_H_
#define WALLPAPER_MANAGER_H_

#include <string>
#include <filesystem>
#include <memory>

class WallpaperManager {
public:
    explicit WallpaperManager(const std::filesystem::path &__path);

    ~WallpaperManager() = default;

    std::wstring GetNextWallpaper();

private:
    struct Impl;

    friend void MonitorDirectory(std::shared_ptr<WallpaperManager::Impl>);

    std::shared_ptr<Impl> pimpl_;
};

#endif // WALLPAPER_MANAGER_H_