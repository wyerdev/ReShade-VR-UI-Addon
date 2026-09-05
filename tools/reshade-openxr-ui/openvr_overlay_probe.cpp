#include <Windows.h>
#include <openvr.h>

#include <cstdio>

int wmain(int argc, wchar_t *argv[])
{
	if (argc != 2)
	{
		std::fwprintf(stderr, L"usage: %s <openvr_api.dll>\n", argv[0]);
		return 2;
	}

	const HMODULE module = LoadLibraryW(argv[1]);
	if (module == nullptr)
		return static_cast<int>(GetLastError());

	const auto init = reinterpret_cast<uint32_t(VR_CALLTYPE *)(vr::EVRInitError *, vr::EVRApplicationType, const char *)>(GetProcAddress(module, "VR_InitInternal2"));
	const auto get_interface = reinterpret_cast<void *(VR_CALLTYPE *)(const char *, vr::EVRInitError *)>(GetProcAddress(module, "VR_GetGenericInterface"));
	const auto shutdown = reinterpret_cast<void(VR_CALLTYPE *)()>(GetProcAddress(module, "VR_ShutdownInternal"));
	if (init == nullptr || get_interface == nullptr || shutdown == nullptr)
	{
		FreeLibrary(module);
		return 3;
	}

	vr::EVRInitError error = vr::VRInitError_None;
	init(&error, vr::VRApplication_Overlay, nullptr);
	if (error != vr::VRInitError_None)
	{
		std::printf("INIT_ERROR:%d\n", static_cast<int>(error));
		FreeLibrary(module);
		return 4;
	}

	auto *const overlay = static_cast<vr::IVROverlay *>(get_interface(vr::IVROverlay_Version, &error));
	if (overlay == nullptr || error != vr::VRInitError_None)
	{
		std::printf("OVERLAY_INTERFACE_ERROR:%d\n", static_cast<int>(error));
		shutdown();
		FreeLibrary(module);
		return 5;
	}

	vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
	const vr::EVROverlayError find_error = overlay->FindOverlay("reshade", &handle);
	std::printf("FIND:%d\n", static_cast<int>(find_error));
	if (find_error == vr::VROverlayError_None)
	{
		char name[256] = {};
		vr::EVROverlayError name_error = vr::VROverlayError_None;
		overlay->GetOverlayName(handle, name, sizeof(name), &name_error);
		uint32_t scene_process = 0;
		const vr::EVROverlayError scene_error = overlay->GetDashboardOverlaySceneProcess(handle, &scene_process);
		std::printf("HANDLE:%llu\n", static_cast<unsigned long long>(handle));
		std::printf("NAME:%s\n", name_error == vr::VROverlayError_None ? name : "<error>");
		std::printf("VISIBLE:%d\n", overlay->IsOverlayVisible(handle) ? 1 : 0);
		std::printf("DASHBOARD_VISIBLE:%d\n", overlay->IsDashboardVisible() ? 1 : 0);
		std::printf("SCENE_PROCESS:%u\n", scene_error == vr::VROverlayError_None ? scene_process : 0);
	}

	shutdown();
	FreeLibrary(module);
	return find_error == vr::VROverlayError_None ? 0 : 1;
}