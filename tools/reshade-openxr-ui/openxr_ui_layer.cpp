#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12

#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <wrl/client.h>

#include "openxr_effect_fallback.hpp"
#include "openxr_ui_model.hpp"
#include "openxr_ui_renderer.hpp"
#include "vr_ui_state.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

extern "C" XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance instance, const char *name, PFN_xrVoidFunction *function);
extern "C" XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance);
extern "C" XrResult XRAPI_CALL xrCreateSession(XrInstance instance, const XrSessionCreateInfo *create_info, XrSession *session);
extern "C" XrResult XRAPI_CALL xrDestroySession(XrSession session);
extern "C" XrResult XRAPI_CALL xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo *create_info, XrSwapchain *swapchain);
extern "C" XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain swapchain);
extern "C" XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo *acquire_info, uint32_t *index);
extern "C" XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo *release_info);
extern "C" XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance instance, const XrInteractionProfileSuggestedBinding *suggested_bindings);
extern "C" XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo *attach_info);
extern "C" XrResult XRAPI_CALL xrWaitFrame(XrSession session, const XrFrameWaitInfo *wait_info, XrFrameState *frame_state);
extern "C" XrResult XRAPI_CALL xrEndFrame(XrSession session, const XrFrameEndInfo *frame_end_info);

namespace
{
	using Microsoft::WRL::ComPtr;

	constexpr float panel_world_width = 0.96f;
	constexpr float compact_panel_world_height = panel_world_width * openxr_ui::compact_panel_height / openxr_ui::panel_width;
	constexpr float expanded_panel_world_height = panel_world_width * openxr_ui::expanded_panel_height / openxr_ui::panel_width;
	constexpr float panel_y = -0.08f;
	constexpr float panel_z = -1.0f;
	constexpr LONG pointer_margin = 12;
	constexpr float pointer_hover_dead_zone = 6.0f;
	constexpr float pointer_hover_follow_rate = 0.18f;
	constexpr float pointer_drag_follow_rate = 0.35f;
	constexpr float pointer_hover_output_step = 2.0f;
	constexpr XrDuration menu_long_press_duration = 1500000000;
	// Official ReShade creates its VR runtime on its first xrEndFrame, so it gets this many frames before the fallback starts.
	constexpr uint32_t effect_fallback_wait_frames = 10;

	enum class graphics_api
	{
		none,
		d3d11,
		d3d12,
	};

	struct instance_dispatch
	{
		PFN_xrGetInstanceProcAddr GetInstanceProcAddr = nullptr;
		PFN_xrDestroyInstance DestroyInstance = nullptr;
		PFN_xrCreateSession CreateSession = nullptr;
		PFN_xrDestroySession DestroySession = nullptr;
		PFN_xrCreateReferenceSpace CreateReferenceSpace = nullptr;
		PFN_xrDestroySpace DestroySpace = nullptr;
		PFN_xrLocateSpace LocateSpace = nullptr;
		PFN_xrEnumerateSwapchainFormats EnumerateSwapchainFormats = nullptr;
		PFN_xrCreateSwapchain CreateSwapchain = nullptr;
		PFN_xrDestroySwapchain DestroySwapchain = nullptr;
		PFN_xrEnumerateSwapchainImages EnumerateSwapchainImages = nullptr;
		PFN_xrAcquireSwapchainImage AcquireSwapchainImage = nullptr;
		PFN_xrWaitSwapchainImage WaitSwapchainImage = nullptr;
		PFN_xrReleaseSwapchainImage ReleaseSwapchainImage = nullptr;
		PFN_xrCreateActionSet CreateActionSet = nullptr;
		PFN_xrDestroyActionSet DestroyActionSet = nullptr;
		PFN_xrCreateAction CreateAction = nullptr;
		PFN_xrCreateActionSpace CreateActionSpace = nullptr;
		PFN_xrStringToPath StringToPath = nullptr;
		PFN_xrSuggestInteractionProfileBindings SuggestInteractionProfileBindings = nullptr;
		PFN_xrAttachSessionActionSets AttachSessionActionSets = nullptr;
		PFN_xrSyncActions SyncActions = nullptr;
		PFN_xrGetActionStatePose GetActionStatePose = nullptr;
		PFN_xrGetActionStateBoolean GetActionStateBoolean = nullptr;
		PFN_xrGetActionStateVector2f GetActionStateVector2f = nullptr;
		PFN_xrWaitFrame WaitFrame = nullptr;
		PFN_xrEndFrame EndFrame = nullptr;
		XrInstance instance = XR_NULL_HANDLE;
		XrActionSet action_set = XR_NULL_HANDLE;
		XrAction aim_action = XR_NULL_HANDLE;
		XrAction select_action = XR_NULL_HANDLE;
		XrAction right_menu_toggle_action = XR_NULL_HANDLE;
		XrAction left_menu_toggle_action = XR_NULL_HANDLE;
		XrAction simple_menu_toggle_action = XR_NULL_HANDLE;
		XrAction scroll_action = XR_NULL_HANDLE;
		XrPath right_hand_path = XR_NULL_PATH;
		XrPath left_hand_path = XR_NULL_PATH;
		XrPath oculus_profile = XR_NULL_PATH;
		XrPath simple_profile = XR_NULL_PATH;
		XrPath aim_path = XR_NULL_PATH;
		XrPath trigger_path = XR_NULL_PATH;
		XrPath select_path = XR_NULL_PATH;
		XrPath right_thumbstick_click_path = XR_NULL_PATH;
		XrPath left_thumbstick_click_path = XR_NULL_PATH;
		XrPath thumbstick_path = XR_NULL_PATH;
		XrPath menu_click_path = XR_NULL_PATH;
		bool actions_ready = false;
	};

	struct session_state
	{
		XrInstance instance = XR_NULL_HANDLE;
		XrSession session = XR_NULL_HANDLE;
		std::shared_ptr<instance_dispatch> dispatch;
		graphics_api api = graphics_api::none;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Query> completion_query;
		ComPtr<ID3D12Device> device12;
		ComPtr<ID3D12CommandQueue> command_queue12;
		ComPtr<ID3D12DescriptorHeap> render_target_heap12;
		ComPtr<ID3D12CommandAllocator> command_allocator12;
		ComPtr<ID3D12GraphicsCommandList> command_list12;
		ComPtr<ID3D12Fence> completion_fence12;
		HANDLE completion_event12 = nullptr;
		uint64_t completion_value12 = 0;
		XrSpace view_space = XR_NULL_HANDLE;
		XrSwapchain quad_swapchain = XR_NULL_HANDLE;
		int64_t quad_format = 0;
		std::vector<XrSwapchainImageD3D11KHR> images;
		std::vector<ComPtr<ID3D11RenderTargetView>> render_target_views;
		std::vector<XrSwapchainImageD3D12KHR> images12;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> render_target_views12;
		std::unique_ptr<openxr_ui::panel_renderer> renderer;
		XrSpace aim_space = XR_NULL_HANDLE;
		bool actions_attached = false;
		bool should_render = true;
		bool pointer_visible = false;
		LONG pointer_x = 0;
		LONG pointer_y = 0;
		float filtered_pointer_x = 0.0f;
		float filtered_pointer_y = 0.0f;
		bool pointer_filter_ready = false;
		bool select_was_pressed = false;
		bool select_clicked = false;
		bool menu_chord_was_pressed = false;
		bool menu_chord_triggered = false;
		XrTime menu_chord_started = 0;
		bool panel_visible = true;
		bool ui_starts_open = true;
		bool performance_mode = false;
		bool shaders_temporarily_disabled = false;
		bool preset_dirty = false;
		bool menu_long_press_enabled = false;
		bool panel_expanded = false;
		bool select_down = false;
		bool select_released = false;
		float scroll_y = 0.0f;
		bool quad_ready = false;
		bool quad_logged = false;
		std::unique_ptr<openxr_ui::effect_fallback> effects;
		std::vector<XrSwapchain> effect_swapchains;
		std::vector<XrSwapchain> pending_releases;
		uint32_t frames_without_vr_runtime = 0;
		bool effects_decided = false;
	};

	struct swapchain_state
	{
		XrSession session = XR_NULL_HANDLE;
		std::shared_ptr<instance_dispatch> dispatch;
		std::vector<void *> textures;
		std::deque<uint32_t> acquired;
		// Eye regions of this swapchain in the last submitted projection layer; the texture is filled in on release.
		std::vector<openxr_ui::effect_view> effect_regions;
		// The application released the oldest acquired image, but the layer holds it until the frame's other eyes arrive.
		bool release_pending = false;
		bool static_image = false;
	};

	std::mutex s_layer_mutex;
	std::unordered_map<XrInstance, std::shared_ptr<instance_dispatch>> s_instances;
	std::unordered_map<XrSession, std::unique_ptr<session_state>> s_sessions;
	std::unordered_map<XrSwapchain, swapchain_state> s_swapchains;

	template <typename T>
	bool load_proc(PFN_xrGetInstanceProcAddr get_instance_proc, XrInstance instance, const char *name, T &target)
	{
		return XR_SUCCEEDED(get_instance_proc(instance, name, reinterpret_cast<PFN_xrVoidFunction *>(&target))) && target != nullptr;
	}

	bool load_dispatch(PFN_xrGetInstanceProcAddr get_instance_proc, XrInstance instance, instance_dispatch &dispatch)
	{
		dispatch.GetInstanceProcAddr = get_instance_proc;
		return load_proc(get_instance_proc, instance, "xrDestroyInstance", dispatch.DestroyInstance) &&
			load_proc(get_instance_proc, instance, "xrCreateSession", dispatch.CreateSession) &&
			load_proc(get_instance_proc, instance, "xrDestroySession", dispatch.DestroySession) &&
			load_proc(get_instance_proc, instance, "xrCreateReferenceSpace", dispatch.CreateReferenceSpace) &&
			load_proc(get_instance_proc, instance, "xrDestroySpace", dispatch.DestroySpace) &&
			load_proc(get_instance_proc, instance, "xrLocateSpace", dispatch.LocateSpace) &&
			load_proc(get_instance_proc, instance, "xrEnumerateSwapchainFormats", dispatch.EnumerateSwapchainFormats) &&
			load_proc(get_instance_proc, instance, "xrCreateSwapchain", dispatch.CreateSwapchain) &&
			load_proc(get_instance_proc, instance, "xrDestroySwapchain", dispatch.DestroySwapchain) &&
			load_proc(get_instance_proc, instance, "xrEnumerateSwapchainImages", dispatch.EnumerateSwapchainImages) &&
			load_proc(get_instance_proc, instance, "xrAcquireSwapchainImage", dispatch.AcquireSwapchainImage) &&
			load_proc(get_instance_proc, instance, "xrWaitSwapchainImage", dispatch.WaitSwapchainImage) &&
			load_proc(get_instance_proc, instance, "xrReleaseSwapchainImage", dispatch.ReleaseSwapchainImage) &&
			load_proc(get_instance_proc, instance, "xrCreateActionSet", dispatch.CreateActionSet) &&
			load_proc(get_instance_proc, instance, "xrDestroyActionSet", dispatch.DestroyActionSet) &&
			load_proc(get_instance_proc, instance, "xrCreateAction", dispatch.CreateAction) &&
			load_proc(get_instance_proc, instance, "xrCreateActionSpace", dispatch.CreateActionSpace) &&
			load_proc(get_instance_proc, instance, "xrStringToPath", dispatch.StringToPath) &&
			load_proc(get_instance_proc, instance, "xrSuggestInteractionProfileBindings", dispatch.SuggestInteractionProfileBindings) &&
			load_proc(get_instance_proc, instance, "xrAttachSessionActionSets", dispatch.AttachSessionActionSets) &&
			load_proc(get_instance_proc, instance, "xrSyncActions", dispatch.SyncActions) &&
			load_proc(get_instance_proc, instance, "xrGetActionStatePose", dispatch.GetActionStatePose) &&
			load_proc(get_instance_proc, instance, "xrGetActionStateBoolean", dispatch.GetActionStateBoolean) &&
			load_proc(get_instance_proc, instance, "xrGetActionStateVector2f", dispatch.GetActionStateVector2f) &&
			load_proc(get_instance_proc, instance, "xrWaitFrame", dispatch.WaitFrame) &&
			load_proc(get_instance_proc, instance, "xrEndFrame", dispatch.EndFrame);
	}

	std::shared_ptr<instance_dispatch> find_instance(XrInstance instance)
	{
		const std::lock_guard<std::mutex> lock(s_layer_mutex);
		const auto instance_it = s_instances.find(instance);
		return instance_it != s_instances.end() ? instance_it->second : nullptr;
	}

	session_state *find_session(XrSession session)
	{
		const std::lock_guard<std::mutex> lock(s_layer_mutex);
		const auto session_it = s_sessions.find(session);
		return session_it != s_sessions.end() ? session_it->second.get() : nullptr;
	}

	swapchain_state *find_swapchain(XrSwapchain swapchain)
	{
		const std::lock_guard<std::mutex> lock(s_layer_mutex);
		const auto swapchain_it = s_swapchains.find(swapchain);
		return swapchain_it != s_swapchains.end() ? &swapchain_it->second : nullptr;
	}

	const XrGraphicsBindingD3D11KHR *find_d3d11_binding(const XrSessionCreateInfo *create_info)
	{
		for (const XrBaseInStructure *entry = reinterpret_cast<const XrBaseInStructure *>(create_info->next); entry != nullptr; entry = entry->next)
			if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)
				return reinterpret_cast<const XrGraphicsBindingD3D11KHR *>(entry);
		return nullptr;
	}

	const XrGraphicsBindingD3D12KHR *find_d3d12_binding(const XrSessionCreateInfo *create_info)
	{
		for (const XrBaseInStructure *entry = reinterpret_cast<const XrBaseInStructure *>(create_info->next); entry != nullptr; entry = entry->next)
			if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR)
				return reinterpret_cast<const XrGraphicsBindingD3D12KHR *>(entry);
		return nullptr;
	}

	bool string_to_path(instance_dispatch &dispatch, const char *path_string, XrPath &path)
	{
		return XR_SUCCEEDED(dispatch.StringToPath(dispatch.instance, path_string, &path));
	}

	void suggest_bindings(instance_dispatch &dispatch, XrPath profile, const std::vector<XrActionSuggestedBinding> &bindings)
	{
		if (profile == XR_NULL_PATH)
			return;
		XrInteractionProfileSuggestedBinding suggestion = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		suggestion.interactionProfile = profile;
		suggestion.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
		suggestion.suggestedBindings = bindings.data();
		dispatch.SuggestInteractionProfileBindings(dispatch.instance, &suggestion);
	}

	bool initialize_actions(instance_dispatch &dispatch, XrInstance instance)
	{
		dispatch.instance = instance;
		if (!string_to_path(dispatch, "/user/hand/right", dispatch.right_hand_path) ||
			!string_to_path(dispatch, "/user/hand/left", dispatch.left_hand_path))
			return false;

		XrActionSetCreateInfo action_set_info = { XR_TYPE_ACTION_SET_CREATE_INFO };
		strcpy_s(action_set_info.actionSetName, "reshade_menu");
		strcpy_s(action_set_info.localizedActionSetName, "ReShade Menu");
		action_set_info.priority = 0;
		if (XR_FAILED(dispatch.CreateActionSet(instance, &action_set_info, &dispatch.action_set)))
			return false;

		XrActionCreateInfo action_info = { XR_TYPE_ACTION_CREATE_INFO };
		action_info.countSubactionPaths = 1;
		action_info.subactionPaths = &dispatch.right_hand_path;
		action_info.actionType = XR_ACTION_TYPE_POSE_INPUT;
		strcpy_s(action_info.actionName, "menu_aim");
		strcpy_s(action_info.localizedActionName, "Menu Aim");
		if (XR_FAILED(dispatch.CreateAction(dispatch.action_set, &action_info, &dispatch.aim_action)))
			return false;

		action_info = { XR_TYPE_ACTION_CREATE_INFO };
		action_info.countSubactionPaths = 1;
		action_info.subactionPaths = &dispatch.right_hand_path;
		action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
		strcpy_s(action_info.actionName, "menu_select");
		strcpy_s(action_info.localizedActionName, "Menu Select");
		if (XR_FAILED(dispatch.CreateAction(dispatch.action_set, &action_info, &dispatch.select_action)))
			return false;

		action_info = { XR_TYPE_ACTION_CREATE_INFO };
		action_info.countSubactionPaths = 1;
		action_info.subactionPaths = &dispatch.right_hand_path;
		action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
		strcpy_s(action_info.actionName, "menu_toggle");
		strcpy_s(action_info.localizedActionName, "Show or Hide Menu");
		if (XR_FAILED(dispatch.CreateAction(dispatch.action_set, &action_info, &dispatch.right_menu_toggle_action)))
			return false;

		action_info.subactionPaths = &dispatch.left_hand_path;
		strcpy_s(action_info.actionName, "menu_toggle_left");
		strcpy_s(action_info.localizedActionName, "Show or Hide Menu Left");
		if (XR_FAILED(dispatch.CreateAction(dispatch.action_set, &action_info, &dispatch.left_menu_toggle_action)))
			return false;

		action_info.subactionPaths = &dispatch.right_hand_path;
		strcpy_s(action_info.actionName, "menu_toggle_simple");
		strcpy_s(action_info.localizedActionName, "Show or Hide Menu Simple");
		if (XR_FAILED(dispatch.CreateAction(dispatch.action_set, &action_info, &dispatch.simple_menu_toggle_action)))
			return false;

		action_info = { XR_TYPE_ACTION_CREATE_INFO };
		action_info.countSubactionPaths = 1;
		action_info.subactionPaths = &dispatch.right_hand_path;
		action_info.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
		strcpy_s(action_info.actionName, "menu_scroll");
		strcpy_s(action_info.localizedActionName, "Menu Scroll");
		if (XR_FAILED(dispatch.CreateAction(dispatch.action_set, &action_info, &dispatch.scroll_action)))
			return false;

		if (!string_to_path(dispatch, "/interaction_profiles/oculus/touch_controller", dispatch.oculus_profile) ||
			!string_to_path(dispatch, "/interaction_profiles/khr/simple_controller", dispatch.simple_profile) ||
			!string_to_path(dispatch, "/user/hand/right/input/aim/pose", dispatch.aim_path) ||
			!string_to_path(dispatch, "/user/hand/right/input/trigger/value", dispatch.trigger_path) ||
			!string_to_path(dispatch, "/user/hand/right/input/select/click", dispatch.select_path) ||
			!string_to_path(dispatch, "/user/hand/right/input/thumbstick/click", dispatch.right_thumbstick_click_path) ||
			!string_to_path(dispatch, "/user/hand/left/input/thumbstick/click", dispatch.left_thumbstick_click_path) ||
			!string_to_path(dispatch, "/user/hand/right/input/thumbstick", dispatch.thumbstick_path) ||
			!string_to_path(dispatch, "/user/hand/right/input/menu/click", dispatch.menu_click_path))
			return false;

		suggest_bindings(dispatch, dispatch.oculus_profile, {
			{ dispatch.aim_action, dispatch.aim_path },
			{ dispatch.select_action, dispatch.trigger_path },
			{ dispatch.right_menu_toggle_action, dispatch.right_thumbstick_click_path },
			{ dispatch.left_menu_toggle_action, dispatch.left_thumbstick_click_path },
			{ dispatch.scroll_action, dispatch.thumbstick_path },
		});
		suggest_bindings(dispatch, dispatch.simple_profile, {
			{ dispatch.aim_action, dispatch.aim_path },
			{ dispatch.select_action, dispatch.select_path },
			{ dispatch.simple_menu_toggle_action, dispatch.menu_click_path },
		});
		dispatch.actions_ready = true;
		return true;
	}

	void append_openxr_ui_bindings(const instance_dispatch &dispatch, XrPath profile, std::vector<XrActionSuggestedBinding> &bindings)
	{
		const auto append = [&bindings](XrAction action, XrPath binding) {
			const auto existing = std::find_if(bindings.begin(), bindings.end(), [action, binding](const XrActionSuggestedBinding &candidate) {
				return candidate.action == action && candidate.binding == binding;
			});
			if (existing == bindings.end())
				bindings.push_back({ action, binding });
		};
		if (profile == dispatch.oculus_profile)
		{
			append(dispatch.aim_action, dispatch.aim_path);
			append(dispatch.select_action, dispatch.trigger_path);
			append(dispatch.right_menu_toggle_action, dispatch.right_thumbstick_click_path);
			append(dispatch.left_menu_toggle_action, dispatch.left_thumbstick_click_path);
			append(dispatch.scroll_action, dispatch.thumbstick_path);
		}
		else if (profile == dispatch.simple_profile)
		{
			append(dispatch.aim_action, dispatch.aim_path);
			append(dispatch.select_action, dispatch.select_path);
			append(dispatch.simple_menu_toggle_action, dispatch.menu_click_path);
		}
	}

	bool initialize_quad(session_state &state, const XrSessionCreateInfo *create_info)
	{
		if (const XrGraphicsBindingD3D11KHR *const binding = find_d3d11_binding(create_info);
			binding != nullptr && binding->device != nullptr)
		{
			state.api = graphics_api::d3d11;
			state.device = binding->device;
			state.device->GetImmediateContext(&state.context);
			if (state.context == nullptr)
			{
				openxr_ui_log_message("native quad initialization failed: D3D11 immediate context is unavailable.", true);
				return false;
			}

			D3D11_QUERY_DESC query_desc = { D3D11_QUERY_EVENT, 0 };
			if (FAILED(state.device->CreateQuery(&query_desc, &state.completion_query)))
				return false;
		}
		else if (const XrGraphicsBindingD3D12KHR *const binding12 = find_d3d12_binding(create_info);
			binding12 != nullptr && binding12->device != nullptr && binding12->queue != nullptr)
		{
			state.api = graphics_api::d3d12;
			state.device12 = binding12->device;
			state.command_queue12 = binding12->queue;
		}
		else
		{
			openxr_ui_log_message("native quad skipped: the OpenXR session is not using D3D11 or D3D12.", true);
			return false;
		}

		const XrPosef identity = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };
		XrReferenceSpaceCreateInfo space_info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
		space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
		space_info.poseInReferenceSpace = identity;
		if (XR_FAILED(state.dispatch->CreateReferenceSpace(state.session, &space_info, &state.view_space)))
			return false;

		uint32_t format_count = 0;
		if (XR_FAILED(state.dispatch->EnumerateSwapchainFormats(state.session, 0, &format_count, nullptr)) || format_count == 0)
			return false;
		std::vector<int64_t> formats(format_count);
		if (XR_FAILED(state.dispatch->EnumerateSwapchainFormats(state.session, format_count, &format_count, formats.data())))
			return false;
		for (const int64_t format : formats)
			if (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || (state.quad_format == 0 && format == DXGI_FORMAT_R8G8B8A8_UNORM))
				state.quad_format = format;
		if (state.quad_format == 0)
		{
			openxr_ui_log_message("native quad initialization failed: no RGBA8 swapchain format is available.", true);
			return false;
		}

		XrSwapchainCreateInfo swapchain_info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
		swapchain_info.format = state.quad_format;
		swapchain_info.sampleCount = 1;
		swapchain_info.width = openxr_ui::panel_width;
		swapchain_info.height = openxr_ui::expanded_panel_height;
		swapchain_info.faceCount = 1;
		swapchain_info.arraySize = 1;
		swapchain_info.mipCount = 1;
		if (XR_FAILED(state.dispatch->CreateSwapchain(state.session, &swapchain_info, &state.quad_swapchain)))
			return false;

		uint32_t image_count = 0;
		if (XR_FAILED(state.dispatch->EnumerateSwapchainImages(state.quad_swapchain, 0, &image_count, nullptr)) || image_count == 0)
			return false;
		if (state.api == graphics_api::d3d11)
		{
			state.images.resize(image_count, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
			if (XR_FAILED(state.dispatch->EnumerateSwapchainImages(state.quad_swapchain, image_count, &image_count,
				reinterpret_cast<XrSwapchainImageBaseHeader *>(state.images.data()))))
				return false;

			state.render_target_views.reserve(image_count);
			for (const XrSwapchainImageD3D11KHR &image : state.images)
			{
				D3D11_RENDER_TARGET_VIEW_DESC view_desc = {};
				view_desc.Format = static_cast<DXGI_FORMAT>(state.quad_format);
				view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				ComPtr<ID3D11RenderTargetView> view;
				if (image.texture == nullptr || FAILED(state.device->CreateRenderTargetView(image.texture, &view_desc, &view)))
					return false;
				state.render_target_views.push_back(std::move(view));
			}
		}
		else
		{
			state.images12.resize(image_count, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
			if (XR_FAILED(state.dispatch->EnumerateSwapchainImages(state.quad_swapchain, image_count, &image_count,
				reinterpret_cast<XrSwapchainImageBaseHeader *>(state.images12.data()))))
				return false;

			D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
			heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heap_desc.NumDescriptors = image_count;
			if (FAILED(state.device12->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&state.render_target_heap12))))
				return false;
			const uint32_t descriptor_increment = state.device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			D3D12_CPU_DESCRIPTOR_HANDLE descriptor = state.render_target_heap12->GetCPUDescriptorHandleForHeapStart();
			state.render_target_views12.reserve(image_count);
			for (const XrSwapchainImageD3D12KHR &image : state.images12)
			{
				if (image.texture == nullptr)
					return false;
				D3D12_RENDER_TARGET_VIEW_DESC view_desc = {};
				view_desc.Format = static_cast<DXGI_FORMAT>(state.quad_format);
				view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
				state.device12->CreateRenderTargetView(image.texture, &view_desc, descriptor);
				state.render_target_views12.push_back(descriptor);
				descriptor.ptr += descriptor_increment;
			}
			if (FAILED(state.device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&state.command_allocator12))) ||
				FAILED(state.device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.command_allocator12.Get(), nullptr,
					IID_PPV_ARGS(&state.command_list12))) || FAILED(state.command_list12->Close()) ||
				FAILED(state.device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&state.completion_fence12))))
				return false;
			state.completion_event12 = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (state.completion_event12 == nullptr)
				return false;
		}
		state.renderer = std::make_unique<openxr_ui::panel_renderer>();
		const bool renderer_initialized = state.api == graphics_api::d3d11 ?
			state.renderer->initialize(state.device.Get(), state.context.Get(), openxr_ui::panel_width, openxr_ui::expanded_panel_height) :
			state.renderer->initialize(state.device12.Get(), state.command_queue12.Get(), image_count, state.quad_format,
				openxr_ui::panel_width, openxr_ui::expanded_panel_height);
		if (!renderer_initialized)
		{
			openxr_ui_log_message("native quad initialization failed: private ImGui renderer could not initialize.", true);
			return false;
		}
		state.quad_ready = true;
		if (state.dispatch->actions_ready)
		{
			const XrPosef action_identity = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };
			XrActionSpaceCreateInfo action_space_info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
			action_space_info.action = state.dispatch->aim_action;
			action_space_info.subactionPath = state.dispatch->right_hand_path;
			action_space_info.poseInActionSpace = action_identity;
			state.dispatch->CreateActionSpace(state.session, &action_space_info, &state.aim_space);
		}
		openxr_ui_log_message("native VIEW-space quad initialized with private ImGui technique renderer.");
		openxr_ui_log_message(state.api == graphics_api::d3d11 ?
			"native quad graphics backend: D3D11." : "native quad graphics backend: D3D12.");
		return true;
	}

	bool wait_for_d3d11_gpu(session_state &state)
	{
		state.context->End(state.completion_query.Get());
		state.context->Flush();
		const ULONGLONG deadline = GetTickCount64() + 1000;
		HRESULT result = S_FALSE;
		while ((result = state.context->GetData(state.completion_query.Get(), nullptr, 0, 0)) == S_FALSE)
		{
			if (GetTickCount64() >= deadline)
				return false;
			SwitchToThread();
		}
		return result == S_OK;
	}

	bool wait_for_d3d12_gpu(session_state &state)
	{
		const uint64_t completion_value = ++state.completion_value12;
		if (FAILED(state.command_queue12->Signal(state.completion_fence12.Get(), completion_value)))
			return false;
		if (state.completion_fence12->GetCompletedValue() >= completion_value)
			return true;
		if (FAILED(state.completion_fence12->SetEventOnCompletion(completion_value, state.completion_event12)))
			return false;
		return WaitForSingleObject(state.completion_event12, 5000) == WAIT_OBJECT_0;
	}

	bool render_quad(session_state &state)
	{
		if (state.quad_swapchain == XR_NULL_HANDLE)
			return false;

		uint32_t image_index = 0;
		XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
		if (XR_FAILED(state.dispatch->AcquireSwapchainImage(state.quad_swapchain, &acquire_info, &image_index)))
			return false;
		XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		wait_info.timeout = XR_INFINITE_DURATION;
		if (XR_FAILED(state.dispatch->WaitSwapchainImage(state.quad_swapchain, &wait_info)))
			return false;

		bool rendered = state.api == graphics_api::d3d11 ? image_index < state.render_target_views.size() :
			state.api == graphics_api::d3d12 && image_index < state.render_target_views12.size();
		if (rendered)
		{
			const std::shared_ptr<const openxr_ui::technique_snapshot> snapshot = openxr_ui_get_techniques();
			state.performance_mode = openxr_ui_get_performance_mode();
			state.shaders_temporarily_disabled = openxr_ui_are_shaders_temporarily_disabled();
			state.preset_dirty = openxr_ui_is_preset_dirty();
			const uint32_t panel_height = state.panel_expanded ? openxr_ui::expanded_panel_height : openxr_ui::compact_panel_height;
			const LONG pointer_x = std::clamp(state.pointer_x,
				pointer_margin, static_cast<LONG>(openxr_ui::panel_width) - pointer_margin - 1);
			const LONG pointer_y = std::clamp(state.pointer_y,
				pointer_margin, static_cast<LONG>(panel_height) - pointer_margin - 1);
			const openxr_ui::render_input input = {
				state.pointer_visible,
				static_cast<float>(pointer_x),
				static_cast<float>(pointer_y),
				state.select_clicked,
				state.panel_expanded,
				state.select_down,
				state.select_released,
				state.scroll_y,
				state.menu_long_press_enabled,
				state.ui_starts_open,
				state.performance_mode,
				state.shaders_temporarily_disabled,
				state.preset_dirty,
				openxr_ui_get_preset_name(),
				openxr_ui_has_effect_runtime(),
			};
			openxr_ui::render_result render_result;
			if (state.api == graphics_api::d3d11)
			{
				render_result = state.renderer->render(state.render_target_views[image_index].Get(), *snapshot, input);
			}
			else if (FAILED(state.command_allocator12->Reset()) ||
				FAILED(state.command_list12->Reset(state.command_allocator12.Get(), nullptr)))
			{
				rendered = false;
			}
			else
			{
				render_result = state.renderer->render(state.command_list12.Get(), state.render_target_views12[image_index], *snapshot, input);
				if (FAILED(state.command_list12->Close()))
					rendered = false;
				else
				{
					ID3D12CommandList *const command_lists[] = { state.command_list12.Get() };
					state.command_queue12->ExecuteCommandLists(1, command_lists);
				}
			}
			switch (render_result.action)
			{
			case openxr_ui::render_result::command::toggle_panel_size:
				state.panel_expanded = !state.panel_expanded;
				break;
			case openxr_ui::render_result::command::hide_panel:
				state.panel_visible = false;
				break;
			case openxr_ui::render_result::command::move_active_to_top:
				openxr_ui_move_active_to_top();
				break;
			case openxr_ui::render_result::command::move_technique:
				openxr_ui_move_technique(render_result.target_id, render_result.state);
				break;
			case openxr_ui::render_result::command::toggle_all_techniques:
				openxr_ui_toggle_all_techniques();
				break;
			case openxr_ui::render_result::command::save_preset:
				state.renderer->notify_save_result(openxr_ui_save_preset());
				break;
			case openxr_ui::render_result::command::previous_preset:
				openxr_ui_switch_preset(true);
				break;
			case openxr_ui::render_result::command::next_preset:
				openxr_ui_switch_preset(false);
				break;
			case openxr_ui::render_result::command::save_preset_as:
				state.renderer->notify_save_as_started(openxr_ui_save_preset_as(render_result.text));
				break;
			case openxr_ui::render_result::command::delete_preset:
				openxr_ui_delete_preset();
				break;
			case openxr_ui::render_result::command::toggle_technique:
				openxr_ui_toggle_technique(render_result.target_id);
				break;
			case openxr_ui::render_result::command::set_technique_hidden:
				openxr_ui_set_technique_hidden(render_result.target_id, render_result.state);
				break;
			case openxr_ui::render_result::command::set_technique_favourite:
				openxr_ui_set_technique_favourite(render_result.target_id, render_result.state);
				break;
			case openxr_ui::render_result::command::revert_technique:
				openxr_ui_revert_technique(render_result.target_id);
				break;
			case openxr_ui::render_result::command::restore_technique:
				openxr_ui_restore_technique(render_result.target_id);
				break;
			case openxr_ui::render_result::command::set_parameter:
				openxr_ui_set_parameter(render_result.target_id, render_result.component, render_result.value);
				break;
			case openxr_ui::render_result::command::reset_parameter:
				openxr_ui_reset_parameter(render_result.target_id);
				break;
			case openxr_ui::render_result::command::reset_all_parameters:
				openxr_ui_reset_all_parameters(render_result.target_id);
				break;
			case openxr_ui::render_result::command::toggle_long_press:
				state.menu_long_press_enabled = !state.menu_long_press_enabled;
				break;
			case openxr_ui::render_result::command::set_ui_starts_open:
				if (openxr_ui_set_ui_starts_open(render_result.state))
					state.ui_starts_open = render_result.state;
				break;
			case openxr_ui::render_result::command::set_performance_mode:
				if (openxr_ui_set_performance_mode(render_result.state))
					state.performance_mode = render_result.state;
				break;
			case openxr_ui::render_result::command::none:
				break;
			}
			if (rendered)
				rendered = state.api == graphics_api::d3d11 ? wait_for_d3d11_gpu(state) : wait_for_d3d12_gpu(state);
		}

		XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		return XR_SUCCEEDED(state.dispatch->ReleaseSwapchainImage(state.quad_swapchain, &release_info)) && rendered;
	}

	XrVector3f aim_direction(const XrQuaternionf &orientation)
	{
		return {
			-2.0f * (orientation.x * orientation.z + orientation.w * orientation.y),
			-2.0f * (orientation.y * orientation.z - orientation.w * orientation.x),
			-(1.0f - 2.0f * (orientation.x * orientation.x + orientation.y * orientation.y)),
		};
	}

	void update_interaction(session_state &state, XrTime display_time)
	{
		state.pointer_visible = false;
		state.select_clicked = false;
		state.select_released = false;
		state.scroll_y = 0.0f;
		if (!state.pointer_visible)
			state.pointer_filter_ready = false;
		if (!state.actions_attached || state.aim_space == XR_NULL_HANDLE)
			return;

		const XrActiveActionSet active_set = { state.dispatch->action_set, XR_NULL_PATH };
		XrActionsSyncInfo sync_info = { XR_TYPE_ACTIONS_SYNC_INFO };
		sync_info.countActiveActionSets = 1;
		sync_info.activeActionSets = &active_set;
		const XrResult sync_result = state.dispatch->SyncActions(state.session, &sync_info);
		if (XR_FAILED(sync_result) && sync_result != XR_SESSION_NOT_FOCUSED)
			return;

		XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
		get_info.subactionPath = state.dispatch->right_hand_path;
		get_info.action = state.dispatch->select_action;
		XrActionStateBoolean select_state = { XR_TYPE_ACTION_STATE_BOOLEAN };
		if (XR_FAILED(state.dispatch->GetActionStateBoolean(state.session, &get_info, &select_state)))
			return;
		if (select_state.isActive)
		{
			const bool pressed = select_state.currentState;
			state.select_clicked = pressed && !state.select_was_pressed;
			state.select_released = !pressed && state.select_was_pressed;
			state.select_down = pressed;
			state.select_was_pressed = pressed;
		}

		get_info.action = state.dispatch->right_menu_toggle_action;
		XrActionStateBoolean right_menu_state = { XR_TYPE_ACTION_STATE_BOOLEAN };
		const bool right_menu_available = XR_SUCCEEDED(state.dispatch->GetActionStateBoolean(state.session, &get_info, &right_menu_state)) && right_menu_state.isActive;
		get_info.action = state.dispatch->left_menu_toggle_action;
		get_info.subactionPath = state.dispatch->left_hand_path;
		XrActionStateBoolean left_menu_state = { XR_TYPE_ACTION_STATE_BOOLEAN };
		const bool left_menu_available = XR_SUCCEEDED(state.dispatch->GetActionStateBoolean(state.session, &get_info, &left_menu_state)) && left_menu_state.isActive;
		get_info.action = state.dispatch->simple_menu_toggle_action;
		get_info.subactionPath = state.dispatch->right_hand_path;
		XrActionStateBoolean simple_menu_state = { XR_TYPE_ACTION_STATE_BOOLEAN };
		const bool simple_menu_pressed = XR_SUCCEEDED(state.dispatch->GetActionStateBoolean(state.session, &get_info, &simple_menu_state)) &&
			simple_menu_state.isActive && simple_menu_state.currentState;
		const bool menu_chord_pressed = (right_menu_available && left_menu_available &&
			right_menu_state.currentState && left_menu_state.currentState) || simple_menu_pressed;
		if (menu_chord_pressed && !state.menu_chord_was_pressed)
		{
			state.menu_chord_started = display_time;
			state.menu_chord_triggered = false;
			if (!state.menu_long_press_enabled)
			{
				state.panel_visible = !state.panel_visible;
				state.menu_chord_triggered = true;
			}
		}
		else if (menu_chord_pressed && state.menu_long_press_enabled && !state.menu_chord_triggered &&
			display_time - state.menu_chord_started >= menu_long_press_duration)
		{
			state.panel_visible = !state.panel_visible;
			state.menu_chord_triggered = true;
		}
		state.menu_chord_was_pressed = menu_chord_pressed;

		get_info.action = state.dispatch->scroll_action;
		XrActionStateVector2f scroll_state = { XR_TYPE_ACTION_STATE_VECTOR2F };
		if (XR_SUCCEEDED(state.dispatch->GetActionStateVector2f(state.session, &get_info, &scroll_state)) && scroll_state.isActive)
			state.scroll_y = scroll_state.currentState.y;
		if (!state.panel_visible)
		{
			state.pointer_filter_ready = false;
			return;
		}

		get_info.subactionPath = state.dispatch->right_hand_path;
		get_info.action = state.dispatch->aim_action;
		XrActionStatePose pose_state = { XR_TYPE_ACTION_STATE_POSE };
		if (XR_FAILED(state.dispatch->GetActionStatePose(state.session, &get_info, &pose_state)) || !pose_state.isActive)
			return;

		XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
		if (XR_FAILED(state.dispatch->LocateSpace(state.aim_space, state.view_space, display_time, &location)) ||
			(location.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) !=
				(XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
			return;

		const XrVector3f direction = aim_direction(location.pose.orientation);
		if (direction.z < -0.0001f)
		{
			const float panel_world_height = state.panel_expanded ? expanded_panel_world_height : compact_panel_world_height;
			const uint32_t panel_height = state.panel_expanded ? openxr_ui::expanded_panel_height : openxr_ui::compact_panel_height;
			const float distance = (panel_z - location.pose.position.z) / direction.z;
			const float hit_x = location.pose.position.x + direction.x * distance;
			const float hit_y = location.pose.position.y + direction.y * distance;
			state.pointer_visible = distance > 0.0f && hit_x >= -panel_world_width * 0.5f && hit_x <= panel_world_width * 0.5f &&
				hit_y >= panel_y - panel_world_height * 0.5f && hit_y <= panel_y + panel_world_height * 0.5f;
			if (state.pointer_visible)
			{
				const float raw_pointer_x = (hit_x / panel_world_width + 0.5f) * openxr_ui::panel_width;
				const float raw_pointer_y = (0.5f - (hit_y - panel_y) / panel_world_height) * panel_height;
				if (!state.pointer_filter_ready)
				{
					state.filtered_pointer_x = raw_pointer_x;
					state.filtered_pointer_y = raw_pointer_y;
					state.pointer_filter_ready = true;
				}
				else
				{
					const float delta_x = raw_pointer_x - state.filtered_pointer_x;
					const float delta_y = raw_pointer_y - state.filtered_pointer_y;
					const float pointer_distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
					const float dead_zone = state.select_down ? 0.0f : pointer_hover_dead_zone;
					if (pointer_distance > dead_zone)
					{
						const float follow_rate = state.select_down ? pointer_drag_follow_rate : pointer_hover_follow_rate;
						const float movement = (pointer_distance - dead_zone) * follow_rate / pointer_distance;
						state.filtered_pointer_x += delta_x * movement;
						state.filtered_pointer_y += delta_y * movement;
					}
				}
				if (state.select_down)
				{
					state.pointer_x = static_cast<LONG>(std::lround(state.filtered_pointer_x));
					state.pointer_y = static_cast<LONG>(std::lround(state.filtered_pointer_y));
				}
				else
				{
					state.pointer_x = static_cast<LONG>(std::lround(state.filtered_pointer_x / pointer_hover_output_step) * pointer_hover_output_step);
					state.pointer_y = static_cast<LONG>(std::lround(state.filtered_pointer_y / pointer_hover_output_step) * pointer_hover_output_step);
				}
			}
			else
				state.pointer_filter_ready = false;
		}
	}

	void destroy_session_resources(session_state &state)
	{
		state.effects.reset();
		state.renderer.reset();
		state.render_target_views.clear();
		state.render_target_views12.clear();
		state.images.clear();
		state.images12.clear();
		if (state.completion_event12 != nullptr)
		{
			CloseHandle(state.completion_event12);
			state.completion_event12 = nullptr;
		}
		if (state.aim_space != XR_NULL_HANDLE)
			state.dispatch->DestroySpace(state.aim_space);
		if (state.quad_swapchain != XR_NULL_HANDLE)
			state.dispatch->DestroySwapchain(state.quad_swapchain);
		if (state.view_space != XR_NULL_HANDLE)
			state.dispatch->DestroySpace(state.view_space);
	}

	bool ensure_effect_fallback(session_state &state)
	{
		if (state.effects != nullptr)
			return true;
		if (state.effects_decided || state.api == graphics_api::none)
			return false;
		if (openxr_ui_has_vr_runtime())
		{
			state.effects_decided = true;
			return false;
		}
		if (++state.frames_without_vr_runtime < effect_fallback_wait_frames)
			return false;

		state.effects_decided = true;
		openxr_ui_log_message("official ReShade did not start a VR effect runtime for this session; starting the effect fallback.");
		state.effects = state.api == graphics_api::d3d11 ?
			std::make_unique<openxr_ui::effect_fallback>(state.device.Get(), state.context.Get()) :
			std::make_unique<openxr_ui::effect_fallback>(state.device12.Get(), state.command_queue12.Get());
		return true;
	}

	// Runtimes may read an image as soon as it is released, so the effects run in xrReleaseSwapchainImage.
	// Which images hold the eyes is only known from xrEndFrame, so the layout of the last frame is used.
	void record_effect_layout(session_state &state, const XrFrameEndInfo &frame_end_info)
	{
		if (!ensure_effect_fallback(state))
			return;

		for (const XrSwapchain previous : state.effect_swapchains)
			if (swapchain_state *const swapchain = find_swapchain(previous))
				swapchain->effect_regions.clear();
		state.effect_swapchains.clear();

		for (uint32_t layer_index = 0; layer_index < frame_end_info.layerCount; ++layer_index)
		{
			const XrCompositionLayerBaseHeader *const layer = frame_end_info.layers[layer_index];
			if (layer == nullptr || layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION)
				continue;

			const auto *const projection = reinterpret_cast<const XrCompositionLayerProjection *>(layer);
			for (uint32_t view_index = 0; view_index < projection->viewCount; ++view_index)
			{
				const XrSwapchainSubImage &sub_image = projection->views[view_index].subImage;
				swapchain_state *const swapchain = find_swapchain(sub_image.swapchain);
				// Effects on a static image would stack every frame.
				if (swapchain == nullptr || swapchain->static_image || swapchain->textures.empty() ||
					sub_image.imageRect.offset.x < 0 || sub_image.imageRect.offset.y < 0 ||
					sub_image.imageRect.extent.width <= 0 || sub_image.imageRect.extent.height <= 0)
					continue;
				if (swapchain->effect_regions.empty())
					state.effect_swapchains.push_back(sub_image.swapchain);
				swapchain->effect_regions.push_back({
					nullptr,
					sub_image.imageArrayIndex,
					sub_image.imageRect.offset.x,
					sub_image.imageRect.offset.y,
					static_cast<uint32_t>(sub_image.imageRect.extent.width),
					static_cast<uint32_t>(sub_image.imageRect.extent.height),
				});
			}
			// A runtime renders effects once per present, so only the first projection layer is processed.
			return;
		}
	}

	bool all_effect_images_released(const session_state &state)
	{
		for (const XrSwapchain handle : state.effect_swapchains)
			if (const swapchain_state *const swapchain = find_swapchain(handle); swapchain != nullptr && !swapchain->release_pending)
				return false;
		return true;
	}

	// Games with one image per eye release them one by one. Holding them until all are released lets the effects run
	// once per frame on one picture of fixed size; eyes that were not released keep their place in that picture.
	void flush_effect_releases(session_state &state)
	{
		if (state.pending_releases.empty())
			return;

		std::vector<openxr_ui::effect_view> views;
		for (const XrSwapchain handle : state.effect_swapchains)
		{
			const swapchain_state *const swapchain = find_swapchain(handle);
			if (swapchain == nullptr)
				continue;
			for (openxr_ui::effect_view view : swapchain->effect_regions)
			{
				view.texture = swapchain->release_pending ? swapchain->textures[swapchain->acquired.front()] : nullptr;
				views.push_back(view);
			}
		}
		if (state.effects != nullptr && !views.empty())
			state.effects->apply(views.data(), static_cast<uint32_t>(views.size()));

		const XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		for (const XrSwapchain handle : state.pending_releases)
		{
			swapchain_state *const swapchain = find_swapchain(handle);
			if (swapchain == nullptr || !swapchain->release_pending)
				continue;
			swapchain->release_pending = false;
			if (XR_SUCCEEDED(swapchain->dispatch->ReleaseSwapchainImage(handle, &release_info)))
				swapchain->acquired.pop_front();
		}
		state.pending_releases.clear();
	}

	void flush_effect_releases_for(const swapchain_state &swapchain)
	{
		if (!swapchain.release_pending)
			return;
		if (session_state *const state = find_session(swapchain.session))
			flush_effect_releases(*state);
	}
}

extern "C" XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance instance, const char *name, PFN_xrVoidFunction *function)
{
	if (name == nullptr || function == nullptr)
		return XR_ERROR_VALIDATION_FAILURE;
#define OPENXR_UI_HOOK(name_string, function_name) \
	if (std::strcmp(name, name_string) == 0) { *function = reinterpret_cast<PFN_xrVoidFunction>(function_name); return XR_SUCCESS; }
	OPENXR_UI_HOOK("xrDestroyInstance", xrDestroyInstance)
	OPENXR_UI_HOOK("xrCreateSession", xrCreateSession)
	OPENXR_UI_HOOK("xrDestroySession", xrDestroySession)
	OPENXR_UI_HOOK("xrCreateSwapchain", xrCreateSwapchain)
	OPENXR_UI_HOOK("xrDestroySwapchain", xrDestroySwapchain)
	OPENXR_UI_HOOK("xrAcquireSwapchainImage", xrAcquireSwapchainImage)
	OPENXR_UI_HOOK("xrReleaseSwapchainImage", xrReleaseSwapchainImage)
	OPENXR_UI_HOOK("xrSuggestInteractionProfileBindings", xrSuggestInteractionProfileBindings)
	OPENXR_UI_HOOK("xrAttachSessionActionSets", xrAttachSessionActionSets)
	OPENXR_UI_HOOK("xrWaitFrame", xrWaitFrame)
	OPENXR_UI_HOOK("xrEndFrame", xrEndFrame)
#undef OPENXR_UI_HOOK

	if (instance == XR_NULL_HANDLE)
		return XR_ERROR_HANDLE_INVALID;
	const std::shared_ptr<instance_dispatch> dispatch = find_instance(instance);
	return dispatch != nullptr ? dispatch->GetInstanceProcAddr(instance, name, function) : XR_ERROR_HANDLE_INVALID;
}

extern "C" XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance)
{
	std::shared_ptr<instance_dispatch> dispatch;
	{
		const std::lock_guard<std::mutex> lock(s_layer_mutex);
		const auto instance_it = s_instances.find(instance);
		if (instance_it == s_instances.end())
			return XR_ERROR_HANDLE_INVALID;
		dispatch = instance_it->second;
		s_instances.erase(instance_it);
	}
	if (dispatch->action_set != XR_NULL_HANDLE)
		dispatch->DestroyActionSet(dispatch->action_set);
	return dispatch->DestroyInstance(instance);
}

extern "C" XrResult XRAPI_CALL xrCreateSession(XrInstance instance, const XrSessionCreateInfo *create_info, XrSession *session)
{
	const std::shared_ptr<instance_dispatch> dispatch = find_instance(instance);
	if (dispatch == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	const XrResult result = dispatch->CreateSession(instance, create_info, session);
	if (XR_FAILED(result))
		return result;

	auto state = std::make_unique<session_state>();
	state->instance = instance;
	state->session = *session;
	state->dispatch = dispatch;
	state->ui_starts_open = openxr_ui_get_ui_starts_open();
	state->performance_mode = openxr_ui_get_performance_mode();
	state->panel_visible = state->ui_starts_open;
	if (state->dispatch->actions_ready)
		initialize_quad(*state, create_info);
	{
		const std::lock_guard<std::mutex> lock(s_layer_mutex);
		s_sessions.emplace(*session, std::move(state));
	}
	return result;
}

extern "C" XrResult XRAPI_CALL xrDestroySession(XrSession session)
{
	std::unique_ptr<session_state> state;
	{
		const std::lock_guard<std::mutex> lock(s_layer_mutex);
		const auto session_it = s_sessions.find(session);
		if (session_it == s_sessions.end())
			return XR_ERROR_HANDLE_INVALID;
		state = std::move(session_it->second);
		s_sessions.erase(session_it);
		// Destroying a session also destroys its swapchains.
		for (auto swapchain_it = s_swapchains.begin(); swapchain_it != s_swapchains.end();)
			swapchain_it = swapchain_it->second.session == session ? s_swapchains.erase(swapchain_it) : std::next(swapchain_it);
	}
	destroy_session_resources(*state);
	return state->dispatch->DestroySession(session);
}

extern "C" XrResult XRAPI_CALL xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo *create_info, XrSwapchain *swapchain)
{
	session_state *const state = find_session(session);
	if (state == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	if (create_info == nullptr || swapchain == nullptr)
		return state->dispatch->CreateSwapchain(session, create_info, swapchain);

	XrSwapchainCreateInfo effect_info = *create_info;
	// The effect fallback copies color images to and from its own texture, as official ReShade does.
	if (state->api != graphics_api::none && (effect_info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) != 0)
		effect_info.usageFlags |= XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	const XrResult result = state->dispatch->CreateSwapchain(session, &effect_info, swapchain);
	if (XR_FAILED(result))
		return result;

	swapchain_state tracked;
	tracked.session = session;
	tracked.dispatch = state->dispatch;
	tracked.static_image = (create_info->createFlags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) != 0;
	uint32_t image_count = 0;
	if (state->api != graphics_api::none &&
		XR_SUCCEEDED(state->dispatch->EnumerateSwapchainImages(*swapchain, 0, &image_count, nullptr)) && image_count != 0)
	{
		if (state->api == graphics_api::d3d11)
		{
			std::vector<XrSwapchainImageD3D11KHR> images(image_count, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
			if (XR_SUCCEEDED(state->dispatch->EnumerateSwapchainImages(*swapchain, image_count, &image_count,
				reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data()))))
				for (const XrSwapchainImageD3D11KHR &image : images)
					tracked.textures.push_back(image.texture);
		}
		else
		{
			std::vector<XrSwapchainImageD3D12KHR> images(image_count, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
			if (XR_SUCCEEDED(state->dispatch->EnumerateSwapchainImages(*swapchain, image_count, &image_count,
				reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data()))))
				for (const XrSwapchainImageD3D12KHR &image : images)
					tracked.textures.push_back(image.texture);
		}
	}
	{
		const std::lock_guard<std::mutex> lock(s_layer_mutex);
		s_swapchains.insert_or_assign(*swapchain, std::move(tracked));
	}
	return result;
}

extern "C" XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain swapchain)
{
	if (const swapchain_state *const tracked = find_swapchain(swapchain))
		flush_effect_releases_for(*tracked);
	std::shared_ptr<instance_dispatch> dispatch;
	{
		const std::lock_guard<std::mutex> lock(s_layer_mutex);
		const auto swapchain_it = s_swapchains.find(swapchain);
		if (swapchain_it == s_swapchains.end())
			return XR_ERROR_HANDLE_INVALID;
		dispatch = swapchain_it->second.dispatch;
		s_swapchains.erase(swapchain_it);
	}
	return dispatch->DestroySwapchain(swapchain);
}

extern "C" XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo *acquire_info, uint32_t *index)
{
	swapchain_state *const tracked = find_swapchain(swapchain);
	if (tracked == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	flush_effect_releases_for(*tracked);
	const XrResult result = tracked->dispatch->AcquireSwapchainImage(swapchain, acquire_info, index);
	if (XR_SUCCEEDED(result) && index != nullptr)
		tracked->acquired.push_back(*index);
	return result;
}

extern "C" XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo *release_info)
{
	swapchain_state *const tracked = find_swapchain(swapchain);
	if (tracked == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	flush_effect_releases_for(*tracked);
	// Release returns the oldest acquired image, in acquire order.
	if (!tracked->effect_regions.empty() && !tracked->acquired.empty() && tracked->acquired.front() < tracked->textures.size())
	{
		session_state *const state = find_session(tracked->session);
		if (state != nullptr && state->effects != nullptr)
		{
			tracked->release_pending = true;
			state->pending_releases.push_back(swapchain);
			if (all_effect_images_released(*state))
				flush_effect_releases(*state);
			return XR_SUCCESS;
		}
	}
	const XrResult result = tracked->dispatch->ReleaseSwapchainImage(swapchain, release_info);
	if (XR_SUCCEEDED(result) && !tracked->acquired.empty())
		tracked->acquired.pop_front();
	return result;
}

extern "C" XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance instance, const XrInteractionProfileSuggestedBinding *suggested_bindings)
{
	const std::shared_ptr<instance_dispatch> dispatch = find_instance(instance);
	if (dispatch == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	if (suggested_bindings == nullptr || !dispatch->actions_ready)
		return dispatch->SuggestInteractionProfileBindings(instance, suggested_bindings);

	std::vector<XrActionSuggestedBinding> bindings;
	bindings.reserve(suggested_bindings->countSuggestedBindings + 2);
	for (uint32_t index = 0; index < suggested_bindings->countSuggestedBindings; ++index)
		bindings.push_back(suggested_bindings->suggestedBindings[index]);
	append_openxr_ui_bindings(*dispatch, suggested_bindings->interactionProfile, bindings);
	XrInteractionProfileSuggestedBinding combined_bindings = *suggested_bindings;
	combined_bindings.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
	combined_bindings.suggestedBindings = bindings.data();
	return dispatch->SuggestInteractionProfileBindings(instance, &combined_bindings);
}

extern "C" XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo *attach_info)
{
	session_state *const state = find_session(session);
	if (state == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	if (attach_info == nullptr || !state->dispatch->actions_ready)
		return state->dispatch->AttachSessionActionSets(session, attach_info);

	std::vector<XrActionSet> action_sets(attach_info->actionSets, attach_info->actionSets + attach_info->countActionSets);
	if (std::find(action_sets.begin(), action_sets.end(), state->dispatch->action_set) == action_sets.end())
		action_sets.push_back(state->dispatch->action_set);
	XrSessionActionSetsAttachInfo combined_info = *attach_info;
	combined_info.countActionSets = static_cast<uint32_t>(action_sets.size());
	combined_info.actionSets = action_sets.data();
	const XrResult result = state->dispatch->AttachSessionActionSets(session, &combined_info);
	state->actions_attached = XR_SUCCEEDED(result);
	if (state->actions_attached)
		openxr_ui_log_message("right-controller aim and select actions attached.");
	return result;
}

extern "C" XrResult XRAPI_CALL xrWaitFrame(XrSession session, const XrFrameWaitInfo *wait_info, XrFrameState *frame_state)
{
	session_state *const state = find_session(session);
	if (state == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	const XrResult result = state->dispatch->WaitFrame(session, wait_info, frame_state);
	if (XR_SUCCEEDED(result) && frame_state != nullptr)
		state->should_render = frame_state->shouldRender == XR_TRUE;
	return result;
}

extern "C" XrResult XRAPI_CALL xrEndFrame(XrSession session, const XrFrameEndInfo *frame_end_info)
{
	session_state *const state = find_session(session);
	if (state == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	const int preset_operation_result = openxr_ui_update_preset_operations();
	if (preset_operation_result != 0 && state->renderer != nullptr)
		state->renderer->notify_save_as_result(preset_operation_result > 0);
	// The runtime requires every image of the frame to be released before it is submitted.
	flush_effect_releases(*state);
	if (frame_end_info != nullptr && state->should_render)
		record_effect_layout(*state, *frame_end_info);
	if (frame_end_info == nullptr || !state->quad_ready || !state->should_render)
		return state->dispatch->EndFrame(session, frame_end_info);

	update_interaction(*state, frame_end_info->displayTime);
	if (!state->panel_visible)
		return state->dispatch->EndFrame(session, frame_end_info);
	if (!render_quad(*state))
		return state->dispatch->EndFrame(session, frame_end_info);

	const XrPosef panel_pose = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, panel_y, panel_z } };
	XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	quad.space = state->view_space;
	quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	quad.subImage.swapchain = state->quad_swapchain;
	const uint32_t panel_height = state->panel_expanded ? openxr_ui::expanded_panel_height : openxr_ui::compact_panel_height;
	const float panel_world_height = state->panel_expanded ? expanded_panel_world_height : compact_panel_world_height;
	quad.subImage.imageRect = { { 0, 0 }, { static_cast<int32_t>(openxr_ui::panel_width), static_cast<int32_t>(panel_height) } };
	quad.pose = panel_pose;
	quad.size = { panel_world_width, panel_world_height };

	std::vector<const XrCompositionLayerBaseHeader *> layers;
	layers.reserve(frame_end_info->layerCount + 1);
	for (uint32_t index = 0; index < frame_end_info->layerCount; ++index)
		layers.push_back(frame_end_info->layers[index]);
	layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad));
	XrFrameEndInfo combined_info = *frame_end_info;
	combined_info.layerCount = static_cast<uint32_t>(layers.size());
	combined_info.layers = layers.data();

	if (!state->quad_logged)
	{
		openxr_ui_log_message("submitting native XrCompositionLayerQuad in VIEW space.");
		state->quad_logged = true;
	}
	return state->dispatch->EndFrame(session, &combined_info);
}

extern "C" XrResult XRAPI_CALL xrCreateApiLayerInstance(const XrInstanceCreateInfo *create_info, const XrApiLayerCreateInfo *layer_info, XrInstance *instance)
{
	if (create_info == nullptr || layer_info == nullptr || instance == nullptr ||
		layer_info->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO ||
		layer_info->structVersion != XR_API_LAYER_CREATE_INFO_STRUCT_VERSION ||
		layer_info->structSize != sizeof(XrApiLayerCreateInfo) ||
		layer_info->nextInfo == nullptr ||
		layer_info->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
		layer_info->nextInfo->structVersion != XR_API_LAYER_NEXT_INFO_STRUCT_VERSION ||
		layer_info->nextInfo->structSize != sizeof(XrApiLayerNextInfo) ||
		layer_info->nextInfo->nextCreateApiLayerInstance == nullptr ||
		layer_info->nextInfo->nextGetInstanceProcAddr == nullptr)
		return XR_ERROR_INITIALIZATION_FAILED;

	const PFN_xrGetInstanceProcAddr next_get_instance_proc = layer_info->nextInfo->nextGetInstanceProcAddr;
	XrApiLayerCreateInfo next_layer_info = *layer_info;
	next_layer_info.nextInfo = layer_info->nextInfo->next;
	const XrResult result = layer_info->nextInfo->nextCreateApiLayerInstance(create_info, &next_layer_info, instance);
	if (XR_FAILED(result))
		return result;

	auto dispatch = std::make_shared<instance_dispatch>();
	if (!load_dispatch(next_get_instance_proc, *instance, *dispatch))
	{
		dispatch->DestroyInstance(*instance);
		return XR_ERROR_INITIALIZATION_FAILED;
	}
	if (openxr_ui_ensure_addon_registered())
		initialize_actions(*dispatch, *instance);
	{
		const std::lock_guard<std::mutex> lock(s_layer_mutex);
		s_instances.emplace(*instance, std::move(dispatch));
	}
	openxr_ui_log_message("dual-role OpenXR API-layer boundary initialized.");
	return XR_SUCCESS;
}

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo *loader_info, const char *, XrNegotiateApiLayerRequest *layer_request)
{
	if (loader_info == nullptr || layer_request == nullptr ||
		loader_info->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
		loader_info->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
		loader_info->structSize != sizeof(XrNegotiateLoaderInfo) ||
		layer_request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
		layer_request->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
		layer_request->structSize != sizeof(XrNegotiateApiLayerRequest) ||
		loader_info->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
		loader_info->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
		loader_info->minApiVersion > XR_CURRENT_API_VERSION ||
		loader_info->maxApiVersion < XR_CURRENT_API_VERSION)
		return XR_ERROR_INITIALIZATION_FAILED;

	layer_request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
	layer_request->layerApiVersion = XR_CURRENT_API_VERSION;
	layer_request->getInstanceProcAddr = xrGetInstanceProcAddr;
	layer_request->createApiLayerInstance = xrCreateApiLayerInstance;
	return XR_SUCCESS;
}