#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define _WIN32_WINNT 0x0600

#include <Windows.h>

#include <iostream>
#include <sstream>
#include <set>
#include <vector>
#include <chrono>

#include "timer.h"

#pragma warning(disable:4996)

enum class Action : unsigned short { NONE, BREAK, CHANGE };

enum class WindowElement : unsigned short { TEXT_BOX, START_STOP, CHANGE, EXIT };

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

/*std::mutex wallpaper_set_mutex;
std::mutex action_mutex;
std::mutex empty_set_mutex;

std::condition_variable action_condition;
std::condition_variable empty_set_condition;*/

HANDLE wallpaper_set_mutex;
CRITICAL_SECTION action_section;
CRITICAL_SECTION empty_set_section;

CONDITION_VARIABLE action_condition;
CONDITION_VARIABLE empty_set_condition;

//std::chrono::seconds sleep_duration(5);
//std::chrono::seconds wait_for_error(20);
std::chrono::milliseconds sleep_duration(5000);

HWND main_window;

std::vector<HWND> window_elements;

WCHAR user_defined_directory[MAX_PATH] = {};

bool text_box_blocked = false;

void CreateWindowElements(HWND hWnd) {
	window_elements.push_back(CreateWindowW(L"EDIT", L"Wallpapers Directory",
		WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_CENTER | WS_BORDER,
		300 / 2 - 200 / 2, 20, 200, 20, hWnd, reinterpret_cast<HMENU>(3),
		nullptr, nullptr));
	window_elements.push_back(CreateWindowW(L"BUTTON", L"Start/Stop",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		300 / 2 - 150 / 2, 90, 150, 50, hWnd, reinterpret_cast<HMENU>(4),
		nullptr, nullptr));
	window_elements.push_back(CreateWindowW(L"BUTTON",
		L"Change Wallpaper", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		300 / 2 - 200 / 2, 160, 200, 50,
		hWnd, reinterpret_cast<HMENU>(1), nullptr, nullptr));
	window_elements.push_back(CreateWindowW(L"BUTTON",
		L"Exit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		300 / 2 - 200 / 2, 230, 200, 50, hWnd, reinterpret_cast<HMENU>(2),
		nullptr, nullptr));
}

void DiscardStream(std::wostringstream& __stream) {
	__stream.str(wallpaper_directory);
	__stream.seekp(sizeof(wallpaper_directory) / sizeof(WCHAR) - 1);
	__stream << L"\\";
}

bool ModifyWallpaperSet(set_type& __wallpaper_set,
	CONST PDWORD __buffer, DWORD __size) {

	//std::lock_guard<std::mutex> lock(wallpaper_set_mutex);

	std::wostringstream file_full_path;
	
	FILE_NOTIFY_INFORMATION* file_info =
		reinterpret_cast<PFILE_NOTIFY_INFORMATION>(__buffer);

	DWORD offset;
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

	while (FindNextFileW(file, &file_data) != 0) {
		DiscardStream(str_stream);
		str_stream << file_data.cFileName;
		__wallpaper_set.insert(str_stream.str());
	}

	FindClose(file);

	return true;
}

DWORD WINAPI WallpaperSetChanging(PVOID __wallpaper_set) {
	static DWORD buffer[4096] = {};
	static DWORD bytes;
	DWORD wait_result;
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

		//std::lock_guard<std::mutex> lock(wallpaper_set_mutex);
		EnterCriticalSection(&empty_set_section);
		//std::unique_lock<std::mutex> empty_set_lock(empty_set_mutex);

		ModifyWallpaperSet(*wallpapers, buffer, bytes);

		LeaveCriticalSection(&empty_set_section);

		WakeConditionVariable(&empty_set_condition);
		//empty_set_condition.notify_one();
	}

	CloseHandle(handle);

	return 0;
}

/*set_type CopySet(const set_type& __wallpapers) {
	std::lock_guard<std::mutex> lock(wallpaper_set_mutex);

	set_changed = false;

	return __wallpapers;
}*/

DWORD WINAPI CommandHandler(PVOID __arg) {
	UNREFERENCED_PARAMETER(__arg);

	std::wstring command;

	bool stop = false;

	while (!stop) {
		std::wcin >> command;

		EnterCriticalSection(&action_section);
		//std::unique_lock<std::mutex> lock(action_mutex);

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
		//action_condition.notify_one();

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
		DWORD wait_result;

		wait_result = WaitForSingleObject(wallpaper_set_mutex, INFINITE);
		//std::unique_lock<std::mutex> wallpaper_lock(wallpaper_set_mutex);

		switch (wait_result) {
			case WAIT_ABANDONED:
				return GetLastError();
		}

		if (wallpapers->empty()) { // Blocks exit button
			ReleaseMutex(wallpaper_set_mutex); // Error handle
			//wallpaper_lock.unlock();

			EnterCriticalSection(&empty_set_section);
			//std::unique_lock<std::mutex> empty_set_lock(empty_set_mutex);
			
			SleepConditionVariableCS(&empty_set_condition, &empty_set_section, INFINITE);
			//empty_set_condition.wait(empty_set_lock);

			wait_result = WaitForSingleObject(wallpaper_set_mutex, INFINITE);
			//wallpaper_lock.lock();

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

		file = CreateFileW(reinterpret_cast<LPCWSTR>(iter->c_str()), GENERIC_READ,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

		if (file == INVALID_HANDLE_VALUE) {
			break;
		}

		//timer.Start();

		result = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0,
			reinterpret_cast<PVOID>(const_cast<LPWSTR>(iter->c_str())),
			/*SPIF_UPDATEINIFILE | */SPIF_SENDCHANGE);

		//timer.Stop();

		//char buf[10]{};

		//MessageBoxA(main_window, itoa(timer.ElapsedTime(), buf, 10), "Time", MB_ICONINFORMATION);

		last_applied = *iter;

		CloseHandle(file);

		if (result == FALSE) {
			std::wcerr << L"Can't set the wallpaper!\n";
			return GetLastError();
		}

		ReleaseMutex(wallpaper_set_mutex);
		//wallpaper_lock.unlock();
		
		EnterCriticalSection(&action_section);
		//std::unique_lock<std::mutex> action_lock(action_mutex);

		if (!action_status.is_valid) {
			/*std::chrono::system_clock::time_point start =
				std::chrono::system_clock::now();*/

			timer.Start();
			
			SleepConditionVariableCS(&action_condition, &action_section,
				sleep_duration.count());
			//action_condition.wait_for(action_lock, sleep_duration);

			timer.Stop();

			/*std::chrono::system_clock::time_point end =
				std::chrono::system_clock::now();

			std::chrono::system_clock::duration elapsed_time(end - start);*/
		}

		if (action_status.is_valid) {
			LRESULT lresult = 0;

			switch (action_status.next_action) {
				case Action::NONE:
                    /*std::this_thread::sleep_for(sleep_duration -
						std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_time));*/
					/*std::this_thread::sleep_for(sleep_duration -
						std::chrono::milliseconds(timer.ElapsedTime()));*/
					Sleep(sleep_duration.count() - timer.ElapsedTime());
					break;
				case Action::BREAK:
					result = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0,
						reinterpret_cast<PVOID>(const_cast<LPWSTR>(iter->c_str())),
						SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

                    lresult = SendMessageW(main_window, WM_DESTROY, 0, 0);

                    if (lresult != ERROR_SUCCESS) {
                        MessageBoxW(main_window, L"Unable to terminate program!",
                            L"Error", MB_ICONERROR);
                    }

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

		ReleaseMutex(wallpaper_set_mutex);

	} while (iter != wallpapers->cend() ? true :
		(iter = wallpapers->cbegin()) == wallpapers->cbegin());

	return 0;
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
		reinterpret_cast<CONST BYTE*>(__style), sizeof(__style));

	if (status != ERROR_SUCCESS) {
		std::wcerr << L"Can't get access to 'WallpaperStyle'"
			" registry key!\n";
		return false;
	}

	RegCloseKey(key);

	return true;
}

LRESULT window_procedure(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_CREATE:
			CreateWindowElements(hWnd);
			return 0;
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case 1:
					EnterCriticalSection(&action_section);
					//std::unique_lock<std::mutex> lock(action_mutex);
					action_status.next_action = Action::CHANGE;
					action_status.is_valid = true;
					LeaveCriticalSection(&action_section);
					break;
				case 2:
					ShowWindow(main_window, SW_HIDE);
					EnterCriticalSection(&action_section);
					//std::unique_lock<std::mutex> lock(action_mutex);
					action_status.next_action = Action::BREAK;
					action_status.is_valid = true;
					LeaveCriticalSection(&action_section);
					WakeConditionVariable(&empty_set_condition);
					//empty_set_condition.notify_one();
					break;
				case 3:
					return 0;
				case 4:
					if (!text_box_blocked) {
						GetWindowTextW(window_elements[static_cast<size_t>(WindowElement::TEXT_BOX)],
							user_defined_directory, MAX_PATH);
						EnableWindow(window_elements[static_cast<size_t>(WindowElement::TEXT_BOX)], FALSE);
						MessageBoxW(hWnd, user_defined_directory, L"New Directory", MB_ICONINFORMATION);
						text_box_blocked = true;
					}
					else {
						EnableWindow(window_elements[static_cast<size_t>(WindowElement::TEXT_BOX)], TRUE);
						memset(user_defined_directory, 0, sizeof(user_defined_directory));
						text_box_blocked = false;
					}
					break;
				default:
					EnterCriticalSection(&action_section);
					//std::unique_lock<std::mutex> lock(action_mutex);
					action_status.next_action = Action::NONE;
					action_status.is_valid = true;
					LeaveCriticalSection(&action_section);
					break;
			}

			WakeConditionVariable(&action_condition);
			//action_condition.notify_one();
			return 0;
		case WM_DESTROY:
			PostQuitMessage(EXIT_SUCCESS);
			return 0;
	}

	return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int nCmdShow) {
	set_type wallpapers;

	wallpaper_set_mutex = CreateMutexW(NULL, FALSE, NULL);

	InitializeConditionVariable(&action_condition);
	InitializeConditionVariable(&empty_set_condition);

	InitializeCriticalSection(&empty_set_section);
	InitializeCriticalSection(&action_section);

	CreateWallpaperSet(wallpapers);

	bool is_set = SetWallpaperStyle(style, sizeof(style));

    MSG message = {};

    int window_width = 300;
    int window_height = 350;
    int window_x = GetSystemMetrics(SM_CXSCREEN) / 2 - window_width / 2;
    int window_y = GetSystemMetrics(SM_CYSCREEN) / 2 - window_height / 2;

    WNDCLASSEXW window_class = {};
    
    window_class.cbSize = sizeof(window_class);
    window_class.cbClsExtra = 0;
    window_class.cbWndExtra = 0;
    window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    window_class.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    window_class.hInstance = hInstance;
    window_class.lpfnWndProc = reinterpret_cast<WNDPROC>(window_procedure);
    window_class.lpszClassName = L"WallpaperSetter";
    window_class.lpszMenuName = nullptr;
    window_class.style = CS_HREDRAW | CS_VREDRAW;

    if (!RegisterClassExW(&window_class)) {
        return GetLastError();
    }

    main_window = CreateWindowW(window_class.lpszClassName,
        L"Wallpaper Setter", 0, window_x, window_y,
        window_width, window_height, nullptr, nullptr, hInstance, nullptr);

    if (main_window == INVALID_HANDLE_VALUE) {
        return EXIT_FAILURE;
    }

    ShowWindow(main_window, nCmdShow);
    UpdateWindow(main_window);

    if (!is_set) {
        MessageBoxW(main_window, L"Can't change wallpaper style!",
            L"Info", MB_ICONINFORMATION);
	}

	HANDLE threads[2] = {};

	threads[0] = CreateThread(NULL, 0, ChangeWallpaperLoop,
		reinterpret_cast<PVOID>(&wallpapers), 0, NULL);
    //std::thread wallpaper_loop(ChangeWallpaperLoop);
	//std::thread command_handler(CommandHandler);
	threads[1] = CreateThread(NULL, 0, WallpaperSetChanging,
		reinterpret_cast<PVOID>(&wallpapers), 0, NULL);
	//std::thread set_changing(WallpaperSetChanging);

    //wallpaper_loop.detach();
	//command_handler.detach();
	//set_changing.detach();

    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

	WaitForMultipleObjects(sizeof(threads) / sizeof(HANDLE),
		threads, FALSE, INFINITE); // WallpaperSetChanging doesn't terminate!

	for (size_t i = 0; i < sizeof(threads) / sizeof(HANDLE); ++i) {
		CloseHandle(threads[i]);
	}

	CloseHandle(wallpaper_set_mutex);
	DeleteCriticalSection(&action_section);
	DeleteCriticalSection(&empty_set_section);

    return static_cast<int>(message.wParam);
}