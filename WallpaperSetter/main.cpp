#include <Windows.h>

#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <future>

WCHAR dir[] = L"C:\\Wallpapers";
WCHAR default_file_path[MAX_PATH] = L"C:\\Windows\\Web\\Wallpaper\\"
									"Windows\\img0.jpg";
WCHAR style[] = L"10";

std::chrono::minutes sleep_duration(1);
//std::chrono::seconds wait_for_error(20);

enum class Action : unsigned short { BREAK, CHANGE };

void ChangeWallpaperLoop(const std::vector<std::wstring>& __wallpapers,
  std::future<Action>& __action) {
	BOOL result = FALSE;

	while (true) {
		for (const auto& i : __wallpapers) {
			result = SystemParametersInfo(SPI_SETDESKWALLPAPER, 0,
				 (PVOID)i.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

			if (result == FALSE) {
				std::wcerr << L"Can't set the wallpaper!\n";
				return;
			}

			if (__action.wait_for(sleep_duration) ==
			  std::future_status::timeout) {
				continue;
			}
			else {
				switch (__action.get()) {
				  case Action::BREAK:
					return;
				  case Action::CHANGE:
					continue;
				  default:
					break;
				}
			}
		}
	}
}

void CommandHandler(std::promise<Action>& __action_promise) {
	std::wstring command;

	while (true) {
		std::wcin >> command;

		if (command == L"exit") {
			__action_promise.set_value(Action::BREAK);
			break;
		}
		else if (command == L"change") {
			__action_promise.set_value(Action::CHANGE);
		}
		else {
			std::wcerr << L"INVALID COMMAND!!!\n";
		}
		std::wcin.ignore(std::wcin.rdbuf()->in_avail());
	}
}

int main(int argc, char* argv[]) {

	ShowWindow(GetConsoleWindow(), SW_HIDE);

	std::set_terminate([]() {
			SystemParametersInfo(SPI_SETDESKWALLPAPER, 0,
				default_file_path, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
			std::abort();
		});

	std::basic_ostringstream<WCHAR> file_full_path;

	file_full_path << dir << L"\\*.*g";

	WIN32_FIND_DATA file_data;

	HANDLE file;

	file = FindFirstFile(file_full_path.str().c_str(), &file_data);

	if (file == INVALID_HANDLE_VALUE) {
		std::wcerr << L"There are no wallpapers in '"
			<< dir << L"'" << std::endl;
		return 1;
	}

	file_full_path.str(dir);

	file_full_path.seekp(sizeof(dir) / sizeof(WCHAR) - 1);

	file_full_path << L"\\" << file_data.cFileName;

	std::vector<std::wstring> wallpapers;

	wallpapers.push_back(file_full_path.str());

	HKEY key;

	LSTATUS status = RegOpenKey(HKEY_CURRENT_USER, NULL, &key);

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't open the 'HKEY_CURRENT_USER'"
			" registry folder!\n";
		return 2;
	}

	status = RegOpenKey(key, L"Control Panel\\Desktop", &key);

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't open the 'Control Panel\\Desktop'"
			" registry subfolder!";
		return 3;
	}

	status = RegSetValueEx(key, L"WallpaperStyle", 0, REG_SZ,
		(const BYTE*)style, sizeof(style));

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't get access to 'WallpaperStyle'"
			" registry key!\n";
		return 4;
	}

	while (FindNextFile(file, &file_data) != 0) {

		file_full_path.str(dir);

		file_full_path.seekp(sizeof(dir) / sizeof(WCHAR) - 1);

		file_full_path << L"\\" << file_data.cFileName;

		wallpapers.push_back(file_full_path.str());
	}

	FindClose(file);

	RegCloseKey(key);

	std::promise<Action> action_promise;

	std::future<Action> action_future = action_promise.get_future();

	std::future<void> wallpaper_error = std::async(std::launch::async,
		ChangeWallpaperLoop, std::cref(wallpapers), std::ref(action_future));

	std::thread command_handler(CommandHandler, std::ref(action_promise));

	command_handler.detach();

	/*std::thread wallpaper_loop(ChangeWallpaperLoop,
		std::cref(wallpapers), std::ref(action_future));*/

	wallpaper_error.wait();

	//wallpaper_loop.join();

	return 0;
}