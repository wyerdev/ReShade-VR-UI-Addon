#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
	constexpr wchar_t mutex_name[] = L"Local\\ReShadeVRUILoader";
	constexpr wchar_t stop_event_name[] = L"Local\\ReShadeVRUILoaderStop";
	constexpr wchar_t stopped_event_name[] = L"Local\\ReShadeVRUILoaderStopped";

	struct scoped_handle
	{
		HANDLE value = nullptr;
		~scoped_handle()
		{
			if (value != nullptr && value != INVALID_HANDLE_VALUE)
				CloseHandle(value);
		}
		operator HANDLE() const { return value; }
	};

	std::filesystem::path module_directory()
	{
		wchar_t path[4096] = {};
		const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
		return std::filesystem::path(path, path + length).parent_path();
	}

	std::filesystem::path program_data_directory()
	{
		wchar_t path[4096] = {};
		const DWORD length = GetEnvironmentVariableW(L"ProgramData", path, static_cast<DWORD>(std::size(path)));
		return length > 0 && length < std::size(path) ? std::filesystem::path(path, path + length) : std::filesystem::path();
	}

	std::filesystem::path user_data_directory()
	{
		wchar_t path[4096] = {};
		const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", path, static_cast<DWORD>(std::size(path)));
		if (length == 0 || length >= std::size(path))
			return {};
		const std::filesystem::path directory = std::filesystem::path(path, path + length) / L"ReShade System-Wide SteamVR Listener";
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		return error ? std::filesystem::path() : directory;
	}

	void log_message(const std::wstring &message)
	{
		const std::filesystem::path directory = user_data_directory();
		if (directory.empty())
			return;
		std::wofstream stream(directory / L"ReShade-System-Wide-SteamVR-Listener.log", std::ios::app);
		if (!stream)
			return;
		SYSTEMTIME time = {};
		GetLocalTime(&time);
		stream << L'[' << time.wYear << L'-' << time.wMonth << L'-' << time.wDay << L' '
			<< time.wHour << L':' << time.wMinute << L':' << time.wSecond << L"] " << message << L'\n';
	}

	std::wstring normalize_path(const std::filesystem::path &path)
	{
		std::error_code error;
		std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
		if (error)
			normalized = path.lexically_normal();
		std::wstring result = normalized.native();
		std::transform(result.begin(), result.end(), result.begin(), [](wchar_t character) { return static_cast<wchar_t>(towlower(character)); });
		return result;
	}

	std::vector<std::wstring> load_allowlist(const std::filesystem::path &path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			return {};
		const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		const size_t value_begin = bytes.find("Apps=");
		if (value_begin == std::string::npos)
			return {};
		const size_t value_end = bytes.find_first_of("\r\n", value_begin);
		const std::string value = bytes.substr(value_begin + 5, value_end - value_begin - 5);
		const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
		if (required <= 0)
			return {};
		std::wstring decoded(required, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), decoded.data(), required);

		std::vector<std::wstring> paths;
		size_t begin = 0;
		while (begin < decoded.size())
		{
			const size_t end = decoded.find(L',', begin);
			const std::wstring entry = decoded.substr(begin, end == std::wstring::npos ? std::wstring::npos : end - begin);
			const size_t first = entry.find_first_not_of(L" \t");
			const size_t last = entry.find_last_not_of(L" \t");
			if (first != std::wstring::npos)
				paths.push_back(normalize_path(entry.substr(first, last - first + 1)));
			if (end == std::wstring::npos)
				break;
			begin = end + 1;
		}
		return paths;
	}

	std::filesystem::path process_path(DWORD process_id)
	{
		scoped_handle process = { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id) };
		if (process.value == nullptr)
			return {};
		wchar_t path[4096] = {};
		DWORD length = static_cast<DWORD>(std::size(path));
		if (!QueryFullProcessImageNameW(process, 0, path, &length))
			return {};
		return std::filesystem::path(path, path + length);
	}

	bool process_has_module(DWORD process_id, const std::filesystem::path &library)
	{
		const std::wstring expected_path = normalize_path(library);
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			scoped_handle snapshot = { CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id) };
			if (snapshot.value != INVALID_HANDLE_VALUE)
			{
				MODULEENTRY32W entry = { sizeof(entry) };
				for (BOOL has_entry = Module32FirstW(snapshot, &entry); has_entry; has_entry = Module32NextW(snapshot, &entry))
					if (normalize_path(entry.szExePath) == expected_path)
						return true;
			}
			Sleep(2);
		}
		return false;
	}

	enum class vr_backend
	{
		unknown,
		openxr,
		openvr
	};

	vr_backend detect_vr_backend(DWORD process_id)
	{
		scoped_handle snapshot = { CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id) };
		if (snapshot.value == INVALID_HANDLE_VALUE)
			return vr_backend::unknown;

		bool openvr_loaded = false;
		MODULEENTRY32W entry = { sizeof(entry) };
		for (BOOL has_entry = Module32FirstW(snapshot, &entry); has_entry; has_entry = Module32NextW(snapshot, &entry))
		{
			if (_wcsicmp(entry.szModule, L"openxr_loader.dll") == 0)
				return vr_backend::openxr;
			if (_wcsicmp(entry.szModule, L"openvr_api.dll") == 0)
				openvr_loaded = true;
		}
		return openvr_loaded ? vr_backend::openvr : vr_backend::unknown;
	}

	bool inject_library(DWORD process_id, const std::filesystem::path &library)
	{
		scoped_handle process = { OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, process_id) };
		if (process.value == nullptr)
			return false;
		BOOL target_is_wow64 = FALSE;
		if (!IsWow64Process(process, &target_is_wow64) || target_is_wow64)
			return false;

		const std::wstring path = library.native();
		const size_t path_size = (path.size() + 1) * sizeof(wchar_t);
		void *const remote_path = VirtualAllocEx(process, nullptr, path_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (remote_path == nullptr)
			return false;
		if (!WriteProcessMemory(process, remote_path, path.c_str(), path_size, nullptr))
		{
			VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
			return false;
		}

		const auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
		scoped_handle thread = { CreateRemoteThread(process, nullptr, 0, load_library, remote_path, 0, nullptr) };
		if (thread.value == nullptr)
		{
			VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
			return false;
		}
		const DWORD wait_result = WaitForSingleObject(thread, 10000);
		DWORD exit_code = 0;
		const bool succeeded = wait_result == WAIT_OBJECT_0 && GetExitCodeThread(thread, &exit_code) && exit_code != 0 &&
			process_has_module(process_id, library);
		VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
		return succeeded;
	}

	struct file_stamp
	{
		FILETIME last_write = {};
		DWORD size = 0;
		bool operator==(const file_stamp &other) const
		{
			return CompareFileTime(&last_write, &other.last_write) == 0 && size == other.size;
		}
	};

	file_stamp read_file_stamp(const std::filesystem::path &path)
	{
		WIN32_FILE_ATTRIBUTE_DATA data = {};
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
			return {};
		return { data.ftLastWriteTime, data.nFileSizeLow };
	}

	// New processes are found by a full process list, which is the main idle cost.
	constexpr ULONGLONG process_scan_interval_ms = 500;
	constexpr ULONGLONG allowlist_check_interval_ms = 5000;
	// A listed game is checked quickly for its VR library so injection stays early,
	// then slowly once it has run without VR for a while (for example in flat mode).
	constexpr ULONGLONG fast_backend_check_interval_ms = 25;
	constexpr ULONGLONG slow_backend_check_interval_ms = 1000;
	constexpr ULONGLONG fast_backend_check_duration_ms = 60000;

	struct injection_progress
	{
		ULONGLONG detected_at = 0;
		ULONGLONG next_check = 0;
		uint32_t attempts = 0;
		bool reshade_loaded = false;
	};
}

int wmain(int argc, wchar_t *argv[])
{
	if (argc == 2 && _wcsicmp(argv[1], L"--stop") == 0)
	{
		scoped_handle stop_event = { OpenEventW(EVENT_MODIFY_STATE, FALSE, stop_event_name) };
		scoped_handle stopped_event = { OpenEventW(SYNCHRONIZE, FALSE, stopped_event_name) };
		if (stop_event.value == nullptr)
			return 0;
		if (!SetEvent(stop_event))
			return 1;
		return stopped_event.value == nullptr || WaitForSingleObject(stopped_event, 10000) == WAIT_OBJECT_0 ? 0 : 1;
	}

	std::filesystem::path allowlist = program_data_directory() / L"ReShade" / L"ReShadeApps.ini";
	std::filesystem::path reshade = program_data_directory() / L"ReShade" / L"ReShade64.dll";
	std::filesystem::path addon = module_directory() / L"ReShade-SteamVR-UI.addon64";
	bool exit_after_injection = false;
	if (argc == 6 && _wcsicmp(argv[1], L"--test") == 0)
	{
		allowlist = argv[2];
		reshade = argv[3];
		addon = argv[4];
		exit_after_injection = _wcsicmp(argv[5], L"--once") == 0;
	}
	if (!std::filesystem::is_regular_file(allowlist) || !std::filesystem::is_regular_file(reshade) || !std::filesystem::is_regular_file(addon))
	{
		log_message(L"Required central ReShade, allowlist, or SteamVR UI file is missing.");
		return 2;
	}

	const wchar_t *const active_mutex_name = exit_after_injection ? L"Local\\ReShadeVRUILoaderTest" : mutex_name;
	const wchar_t *const active_stop_event_name = exit_after_injection ? L"Local\\ReShadeVRUILoaderTestStop" : stop_event_name;
	scoped_handle mutex = { CreateMutexW(nullptr, FALSE, active_mutex_name) };
	if (mutex.value == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
		return 0;
	scoped_handle stop_event = { CreateEventW(nullptr, TRUE, FALSE, active_stop_event_name) };
	if (stop_event.value == nullptr)
		return 3;
	scoped_handle stopped_event;
	if (!exit_after_injection)
	{
		stopped_event.value = CreateEventW(nullptr, TRUE, FALSE, stopped_event_name);
		if (stopped_event.value == nullptr)
			return 3;
	}

	std::set<DWORD> observed_processes;
	std::map<DWORD, injection_progress> pending_injections;
	std::vector<std::wstring> allowed_paths = load_allowlist(allowlist);
	file_stamp allowlist_stamp = read_file_stamp(allowlist);
	log_message(L"Loader started with " + std::to_wstring(allowed_paths.size()) + L" centrally enabled application(s).");
	const DWORD own_process_id = GetCurrentProcessId();
	ULONGLONG next_allowlist_check = GetTickCount64() + allowlist_check_interval_ms;
	ULONGLONG next_process_scan = 0;
	DWORD wait_ms = 0;
	while (WaitForSingleObject(stop_event, wait_ms) == WAIT_TIMEOUT)
	{
		ULONGLONG now = GetTickCount64();
		if (now >= next_allowlist_check)
		{
			const file_stamp stamp = read_file_stamp(allowlist);
			if (!(stamp == allowlist_stamp))
			{
				allowed_paths = load_allowlist(allowlist);
				allowlist_stamp = stamp;
			}
			next_allowlist_check = now + allowlist_check_interval_ms;
		}

		if (now >= next_process_scan)
		{
			next_process_scan = now + process_scan_interval_ms;
			scoped_handle snapshot = { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
			if (snapshot.value != INVALID_HANDLE_VALUE)
			{
				std::vector<DWORD> current_processes;
				PROCESSENTRY32W entry = { sizeof(entry) };
				for (BOOL has_entry = Process32FirstW(snapshot, &entry); has_entry; has_entry = Process32NextW(snapshot, &entry))
				{
					const DWORD process_id = entry.th32ProcessID;
					current_processes.push_back(process_id);
					if (process_id == own_process_id || observed_processes.count(process_id) != 0 || pending_injections.count(process_id) != 0)
						continue;
					const std::filesystem::path image_path = process_path(process_id);
					if (image_path.empty())
						continue;
					if (std::find(allowed_paths.begin(), allowed_paths.end(), normalize_path(image_path)) == allowed_paths.end())
					{
						observed_processes.insert(process_id);
						continue;
					}
					log_message(L"Detected enabled application: " + image_path.native());
					injection_progress &progress = pending_injections[process_id];
					progress.detected_at = now;
					progress.next_check = now;
				}
				std::sort(current_processes.begin(), current_processes.end());
				const auto exited = [&current_processes](DWORD process_id) {
					return !std::binary_search(current_processes.begin(), current_processes.end(), process_id);
				};
				for (auto iterator = observed_processes.begin(); iterator != observed_processes.end();)
					iterator = exited(*iterator) ? observed_processes.erase(iterator) : std::next(iterator);
				for (auto iterator = pending_injections.begin(); iterator != pending_injections.end();)
					iterator = exited(iterator->first) ? pending_injections.erase(iterator) : std::next(iterator);
			}
		}

		ULONGLONG next_wake = (std::min)(next_process_scan, next_allowlist_check);
		for (auto iterator = pending_injections.begin(); iterator != pending_injections.end();)
		{
			const DWORD process_id = iterator->first;
			injection_progress &progress = iterator->second;
			if (now < progress.next_check)
			{
				next_wake = (std::min)(next_wake, progress.next_check);
				++iterator;
				continue;
			}

			const vr_backend backend = detect_vr_backend(process_id);
			if (backend == vr_backend::openxr)
			{
				log_message(L"Detected OpenXR in process " + std::to_wstring(process_id) + L"; leaving activation to the OpenXR API layer.");
				observed_processes.insert(process_id);
				iterator = pending_injections.erase(iterator);
				if (exit_after_injection)
					return 0;
				continue;
			}
			if (backend == vr_backend::openvr)
			{
				++progress.attempts;
				if (!progress.reshade_loaded)
					progress.reshade_loaded = inject_library(process_id, reshade);
				if (progress.reshade_loaded && inject_library(process_id, addon))
				{
					log_message(L"Loaded central ReShade and SteamVR UI into process " + std::to_wstring(process_id) + L'.');
					observed_processes.insert(process_id);
					iterator = pending_injections.erase(iterator);
					if (exit_after_injection)
						return 0;
					continue;
				}
				if (progress.attempts >= 80)
				{
					log_message(L"Failed to load central ReShade or SteamVR UI into process " + std::to_wstring(process_id) + L" after 80 attempts.");
					observed_processes.insert(process_id);
					iterator = pending_injections.erase(iterator);
					continue;
				}
			}

			now = GetTickCount64();
			const bool fast = backend == vr_backend::openvr || now - progress.detected_at < fast_backend_check_duration_ms;
			progress.next_check = now + (fast ? fast_backend_check_interval_ms : slow_backend_check_interval_ms);
			next_wake = (std::min)(next_wake, progress.next_check);
			++iterator;
		}

		now = GetTickCount64();
		wait_ms = next_wake > now ? static_cast<DWORD>(next_wake - now) : 0;
	}
	if (stopped_event.value != nullptr)
		SetEvent(stopped_event);
	log_message(L"Loader stopped.");
	return 0;
}