#include <Windows.h>

#ifdef STEAMVR_LOADER_TEST_MARKER
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
	return TRUE;
}
#else
int wmain(int argc, wchar_t *argv[])
{
	if (argc != 4 && argc != 5)
		return 2;
	if (LoadLibraryW(argv[3]) == nullptr)
		return 3;
	if (argc == 5 && _wcsicmp(argv[4], L"--expect-absent") == 0)
	{
		Sleep(1000);
		return GetModuleHandleW(argv[1]) == nullptr && GetModuleHandleW(argv[2]) == nullptr ? 0 : 1;
	}
	for (int attempt = 0; attempt < 600; ++attempt)
	{
		if (GetModuleHandleW(argv[1]) != nullptr && GetModuleHandleW(argv[2]) != nullptr)
			return 0;
		Sleep(5);
	}
	return 1;
}
#endif