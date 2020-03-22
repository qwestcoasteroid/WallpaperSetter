#include <Windows.h>

#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <future>

#include "timer.h"

enum class Action : unsigned short { NONE, BREAK, CHANGE };

WCHAR wallpaper_directory[] = L"D:\\Wallpapers";
WCHAR wallpaper_mask[] = L"\\*.*g";
WCHAR default_file_path[MAX_PATH] = L"C:\\Windows\\Web\\Wallpaper\\"
	"Windows\\img0.jpg";
WCHAR style[] = L"10";

Action next_action = Action::NONE;

bool set_changed = false;

std::mutex wallpaper_set_mutex;
std::mutex action_mutex;

std::condition_variable action_condition;

//std::chrono::seconds sleep_duration(5);
//std::chrono::seconds wait_for_error(20);
std::chrono::milliseconds sleep_duration(5000);

void DiscardStream(std::basic_ostringstream<WCHAR>& __stream) {
	__stream.str(wallpaper_directory);
	__stream.seekp(sizeof(wallpaper_directory) / sizeof(WCHAR) - 1);
	__stream << L"\\";
}

bool CreateWallpaperSet(std::vector<std::wstring>& __wallpaper_set) {
	std::lock_guard<std::mutex> lock(wallpaper_set_mutex);

	if (!__wallpaper_set.empty()) {
		__wallpaper_set.clear();
	}

	std::basic_ostringstream<WCHAR> file_full_path;

	file_full_path << wallpaper_directory << wallpaper_mask;

	WIN32_FIND_DATA file_data;

	HANDLE file;

	file = FindFirstFile(file_full_path.str().c_str(), &file_data);

	if (file == INVALID_HANDLE_VALUE) {
		std::wcerr << L"There are no wallpapers in '"
			<< wallpaper_directory << L"'" << std::endl;
		return false;
	}

	DiscardStream(file_full_path);
	file_full_path << file_data.cFileName;

	__wallpaper_set.push_back(file_full_path.str());

	while (FindNextFile(file, &file_data) != 0) {

		DiscardStream(file_full_path);
		file_full_path << file_data.cFileName;

		__wallpaper_set.push_back(file_full_path.str());
	}

	FindClose(file);

	set_changed = true;

	return true;
}

void WallpaperSetChanging(std::vector<std::wstring>& __wallpaper_set) {
	HANDLE change_handle =
		FindFirstChangeNotification(wallpaper_directory,
		FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);

	bool created = true;
	BOOL found = TRUE;
	DWORD wait_status;

	if (change_handle == INVALID_HANDLE_VALUE) {
		FindCloseChangeNotification(change_handle);
		return;
	}
	while (found == TRUE) {
		wait_status = WaitForSingleObject(change_handle, INFINITE);

		if (wait_status == WAIT_OBJECT_0) {
			created = CreateWallpaperSet(__wallpaper_set);

			if (!created) {
				std::wcerr << L"Can't refresh wallpaper set!\n";
				return;
			}

			found = FindNextChangeNotification(change_handle);

			if (found == FALSE) {
				std::wcerr << L"Can't refresh wallpaper set!\n";
				FindCloseChangeNotification(change_handle);
				return;
			}
		}
	}
}

/*std::vector<std::wstring> CopySet(const std::vector<std::wstring>& __wallpapers) {
	std::lock_guard<std::mutex> lock(wallpaper_set_mutex);

	set_changed = false;

	return __wallpapers;
}*/

void CommandHandler() {
	std::wstring command;

	while (true) {
		std::wcin >> command;

		std::unique_lock<std::mutex> lock(action_mutex);

		if (command == L"exit") {
			next_action = Action::BREAK;
		}
		else if (command == L"change") {
			next_action = Action::CHANGE;
		}
		else {
			next_action = Action::NONE;
		}

		action_condition.notify_one();
	}
}

void ChangeWallpaperLoop(const std::vector<std::wstring>& __wallpapers) {

	BOOL result = FALSE;
	HANDLE file = NULL;

	Timer timer;

	while (true) {
		timer.Reset();

		for (const auto& i : __wallpapers) {
			std::unique_lock<std::mutex> lock(wallpaper_set_mutex);
			
			if (set_changed) {
				set_changed = false;
				break;
			}
			
			file = CreateFile((LPCWSTR)i.c_str(), 0, 0, NULL,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			
			if (file == INVALID_HANDLE_VALUE) {
				break;
			}
			result = SystemParametersInfo(SPI_SETDESKWALLPAPER, 0,
				 (PVOID)i.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

			CloseHandle(file);

			lock.unlock();

			if (result == FALSE) {
				std::wcerr << L"Can't set the wallpaper!\n";
				return;
			}

			std::unique_lock<std::mutex> action_lock(action_mutex);

			/*std::chrono::system_clock::time_point start =
				std::chrono::system_clock::now();*/

			timer.Start();

			action_condition.wait_for(action_lock, sleep_duration);

			timer.Stop();

			/*std::chrono::system_clock::time_point end =
				std::chrono::system_clock::now();

			std::chrono::system_clock::duration elapsed_time(end - start);*/

			switch (next_action) {
				case Action::NONE:
					/*std::this_thread::sleep_for(sleep_duration -
						std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_time));*/
					std::this_thread::sleep_for(sleep_duration - timer.ElapsedTime());
					break;
				case Action::BREAK:
					return;
				case Action::CHANGE:
					continue;
			}
		}
	}
}

bool CheckStartup(const char* __process_name) {
	HKEY key;
	LSTATUS status = RegOpenKey(HKEY_LOCAL_MACHINE,
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", &key);

	if (status == ERROR_SUCCESS) {
		status = RegGetValue(key, NULL, L"WallpaperSetter", 0, NULL, NULL, NULL);
		if (status == ERROR_SUCCESS) {
			RegCloseKey(key);
			return true;
		}
		else {
			//status = RegCreateKey(HKEY_LOCAL_MACHINE,
				//L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", &key);
			CHAR path[MAX_PATH] = {};
			DWORD result = GetCurrentDirectoryA(MAX_PATH, path);
			strcat_s(path, "\\");
			strcat_s(path, __process_name);
			status = RegSetValueExA(key, "WallpaperSetter",
				0, REG_SZ, (const BYTE*)path, sizeof(path)); // Access denied
			RegCloseKey(key);
		}
	}
	return true;
}

bool SetWallpaperStyle(LPCWSTR __style, DWORD __size) {
	HKEY key = NULL;

	LSTATUS status = RegOpenKey(HKEY_CURRENT_USER, NULL, &key);

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't open the 'HKEY_CURRENT_USER'"
			" registry folder!\n";
		return false;
	}

	status = RegOpenKey(key, L"Control Panel\\Desktop", &key);

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't open the 'Control Panel\\Desktop'"
			" registry subfolder!";
		return false;
	}

	status = RegSetValueEx(key, L"WallpaperStyle", 0, REG_SZ,
		(const BYTE*)__style, sizeof(__style));

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't get access to 'WallpaperStyle'"
			" registry key!\n";
		return false;
	}

	RegCloseKey(key);

	return true;
}

int main(int argc, char* argv[]) {
	//ShowWindow(GetConsoleWindow(), SW_HIDE);

	//CheckStartup(argv[0]);

	std::set_terminate([]() {
			SystemParametersInfo(SPI_SETDESKWALLPAPER, 0,
				default_file_path, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
			std::abort();
		});

	std::vector<std::wstring> wallpapers;

	CreateWallpaperSet(wallpapers);

	bool is_set = SetWallpaperStyle(style, sizeof(style));

	if (!is_set) {
		std::wcerr << L"Can't change wallpaper style!\n";
	}

	//std::promise<Action> action_promise;

	//std::future<Action> action_future = action_promise.get_future();

	std::future<void> wallpaper_error = std::async(std::launch::async,
		ChangeWallpaperLoop, std::cref(wallpapers));

	std::thread command_handler(CommandHandler);
	std::thread set_changing(WallpaperSetChanging, std::ref(wallpapers));

	command_handler.detach();
	set_changing.detach();

	/*std::thread wallpaper_loop(ChangeWallpaperLoop,
		std::cref(wallpapers), std::ref(action_future));*/

	wallpaper_error.wait();

	//wallpaper_loop.join();

	return 0;
}