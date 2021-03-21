#include "wallpaper_manager.h"

#include <regex>
#include <iostream>

VOID WINAPI ModifyWallpaperSet(CONST PDWORD __buffer,
    std::vector<std::wstring> &__set,
    std::vector<std::wstring>::iterator &__next) {

    auto fileInformation =
        reinterpret_cast<CONST PFILE_NOTIFY_INFORMATION>(__buffer);

    DWORD offset{};
    WCHAR fileName[MAX_PATH]{};

    do {
        offset = fileInformation->NextEntryOffset;
        CopyMemory(fileName, fileInformation->FileName,
            fileInformation->FileNameLength);

        auto oldNext = *__next;

        // Need to find next wallpaper properly!

        switch (fileInformation->Action) {
        case FILE_ACTION_REMOVED:
            __set.erase(std::find(__set.begin(), __set.end(), fileName));
            break;
        case FILE_ACTION_ADDED:
            __set.push_back(fileName);
            break;
        }

        __next = std::find(__set.begin(), __set.end(), oldNext);

        fileInformation = reinterpret_cast<CONST PFILE_NOTIFY_INFORMATION>(
            reinterpret_cast<CONST PBYTE>(fileInformation) + offset);

        ZeroMemory(fileName, sizeof(fileName));

    } while (offset);
}

DWORD WINAPI MonitorDirectory(PVOID __manager) {
    auto &manager = *reinterpret_cast<WallpaperManager *>(__manager);

    static DWORD buffer[4096]{};
    DWORD bytes{};
    BOOL found{ TRUE };

    HANDLE hDirectory = CreateFileW(manager.directory.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS, NULL);

    while (true) {
        ReadDirectoryChangesW(hDirectory, buffer, sizeof(buffer),
            FALSE, FILE_NOTIFY_CHANGE_FILE_NAME, &bytes, NULL, NULL);
        
        WaitForSingleObject(manager.hMutex, INFINITE);

        ModifyWallpaperSet(buffer, manager.wallpaperSet,
            manager.nextWallpaper);

        ReleaseMutex(manager.hMutex);
    }

    CloseHandle(hDirectory);

    return EXIT_SUCCESS;
}

WallpaperManager::WallpaperManager(const std::filesystem::path &__path) {
    std::regex regularExpression(R"(.*jpg|.*jpeg|.*png|.*bmp)",
        std::regex_constants::extended);

    for (const auto &entry : std::filesystem::directory_iterator(__path)) {
        if (entry.is_regular_file() &&
            std::regex_match(entry.path().string(), regularExpression)) {

            wallpaperSet.push_back(entry.path().filename().wstring());
        }
    }

    for (const auto &i : wallpaperSet) {
        std::wcout << i << std::endl;
    }

    hMutex = CreateMutexA(NULL, FALSE, NULL);
    nextWallpaper = wallpaperSet.begin();
    directory = __path.wstring() + L"\\";

    QueueUserWorkItem(MonitorDirectory, this, WT_EXECUTEDEFAULT);
}

std::wstring WallpaperManager::GetNextWallpaper() {
    if (wallpaperSet.empty()) {
        throw std::runtime_error("Wallpaper Set is empty!");
    }

    std::wstring result;

    WaitForSingleObject(hMutex, INFINITE);

    if (nextWallpaper == wallpaperSet.end()) {
        nextWallpaper = wallpaperSet.begin();
    }

    result = *nextWallpaper++;

    ReleaseMutex(hMutex);

    return directory + result;
}

WallpaperManager::~WallpaperManager() {
    CloseHandle(hMutex);
}