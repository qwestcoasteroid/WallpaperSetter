#include "wallpaper_manager.h"

#include <regex>
#include <iostream>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <Windows.h>
#elif defined(__linux__)
#include <sys/inotify.h>
#include <limits.h>
#endif

struct WallpaperManager::Impl {
    std::mutex mutex;
    std::vector<std::wstring> wallpaperSet;
    std::vector<std::wstring>::iterator nextWallpaper;
    std::wstring directory;
};

void ModifyWallpaperSet(const void* buffer,
    std::vector<std::wstring>& set,
    std::vector<std::wstring>::iterator& next);

void MonitorDirectory(std::shared_ptr<WallpaperManager::Impl> pimpl);

WallpaperManager::WallpaperManager(const std::filesystem::path &path) :
    pimpl_(std::make_shared<Impl>()) {

    std::regex regularExpression(R"(.*jpg|.*jpeg|.*png|.*bmp)",
        std::regex_constants::extended);

    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_regular_file() &&
            std::regex_match(entry.path().string(), regularExpression)) {

            pimpl_->wallpaperSet.push_back(entry.path().filename().wstring());
        }
    }

    for (const auto &i : pimpl_->wallpaperSet) {
        std::wcout << i << std::endl;
    }

    pimpl_->nextWallpaper = pimpl_->wallpaperSet.begin();
    pimpl_->directory = path.wstring() + L"\\";


    std::thread monitorThread(MonitorDirectory, pimpl_);
    monitorThread.detach();
}

std::wstring WallpaperManager::GetNextWallpaper() {
    if (pimpl_->wallpaperSet.empty()) {
        throw std::runtime_error("Wallpaper Set is empty!");
    }

    std::wstring result;

    std::lock_guard<std::mutex> lock(pimpl_->mutex);

    if (pimpl_->nextWallpaper == pimpl_->wallpaperSet.end()) {
        pimpl_->nextWallpaper = pimpl_->wallpaperSet.begin();
    }

    result = *pimpl_->nextWallpaper++;

    return pimpl_->directory + result;
}

#ifdef _WIN32
void ModifyWallpaperSet(const void* buffer,
    std::vector<std::wstring>& set,
    std::vector<std::wstring>::iterator& next) {

    auto fileInformation =
        reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(buffer);

    DWORD offset{};
    WCHAR fileName[MAX_PATH]{};

    do {
        offset = fileInformation->NextEntryOffset;
        CopyMemory(fileName, fileInformation->FileName,
            fileInformation->FileNameLength);

        auto oldNext = *next;

        // Need to find next wallpaper properly!

        switch (fileInformation->Action) {
        case FILE_ACTION_REMOVED:
            set.erase(std::find(set.begin(), set.end(), fileName));
            break;
        case FILE_ACTION_ADDED:
            set.push_back(fileName);
            break;
        }

        next = std::find(set.begin(), set.end(), oldNext);

        fileInformation = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(
            reinterpret_cast<const BYTE *>(fileInformation) + offset);

        ZeroMemory(fileName, sizeof(fileName));

    } while (offset);
}
#elif defined(__linux__)
void ModifyWallpaperSet(const void* buffer,
    std::vector<std::wstring>& set,
    std::vector<std::wstring>::iterator& next) {

    auto event = reinterpret_cast<const inotify_event *>(buffer);

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::wstring fileName = converter.from_bytes(event->name);

    auto oldNext = *next;

    if (event->mask & IN_CREATE) {
        set.push_back(fileName);
    }
    else if (event->mask & IN_DELETE) {
        set.erase(std::find(set.begin(), set.end(), fileName));
    }

    next = std::find(set.begin(), set.end(), oldNext);
}
#else
static_assert(false, "Unknown target OS!");
#endif

#ifdef _WIN32
void MonitorDirectory(std::shared_ptr<WallpaperManager::Impl> pimpl) {
    static DWORD buffer[4096]{};
    DWORD bytes{};
    BOOL found{ TRUE };

    HANDLE hDirectory = CreateFileW(pimpl->directory.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);

    while (true) {
        ReadDirectoryChangesW(hDirectory, buffer, sizeof(buffer),
            FALSE, FILE_NOTIFY_CHANGE_FILE_NAME, &bytes, NULL, NULL);

        std::lock_guard<std::mutex> lock(pimpl->mutex);

        ModifyWallpaperSet(buffer, pimpl->wallpaperSet,
            pimpl->nextWallpaper);
    }

    CloseHandle(hDirectory);
}
#elif defined(__linux__)
void MonitorDirectory(std::shared_ptr<WallpaperManager::Impl> pimpl) {
    static char buffer[sizeof(inotify_event) + MAX_NAME + 1]{};

    int fileDescriptor = inotify_init();

    if (fileDescriptor == -1) {
        std::cerr << "Error initializing inotify!" << std::endl;
        return;
    }

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::string directoryPath = converter.to_bytes(pimpl->directory);

    int watchDescriptor = inotify_add_watch(fileDescriptor,
        directoryPath.c_str(), IN_CREATE | IN_DELETE);

    if (watchDescriptor == -1) {
        std::cerr << "Error adding watch!" << std::endl;
        close(fileDescriptor);
        return;
    }

    while (true) {
        int result = read(fileDescriptor, buffer, sizeof(buffer));

        if (result == -1) {
            std::cerr << "Error reading inotify event!" << std::endl;
            break;
        }

        std::lock_guard<std::mutex> lock(pimpl->mutex);

        ModifyWallpaperSet(buffer, pimpl->wallpaperSet, pimpl->nextWallpaper);
    }

    inotify_rm_watch(fileDescriptor, watchDescriptor);
    close(fileDescriptor);
}
#else
static_assert(false, "Unknown target OS!");
#endif