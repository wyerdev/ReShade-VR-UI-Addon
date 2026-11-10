#pragma once

#include <windows.h>

#include <cwctype>
#include <filesystem>
#include <string>

// Switches the add-on off for a game after it crashed during startup, so the game can start without it next time.
// A crash counts only when it happens in the first startup_window_ms after the add-on starts and the add-on's code
// is on the crashing thread's call stack, walked from the failing instruction. Crashes later never switch it off.
// The switched-off state is a non-empty file per game executable in <settings folder>\disabled-games; deleting it
// (or running Re-enable-Games.cmd) turns the add-on back on.
// The file is created at startup already marked for deletion, so it vanishes when the game closes, and a crash only
// clears that mark and writes a line through the open handle (no memory allocation while the game is crashing).
// Games, runtimes and drivers also raise errors they catch and recover from; when the failing thread calls the
// add-on again, or frames keep coming survival_delay_ms later, the game survived and the mark is set again.
namespace startup_crash_guard
{
	constexpr ULONGLONG startup_window_ms = 90 * 1000;
	constexpr ULONGLONG survival_delay_ms = 5 * 1000;

	namespace detail
	{
		inline HANDLE s_marker = INVALID_HANDLE_VALUE;
		inline ULONGLONG s_start_tick = 0;
		inline UINT_PTR s_module_begin = 0;
		inline UINT_PTR s_module_end = 0;
		inline void *volatile s_handler = nullptr;
		inline volatile LONG s_crash_pending = 0;
		inline volatile LONG64 s_crash_tick = 0;
		inline volatile LONG s_crash_thread = 0;
		inline bool s_started = false;
		inline bool s_enabled = true;

		inline bool in_module(UINT_PTR address)
		{
			return address >= s_module_begin && address < s_module_end;
		}

		inline bool is_crash_code(DWORD code)
		{
			return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
				code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION ||
				code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED;
		}

		inline bool set_delete_on_close(bool remove)
		{
			FILE_DISPOSITION_INFO disposition = {};
			disposition.DeleteFile = remove ? TRUE : FALSE;
			return SetFileInformationByHandle(s_marker, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
		}

		// Starts at the failing instruction: a capture from inside the handler would always find the handler itself.
		inline bool add_on_on_stack(const CONTEXT &failure)
		{
			const NT_TIB *const thread_block = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
			const UINT_PTR stack_low = reinterpret_cast<UINT_PTR>(thread_block->StackLimit);
			const UINT_PTR stack_high = reinterpret_cast<UINT_PTR>(thread_block->StackBase);
#if defined(_M_X64)
			CONTEXT context = failure;
			for (int depth = 0; depth < 64 && context.Rip != 0; ++depth)
			{
				if (in_module(context.Rip))
					return true;
				DWORD64 image_base = 0;
				const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
				if (function == nullptr)
				{
					// Code without unwind data (or a call to a bad address) has its return address on top of the stack.
					if (context.Rsp < stack_low || context.Rsp + sizeof(DWORD64) > stack_high)
						return false;
					context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
					context.Rsp += sizeof(DWORD64);
					continue;
				}
				void *handler_data = nullptr;
				DWORD64 establisher_frame = 0;
				RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context, &handler_data, &establisher_frame, nullptr);
				if (context.Rsp < stack_low || context.Rsp >= stack_high)
					return false;
			}
			return false;
#elif defined(_M_IX86)
			if (in_module(failure.Eip))
				return true;
			// The 32-bit add-on is built with frame pointers (/Oy-), so its frames are in the saved-EBP chain.
			UINT_PTR frame = failure.Ebp;
			for (int depth = 0; depth < 64; ++depth)
			{
				if (frame < stack_low || frame + 2 * sizeof(UINT_PTR) > stack_high || frame % sizeof(UINT_PTR) != 0)
					return false;
				const UINT_PTR *const slots = reinterpret_cast<const UINT_PTR *>(frame);
				if (in_module(slots[1]))
					return true;
				if (slots[0] <= frame)
					return false;
				frame = slots[0];
			}
			return false;
#else
			static_cast<void>(failure);
			static_cast<void>(stack_low);
			static_cast<void>(stack_high);
			return false;
#endif
		}

		// Runs before the game's own handlers, for every error, so it only reads and changes the open file.
		inline LONG CALLBACK on_exception(EXCEPTION_POINTERS *info)
		{
			if (!is_crash_code(info->ExceptionRecord->ExceptionCode) || GetTickCount64() - s_start_tick > startup_window_ms)
				return EXCEPTION_CONTINUE_SEARCH;
			if (!add_on_on_stack(*info->ContextRecord))
				return EXCEPTION_CONTINUE_SEARCH;

			InterlockedExchange(&s_crash_thread, static_cast<LONG>(GetCurrentThreadId()));
			InterlockedExchange64(&s_crash_tick, static_cast<LONG64>(GetTickCount64()));
			if (InterlockedCompareExchange(&s_crash_pending, 1, 0) == 0)
			{
				static const char text[] = "ReShade VR UI Add-on: this game crashed while starting, so the add-on stays off for it.\r\nDelete this file to turn it on again.\r\n";
				OVERLAPPED start_of_file = {};
				DWORD written = 0;
				WriteFile(s_marker, text, sizeof(text) - 1, &written, &start_of_file);
				set_delete_on_close(false);
			}
			return EXCEPTION_CONTINUE_SEARCH;
		}

		inline bool marker_switches_off(const std::filesystem::path &marker)
		{
			// A file the running copy of the game holds open for deletion cannot be read and does not count.
			WIN32_FILE_ATTRIBUTE_DATA data = {};
			return GetFileAttributesExW(marker.c_str(), GetFileExInfoStandard, &data) != FALSE &&
				(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
				(data.nFileSizeHigh != 0 || data.nFileSizeLow != 0);
		}

		inline std::wstring game_file_name()
		{
			std::wstring executable(MAX_PATH, L'\0');
			DWORD length = 0;
			while ((length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()))) == executable.size())
				executable.resize(executable.size() * 2);
			executable.resize(length);
			const size_t separator = executable.find_last_of(L"\\/");
			std::wstring name = separator == std::wstring::npos ? executable : executable.substr(separator + 1);
			for (wchar_t &character : name)
				character = static_cast<wchar_t>(towlower(character));
			return name + L".txt";
		}
	}

	// Call once before the add-on registers with ReShade. Returns false when the add-on is switched off for this game.
	inline bool begin(HMODULE module, const std::filesystem::path &settings_directory)
	{
		if (detail::s_started)
			return detail::s_enabled;
		detail::s_started = true;
		if (module == nullptr || settings_directory.empty())
			return true;

		const std::filesystem::path directory = settings_directory / L"disabled-games";
		const std::filesystem::path marker = directory / detail::game_file_name();
		if (detail::marker_switches_off(marker))
		{
			detail::s_enabled = false;
			return false;
		}

		std::error_code error;
		std::filesystem::create_directories(directory, error);
		// A second copy of the same game cannot open the file while the first one runs, and then starts unguarded.
		const HANDLE file = CreateFileW(marker.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return true;
		detail::s_marker = file;
		if (!detail::set_delete_on_close(true))
		{
			CloseHandle(file);
			detail::s_marker = INVALID_HANDLE_VALUE;
			DeleteFileW(marker.c_str());
			return true;
		}

		const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(module);
		const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(reinterpret_cast<const BYTE *>(module) + dos->e_lfanew);
		detail::s_module_begin = reinterpret_cast<UINT_PTR>(module);
		detail::s_module_end = detail::s_module_begin + nt->OptionalHeader.SizeOfImage;
		detail::s_start_tick = GetTickCount64();
		detail::s_handler = AddVectoredExceptionHandler(1, detail::on_exception);
		return true;
	}

	inline bool switched_off()
	{
		return detail::s_started && !detail::s_enabled;
	}

	// Call from the add-on's frame and OpenXR entry points on any thread; costs a few reads when nothing is pending.
	inline void note_activity(ULONGLONG now = GetTickCount64())
	{
		if (detail::s_crash_pending != 0)
		{
			const ULONGLONG crash_tick = static_cast<ULONGLONG>(InterlockedCompareExchange64(&detail::s_crash_tick, 0, 0));
			const bool failing_thread_continued = static_cast<DWORD>(detail::s_crash_thread) == GetCurrentThreadId();
			const bool frames_continued = now >= crash_tick && now - crash_tick >= survival_delay_ms;
			if ((failing_thread_continued || frames_continued) && InterlockedCompareExchange(&detail::s_crash_pending, 0, 1) == 1)
			{
				detail::set_delete_on_close(true);
				// Keep the file when another crash arrived while the mark was being set again.
				if (detail::s_crash_pending != 0)
					detail::set_delete_on_close(false);
			}
		}
		if (detail::s_handler != nullptr && detail::s_crash_pending == 0 && now - detail::s_start_tick > startup_window_ms)
		{
			if (void *const handler = InterlockedExchangePointer(&detail::s_handler, nullptr))
				RemoveVectoredExceptionHandler(handler);
		}
	}

	// Call when the DLL unloads; the handler must not outlive the code it points to.
	inline void end()
	{
		if (void *const handler = InterlockedExchangePointer(&detail::s_handler, nullptr))
			RemoveVectoredExceptionHandler(handler);
		if (detail::s_marker != INVALID_HANDLE_VALUE)
			CloseHandle(detail::s_marker);
		detail::s_marker = INVALID_HANDLE_VALUE;
	}
}
