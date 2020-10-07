#define _WIN32_WINNT 0x0600

#include <windows.h>

#include <iostream>
#include <sstream>
#include <set>
#include <chrono>

#include "timer.h"

enum class Action : unsigned short { NONE, BREAK, CHANGE };

struct ActionStatus {
	constexpr ActionStatus() noexcept :
		next_action(Action::NONE), is_valid(false) {}

	Action next_action;
	bool is_valid;
};

using set_type = std::set<std::wstring, std::less<std::wstring>>;

WCHAR wallpaper_directory[] = L"F:\\Wallpapers";
WCHAR wallpaper_mask[] = L"F:\\Wallpapers\\*.*g";
WCHAR default_file_path[MAX_PATH] = L"C:\\Windows\\Web\\Wallpaper\\"
	"Windows\\img0.jpg";
WCHAR style[] = L"10";

std::wstring last_applied;

ActionStatus action_status;

bool set_changed = false;

HANDLE wallpaper_set_mutex;
CRITICAL_SECTION action_section;
CRITICAL_SECTION empty_set_section;

CONDITION_VARIABLE action_condition;
CONDITION_VARIABLE empty_set_condition;

std::chrono::milliseconds sleep_duration(5000);

void DiscardStream(std::wostringstream& __stream) {
	__stream.str(wallpaper_directory);
	__stream.seekp(sizeof(wallpaper_directory) / sizeof(WCHAR) - 1);
	__stream << L"\\";
}

bool ModifyWallpaperSet(set_type& __wallpaper_set,
	CONST PDWORD __buffer, DWORD __size) {

	std::wostringstream file_full_path;

	FILE_NOTIFY_INFORMATION *file_info =
		reinterpret_cast<PFILE_NOTIFY_INFORMATION>(__buffer);

	DWORD offset = 0;
	WCHAR file[MAX_PATH] = {};

	DWORD wait_result = WaitForSingleObject(wallpaper_set_mutex, INFINITE);

	switch (wait_result) {
		case WAIT_ABANDONED:
			return GetLastError();
	}

	do {
		offset = file_info->NextEntryOffset;

		memcpy(file, file_info->FileName, file_info->FileNameLength);

		DiscardStream(file_full_path);

		file_full_path << file;

		switch (file_info->Action) {
			case FILE_ACTION_REMOVED:
				__wallpaper_set.erase(file_full_path.str());
				break;
			case FILE_ACTION_ADDED:
				__wallpaper_set.insert(file_full_path.str());
				break;
		}

		file_info = reinterpret_cast<PFILE_NOTIFY_INFORMATION>
			((reinterpret_cast<PBYTE>(file_info) + offset));

		memset(file, 0, sizeof(file));

	} while (offset);

	set_changed = true;

	ReleaseMutex(wallpaper_set_mutex);

	return true;
}

bool CreateWallpaperSet(set_type& __wallpaper_set) {
	WIN32_FIND_DATAW file_data = {};

	HANDLE file = NULL;

	file = FindFirstFileW(wallpaper_mask, &file_data);

	if (file == INVALID_HANDLE_VALUE) {
		std::wcerr << L"There are no wallpapers in '"
			<< wallpaper_directory << L"'" << std::endl;
		return false;
	}

	std::wostringstream str_stream;

	DiscardStream(str_stream);

	str_stream << file_data.cFileName;

	__wallpaper_set.insert(str_stream.str());

	while (FindNextFileW(file, &file_data) != FALSE) {
		DiscardStream(str_stream);
		str_stream << file_data.cFileName;
		__wallpaper_set.insert(str_stream.str());
	}

	FindClose(file);

	return true;
}

DWORD WINAPI WallpaperSetChanging(PVOID __wallpaper_set) {
	static DWORD buffer[4096] = {};
	DWORD bytes = 0;
	DWORD wait_result = 0;
	set_type *wallpapers = static_cast<set_type *>(__wallpaper_set);

	HANDLE handle = CreateFileW(wallpaper_directory, GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (handle == INVALID_HANDLE_VALUE) {
		return GetLastError();
	}

	while (true) {
		BOOL result = ReadDirectoryChangesW(handle, &buffer, sizeof(buffer),
			FALSE, FILE_NOTIFY_CHANGE_FILE_NAME, &bytes, NULL, NULL);

		if (result == FALSE) {
			CloseHandle(handle);
			return GetLastError();
		}

		wait_result = WaitForSingleObject(wallpaper_set_mutex, INFINITE);

		switch (wait_result) {
			case WAIT_ABANDONED:
				return GetLastError();
		}

		EnterCriticalSection(&empty_set_section);

		ModifyWallpaperSet(*wallpapers, buffer, bytes);

		LeaveCriticalSection(&empty_set_section);

		WakeConditionVariable(&empty_set_condition);
	}

	CloseHandle(handle);

	return 0;
}

DWORD WINAPI CommandHandler(PVOID __arg) {
	UNREFERENCED_PARAMETER(__arg);

	std::wstring command;

	bool stop = false;

	while (!stop) {
		std::wcin >> command;

		EnterCriticalSection(&action_section);

		if (command == L"exit") {
			action_status.next_action = Action::BREAK;
			stop = true;
		}
		else if (command == L"change") {
			action_status.next_action = Action::CHANGE;
		}
		else {
			action_status.next_action = Action::NONE;
		}

		action_status.is_valid = true;

		LeaveCriticalSection(&action_section);

		WakeConditionVariable(&action_condition);

		std::wcin.ignore(std::wcin.rdbuf()->in_avail());
	}

	return 0;
}

DWORD WINAPI ChangeWallpaperLoop(PVOID __wallpapers) {
	BOOL result = FALSE;
	HANDLE file = NULL;

	Timer timer;

	set_type *wallpapers = static_cast<set_type *>(__wallpapers);

	set_type::const_iterator iter = wallpapers->cbegin();
	
	do {
		DWORD wait_result = 0;

		wait_result = WaitForSingleObject(wallpaper_set_mutex, INFINITE);
		
		switch (wait_result) {
			case WAIT_ABANDONED:
				return GetLastError();
		}

		if (wallpapers->empty()) {
			ReleaseMutex(wallpaper_set_mutex); // Error handle

			EnterCriticalSection(&empty_set_section);

			SleepConditionVariableCS(&empty_set_condition, &empty_set_section, INFINITE);

			wait_result = WaitForSingleObject(wallpaper_set_mutex, INFINITE);

			switch (wait_result) {
				case WAIT_ABANDONED:
					return GetLastError();
			}

			iter = wallpapers->cbegin();

			LeaveCriticalSection(&empty_set_section);
		}

		if (iter == wallpapers->cend()) {
			ReleaseMutex(wallpaper_set_mutex);
			break;
		}
		
		file = CreateFileW(static_cast<LPCWSTR>(iter->c_str()), GENERIC_READ,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
		
		if (file == INVALID_HANDLE_VALUE) {
			break;
		}

		//std::wcout << iter->c_str() << std::endl;

		result = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0,
			static_cast<PVOID>(const_cast<LPWSTR>(iter->c_str())),
			/*SPIF_UPDATEINIFILE | */SPIF_SENDCHANGE);

		last_applied = *iter;

		ReleaseMutex(wallpaper_set_mutex);

		CloseHandle(file);

		if (result == FALSE) {
			std::wcerr << L"Can't set the wallpaper!\n";
			return GetLastError();
		}

		EnterCriticalSection(&action_section);

		if (!action_status.is_valid) {

			timer.Start();

			SleepConditionVariableCS(&action_condition, &action_section,
				sleep_duration.count());

			timer.Stop();
		}
		
		if (action_status.is_valid) {
			switch (action_status.next_action) {
				case Action::NONE:
					Sleep(sleep_duration.count() - timer.ElapsedTime());
					break;
				case Action::BREAK:
					result = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0,
						static_cast<PVOID>(const_cast<LPWSTR>(iter->c_str())),
						SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

					LeaveCriticalSection(&action_section);

					return 0;
				case Action::CHANGE:
					break;
			}
			action_status.is_valid = false;
		}

		LeaveCriticalSection(&action_section);

		wait_result = WaitForSingleObject(wallpaper_set_mutex, INFINITE);

		switch (wait_result) {
			case WAIT_ABANDONED:
				return GetLastError();
		}
		//wallpaper_lock.lock();

		if (set_changed) {
			iter = wallpapers->find(last_applied);
			if (iter == wallpapers->cend()) {
				iter = wallpapers->lower_bound(last_applied);
			}
			else {
				++iter;
			}
			set_changed = false;
		}
		else {
			++iter;
		}

		ReleaseMutex(wallpaper_set_mutex); // Need to move somewhere else...

	} while (iter != wallpapers->cend() ? true :
		(iter = wallpapers->cbegin()) == wallpapers->cbegin());

	return 0;
}

bool SetWallpaperStyle(LPCWSTR __style, DWORD __size) {
	HKEY key = NULL;

	LONG status = RegOpenKeyW(HKEY_CURRENT_USER, NULL, &key);

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't open the 'HKEY_CURRENT_USER'"
			" registry folder!\n";
		return false;
	}

	status = RegOpenKeyW(key, L"Control Panel\\Desktop", &key);

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't open the 'Control Panel\\Desktop'"
			" registry subfolder!";
		return false;
	}

	status = RegSetValueExW(key, L"WallpaperStyle", 0, REG_SZ,
		reinterpret_cast<const BYTE *>(__style), sizeof(__style));

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't get access to 'WallpaperStyle'"
			" registry key!\n";
		return false;
	}

	RegCloseKey(key);

	return true;
}

int main(int argc, char *argv[]) {
	wallpaper_set_mutex = CreateMutexW(NULL, FALSE, NULL);

	InitializeConditionVariable(&action_condition);
	InitializeConditionVariable(&empty_set_condition);

	InitializeCriticalSection(&empty_set_section);
	InitializeCriticalSection(&action_section);

	set_type wallpapers;

	CreateWallpaperSet(wallpapers);

	bool is_set = SetWallpaperStyle(style, sizeof(style));

	if (!is_set) {
		std::wcerr << L"Can't change wallpaper style!\n";
	}

	HANDLE threads[3] = {};

	threads[0] = CreateThread(NULL, 0, ChangeWallpaperLoop,
		static_cast<PVOID>(&wallpapers), 0, NULL);

	threads[1] = CreateThread(NULL, 0, CommandHandler, NULL, 0, NULL);
	threads[2] = CreateThread(NULL, 0, WallpaperSetChanging,
		static_cast<PVOID>(&wallpapers), 0, NULL);

	WaitForMultipleObjects(sizeof(threads) / sizeof(HANDLE),
		threads, TRUE, INFINITE); // WallpaperSetChanging doesn't terminate!

	for (size_t i = 0; i < sizeof(threads) / sizeof(HANDLE); ++i) {
		CloseHandle(threads[i]);
	}

	CloseHandle(wallpaper_set_mutex);
	DeleteCriticalSection(&action_section);
	DeleteCriticalSection(&empty_set_section);

	return 0;
}