#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <Windows.h>

#include <iostream>
#include <sstream>
#include <thread>
#include <set>
#include <future>

#include "timer.h"

enum class Action : unsigned short { NONE, BREAK, CHANGE };

struct ActionStatus {
	constexpr ActionStatus() noexcept :
		next_action(Action::NONE), is_valid(false) {}

	Action next_action;
	bool is_valid;
};

using set_type = std::set<std::wstring, std::less<std::wstring>>;

WCHAR wallpaper_directory[] = L"D:\\Wallpapers";
WCHAR wallpaper_mask[] = L"*.*g";
WCHAR default_file_path[MAX_PATH] = L"C:\\Windows\\Web\\Wallpaper\\"
	"Windows\\img0.jpg";
WCHAR style[] = L"10";

std::wstring last_applied;

ActionStatus action_status;

bool set_changed = false;

std::mutex wallpaper_set_mutex;
std::mutex action_mutex;
std::mutex empty_set_mutex;

std::condition_variable action_condition;
std::condition_variable empty_set_condition;

//std::chrono::seconds sleep_duration(5);
//std::chrono::seconds wait_for_error(20);
std::chrono::milliseconds sleep_duration(5000);

void DiscardStream(std::basic_ostringstream<WCHAR>& __stream) {
	__stream.str(wallpaper_directory);
	__stream.seekp(sizeof(wallpaper_directory) / sizeof(WCHAR) - 1);
	__stream << L"\\";
}

bool ModifyWallpaperSet(set_type& __wallpaper_set,
	CONST PDWORD __buffer, DWORD __size) {

	//std::lock_guard<std::mutex> lock(wallpaper_set_mutex);

	std::basic_ostringstream<WCHAR> file_full_path;

	FILE_NOTIFY_INFORMATION* file_info =
		reinterpret_cast<PFILE_NOTIFY_INFORMATION>(__buffer);

	DWORD offset{};
	WCHAR file[MAX_PATH]{};

	do {
		offset = file_info->NextEntryOffset;

		memcpy(file, file_info->FileName, file_info->FileNameLength);

		DiscardStream(file_full_path);

		file_full_path << file;

		switch (file_info->Action) {
			case FILE_ACTION_REMOVED:
            {
                __wallpaper_set.erase(file_full_path.str());
				break;
            }
			case FILE_ACTION_ADDED:
            {
                __wallpaper_set.insert(file_full_path.str());
				break;
            }			
		}

		file_info = reinterpret_cast<PFILE_NOTIFY_INFORMATION>
			((reinterpret_cast<PBYTE>(file_info) + offset));

		memset(file, 0, sizeof(file));

	} while (offset);

	set_changed = true;

	return true;
}

bool CreateWallpaperSet(set_type& __wallpaper_set) {
	std::basic_ostringstream<WCHAR> file_full_path;

	DiscardStream(file_full_path);

	file_full_path << wallpaper_mask;

	WIN32_FIND_DATAW file_data{};

	HANDLE file{};

	file = FindFirstFileW(file_full_path.str().c_str(), &file_data);

	if (file == INVALID_HANDLE_VALUE) {
		std::wcerr << L"There are no wallpapers in '"
			<< wallpaper_directory << L"'" << std::endl;
		return false;
	}

	DiscardStream(file_full_path);
	file_full_path << file_data.cFileName;

	__wallpaper_set.insert(file_full_path.str());

	while (FindNextFileW(file, &file_data) != 0) {

		DiscardStream(file_full_path);
		file_full_path << file_data.cFileName;

		__wallpaper_set.insert(file_full_path.str());
	}

	FindClose(file);

	return true;
}

void WallpaperSetChanging(set_type& __wallpaper_set) {
	static DWORD buffer[4096]{};
	static DWORD bytes{};

	HANDLE handle = CreateFileW(wallpaper_directory, GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (handle == INVALID_HANDLE_VALUE) {
		DWORD result = GetLastError();
		return;
	}

	while (true) {
		BOOL result = ReadDirectoryChangesW(handle, &buffer, sizeof(buffer),
			FALSE, FILE_NOTIFY_CHANGE_FILE_NAME, &bytes, NULL, NULL);

		if (result == FALSE) {
			CloseHandle(handle);
			return;
		}

		std::lock_guard<std::mutex> lock(wallpaper_set_mutex);
		std::unique_lock<std::mutex> empty_set_lock(empty_set_mutex);

		ModifyWallpaperSet(__wallpaper_set, buffer, bytes);

		empty_set_condition.notify_one();
	}

	CloseHandle(handle);
}

/*set_type CopySet(const set_type& __wallpapers) {
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
			action_status.next_action = Action::BREAK;
		}
		else if (command == L"change") {
			action_status.next_action = Action::CHANGE;
		}
		else {
			action_status.next_action = Action::NONE;
		}

		action_status.is_valid = true;

		action_condition.notify_one();

		std::wcin.ignore(std::wcin.rdbuf()->in_avail());
	}
}

void ChangeWallpaperLoop(const set_type& __wallpapers, HWND __hWnd) {
	BOOL result = FALSE;
	HANDLE file{};

	Timer timer;

	set_type::const_iterator iter = __wallpapers.cbegin();

	do {
		std::unique_lock<std::mutex> wallpaper_lock(wallpaper_set_mutex);

		if (__wallpapers.empty()) {
			wallpaper_lock.unlock();

			std::unique_lock<std::mutex> empty_set_lock(empty_set_mutex);
			
			empty_set_condition.wait(empty_set_lock);

			wallpaper_lock.lock();

			iter = __wallpapers.cbegin();
		}

		file = CreateFileW((LPCWSTR)(*iter).c_str(), GENERIC_READ,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

		if (file == INVALID_HANDLE_VALUE) {
			break;
		}

		result = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0,
			(PVOID)(*iter).c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

		last_applied = *iter;

		CloseHandle(file);

		if (result == FALSE) {
			std::wcerr << L"Can't set the wallpaper!\n";
			return;
		}

		wallpaper_lock.unlock();

		std::unique_lock<std::mutex> action_lock(action_mutex);

		if (!action_status.is_valid) {
			/*std::chrono::system_clock::time_point start =
				std::chrono::system_clock::now();*/

			timer.Start();

			action_condition.wait_for(action_lock, sleep_duration);

			timer.Stop();

			/*std::chrono::system_clock::time_point end =
				std::chrono::system_clock::now();

			std::chrono::system_clock::duration elapsed_time(end - start);*/
		}

		if (action_status.is_valid) {
			switch (action_status.next_action) {
				case Action::NONE:
                {
                    /*std::this_thread::sleep_for(sleep_duration -
						std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_time));*/
					std::this_thread::sleep_for(sleep_duration -
						std::chrono::milliseconds(timer.ElapsedTime()));
					break;
                }
				case Action::BREAK:
                {
                    LRESULT result = SendMessageW(__hWnd, WM_DESTROY, 0, 0);
                    if (result != ERROR_SUCCESS) {
                        MessageBoxW(__hWnd, L"Unable to terminate program!",
                            L"Error", MB_ICONERROR);
                    }
					return;
                }
				case Action::CHANGE:
                {
                    break;
                }
			}
			action_status.is_valid = false;
		}

		wallpaper_lock.lock();

		if (set_changed) {
			iter = __wallpapers.find(last_applied);
			if (iter == __wallpapers.cend()) {
				iter = __wallpapers.lower_bound(last_applied);
			}
			else {
				iter++;
			}
			set_changed = false;
		}
		else {
			iter++;
		}

	} while (iter != __wallpapers.cend() ? true :
		(iter = __wallpapers.cbegin()) == iter);
}

/*bool CheckStartup(const char* __process_name) {
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
}*/

bool SetWallpaperStyle(LPCWSTR __style, DWORD __size) {
	HKEY key{};

	LSTATUS status = RegOpenKeyW(HKEY_CURRENT_USER, NULL, &key);

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
		(const BYTE*)__style, sizeof(__style));

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't get access to 'WallpaperStyle'"
			" registry key!\n";
		return false;
	}

	RegCloseKey(key);

	return true;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR szCmdLine, int nCmdShow) {
    set_type wallpapers;

    CreateWallpaperSet(wallpapers);

	bool is_set = SetWallpaperStyle(style, sizeof(style));

    MSG message{};
    HWND window_handle{};

    int window_width = 300;
    int window_height = 200;
    int window_x = GetSystemMetrics(SM_CXSCREEN) / 2 - window_width / 2;
    int window_y = GetSystemMetrics(SM_CYSCREEN) / 2 - window_height / 2;

    WNDCLASSEXW window_class{};
    
    window_class.cbSize = sizeof(WNDCLASSEXW);
    window_class.cbClsExtra = 0;
    window_class.cbWndExtra = 0;
    window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hInstance = hInstance;
    window_class.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        switch (uMsg) {
            case WM_CREATE:
            {
                HWND change_button = CreateWindowW(L"BUTTON",
                    L"Change Wallpaper", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    300 / 2 - 200 / 2, 20, 200, 50,
                    hWnd, reinterpret_cast<HMENU>(1), nullptr, nullptr);
                HWND exit_button = CreateWindowW(L"BUTTON",
                    L"Exit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    300 / 2 - 200 / 2, 90, 200, 50, hWnd, reinterpret_cast<HMENU>(2),
                    nullptr, nullptr);
                return 0;
            }
            case WM_COMMAND:
            {
                std::unique_lock<std::mutex> lock(action_mutex);
                switch (LOWORD(wParam)) {
                    case 1:
                    {
                        action_status.next_action = Action::CHANGE;
                        break;
                    }
                    case 2:
                    {
                        action_status.next_action = Action::BREAK;
                        break;
                    }
                    default:
                    {
                        action_status.next_action = Action::NONE;
                        break;
                    }
                }
                action_status.is_valid = true;
                action_condition.notify_one();
                return 0;
            }
            case WM_DESTROY:
            {
                PostQuitMessage(EXIT_SUCCESS);
                return 0;
            }
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    };
    window_class.lpszClassName = L"WallpaperSetter";
    window_class.lpszMenuName = nullptr;
    window_class.style = CS_HREDRAW | CS_VREDRAW;

    if (!RegisterClassExW(&window_class)) {
        return GetLastError();
    }

    window_handle = CreateWindowW(window_class.lpszClassName,
        L"Wallpaper Setter", 0, window_x, window_y,
        window_width, window_height, nullptr, nullptr, hInstance, nullptr);

    if (window_handle == INVALID_HANDLE_VALUE) {
        return EXIT_FAILURE;
    }

    ShowWindow(window_handle, nCmdShow);
    UpdateWindow(window_handle);

    if (!is_set) {
        MessageBoxW(window_handle, L"Can't change wallpaper style!",
            L"Info", MB_ICONINFORMATION);
	}

    std::thread wallpaper_loop(ChangeWallpaperLoop, std::cref(wallpapers), window_handle);
	//std::thread command_handler(CommandHandler);
	std::thread set_changing(WallpaperSetChanging, std::ref(wallpapers));

    wallpaper_loop.detach();
	//command_handler.detach();
	set_changing.detach();

    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}