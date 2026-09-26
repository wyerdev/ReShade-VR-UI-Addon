#include "openxr_ui_renderer.hpp"

#include <d3d11.h>
#include <d3d12.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_dx12.h>

#include "vr_frame_graph.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

namespace
{
	std::mutex s_imgui_mutex;

	constexpr size_t compact_techniques_per_page = 10;
	constexpr size_t compact_visible_parameters = 5;
	constexpr float outer_padding = 24.0f;
	constexpr float main_footer_height = 149.0f;
	constexpr float detail_footer_height = 83.0f;
	constexpr float row_spacing = 3.0f;
	constexpr uint32_t drag_release_frames = 3;
	constexpr float joystick_scroll_dead_zone = 0.5f;
	constexpr uint32_t joystick_scroll_repeat_frames = 8;
	constexpr uint32_t save_feedback_frames = 180;
	constexpr ImU32 default_value_color = IM_COL32(155, 210, 218, 255);
	constexpr ImU32 changed_value_color = IM_COL32(255, 205, 72, 255);

	void apply_style()
	{
		ImGuiStyle &style = ImGui::GetStyle();
		style.WindowPadding = { outer_padding, 18.0f };
		style.WindowRounding = 0.0f;
		style.WindowBorderSize = 0.0f;
		style.FrameRounding = 3.0f;
		style.FrameBorderSize = 0.0f;
		style.ItemSpacing = { 8.0f, row_spacing };
		style.Colors[ImGuiCol_WindowBg] = { 0.018f, 0.028f, 0.040f, 0.96f };
		style.Colors[ImGuiCol_Text] = { 0.84f, 0.94f, 0.96f, 1.0f };
		style.Colors[ImGuiCol_TextDisabled] = { 0.42f, 0.50f, 0.52f, 1.0f };
	}

	void draw_crosshair(ImDrawList *draw_list, const ImVec2 position)
	{
		const ImU32 shadow = IM_COL32(0, 0, 0, 255);
		const ImU32 foreground = IM_COL32(235, 250, 255, 255);
		const ImU32 accent = IM_COL32(0, 200, 225, 255);
		draw_list->AddLine({ position.x - 11.0f, position.y }, { position.x + 11.0f, position.y }, shadow, 7.0f);
		draw_list->AddLine({ position.x, position.y - 11.0f }, { position.x, position.y + 11.0f }, shadow, 7.0f);
		draw_list->AddLine({ position.x - 9.0f, position.y }, { position.x + 9.0f, position.y }, foreground, 3.0f);
		draw_list->AddLine({ position.x, position.y - 9.0f }, { position.x, position.y + 9.0f }, foreground, 3.0f);
		draw_list->AddCircleFilled(position, 3.0f, accent);
	}

	bool draw_text_button(const char *id, const char *label, const ImVec2 size, bool enabled = true, ImU32 background = 0)
	{
		ImGui::BeginDisabled(!enabled);
		ImGui::InvisibleButton(id, size);
		const bool hovered = enabled && ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		draw_list->AddRectFilled(minimum, maximum,
			background != 0 ? background : hovered ? IM_COL32(28, 82, 90, 255) : IM_COL32(18, 48, 55, 255), 3.0f);
		const ImVec2 text_size = ImGui::CalcTextSize(label);
		draw_list->AddText({ minimum.x + (size.x - text_size.x) * 0.5f, minimum.y + (size.y - text_size.y) * 0.5f },
			enabled ? IM_COL32(222, 242, 245, 255) : IM_COL32(78, 90, 92, 255), label);
		ImGui::EndDisabled();
		return hovered;
	}

	bool draw_star_button(const char *id, const ImVec2 size, bool active)
	{
		ImGui::InvisibleButton(id, size);
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		draw_list->AddRectFilled(minimum, maximum, hovered ? IM_COL32(28, 82, 90, 255) : IM_COL32(18, 48, 55, 255), 3.0f);

		const ImVec2 center = { (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f };
		ImVec2 points[10];
		for (size_t index = 0; index < 10; ++index)
		{
			const float angle = -1.5707963268f + static_cast<float>(index) * 0.6283185307f;
			const float radius = index % 2 == 0 ? 10.0f : 4.5f;
			points[index] = { center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius };
		}
		const ImU32 color = active ? IM_COL32(255, 205, 72, 255) : IM_COL32(170, 190, 194, 255);
		if (active)
			for (size_t index = 0; index < 10; ++index)
				draw_list->AddTriangleFilled(center, points[index], points[(index + 1) % 10], color);
		else
			draw_list->AddPolyline(points, 10, color, ImDrawFlags_Closed, 2.0f);
		return hovered;
	}

	bool draw_tick_box(const char *id, const char *label, bool checked, float width = 276.0f)
	{
		ImGui::InvisibleButton(id, { width, 34.0f });
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const ImVec2 box_min = { minimum.x + 7.0f, minimum.y + 7.0f };
		const ImVec2 box_max = { box_min.x + 20.0f, box_min.y + 20.0f };
		draw_list->AddRectFilled(box_min, box_max, checked ? IM_COL32(26, 196, 100, 255) :
			hovered ? IM_COL32(92, 105, 110, 255) : IM_COL32(72, 82, 86, 255), 3.0f);
		if (checked)
		{
			draw_list->AddLine({ box_min.x + 4.0f, box_min.y + 10.0f }, { box_min.x + 8.0f, box_min.y + 14.0f }, IM_COL32_WHITE, 2.0f);
			draw_list->AddLine({ box_min.x + 8.0f, box_min.y + 14.0f }, { box_min.x + 17.0f, box_min.y + 5.0f }, IM_COL32_WHITE, 2.0f);
		}
		draw_list->AddText({ minimum.x + 36.0f, minimum.y + (34.0f - ImGui::GetFontSize()) * 0.5f },
			IM_COL32(222, 242, 245, 255), label);
		return hovered;
	}

	std::string fit_text(const char *text, float maximum_width)
	{
		if (ImGui::CalcTextSize(text).x <= maximum_width)
			return text;

		std::string fitted = text;
		while (!fitted.empty() && ImGui::CalcTextSize((fitted + "...").c_str()).x > maximum_width)
		{
			fitted.pop_back();
			while (!fitted.empty() && (static_cast<unsigned char>(fitted.back()) & 0xC0) == 0x80)
				fitted.pop_back();
		}
		return fitted + "...";
	}

	void draw_hover_details(const ImVec2 minimum, const ImVec2 maximum, const char *label, const char *tooltip = nullptr)
	{
		if (!ImGui::IsMouseHoveringRect(minimum, maximum) || ((tooltip == nullptr || *tooltip == '\0') && ImGui::CalcTextSize(label).x <= maximum.x - minimum.x))
			return;

		ImGui::SetNextWindowSizeConstraints({ 280.0f, 0.0f }, { (std::min)(700.0f, ImGui::GetIO().DisplaySize.x - outer_padding * 2.0f), ImGui::GetIO().DisplaySize.y - outer_padding * 2.0f });
		if (ImGui::BeginTooltip())
		{
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (std::min)(640.0f, ImGui::GetIO().DisplaySize.x - outer_padding * 4.0f));
			ImGui::TextUnformatted(label);
			if (tooltip != nullptr && *tooltip != '\0')
			{
				ImGui::Separator();
				ImGui::TextUnformatted(tooltip);
			}
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	struct slider_interaction
	{
		bool hovered = false;
		float fraction = 0.0f;
	};

	slider_interaction draw_slider(const char *id, const ImVec2 size, float fraction, bool enabled = true)
	{
		fraction = std::clamp(fraction, 0.0f, 1.0f);
		ImGui::BeginDisabled(!enabled);
		ImGui::InvisibleButton(id, size);
		const bool hovered = enabled && ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const float center_y = (minimum.y + maximum.y) * 0.5f;
		const float track_min_x = minimum.x + 8.0f;
		const float track_max_x = maximum.x - 8.0f;
		draw_list->AddRectFilled({ track_min_x, center_y - 3.0f }, { track_max_x, center_y + 3.0f }, IM_COL32(30, 55, 61, 255), 3.0f);
		draw_list->AddRectFilled({ track_min_x, center_y - 3.0f }, { track_min_x + (track_max_x - track_min_x) * fraction, center_y + 3.0f },
			enabled ? IM_COL32(0, 174, 197, 255) : IM_COL32(62, 73, 76, 255), 3.0f);
		draw_list->AddCircleFilled({ track_min_x + (track_max_x - track_min_x) * fraction, center_y }, 7.0f,
			hovered ? IM_COL32(235, 250, 255, 255) : IM_COL32(170, 220, 226, 255));
		ImGui::EndDisabled();
		return { hovered, std::clamp((ImGui::GetIO().MousePos.x - track_min_x) / (track_max_x - track_min_x), 0.0f, 1.0f) };
	}

	slider_interaction draw_vertical_slider(const char *id, const ImVec2 size, float fraction, bool enabled = true)
	{
		fraction = std::clamp(fraction, 0.0f, 1.0f);
		ImGui::BeginDisabled(!enabled);
		ImGui::InvisibleButton(id, size);
		const bool hovered = enabled && ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const float center_x = (minimum.x + maximum.x) * 0.5f;
		const float track_min_y = minimum.y + 8.0f;
		const float track_max_y = maximum.y - 8.0f;
		draw_list->AddRectFilled({ center_x - 4.0f, track_min_y }, { center_x + 4.0f, track_max_y }, IM_COL32(30, 55, 61, 255), 4.0f);
		draw_list->AddCircleFilled({ center_x, track_min_y + (track_max_y - track_min_y) * fraction }, 10.0f,
			hovered ? IM_COL32(235, 250, 255, 255) : IM_COL32(0, 174, 197, 255));
		ImGui::EndDisabled();
		return { hovered, std::clamp((ImGui::GetIO().MousePos.y - track_min_y) / (track_max_y - track_min_y), 0.0f, 1.0f) };
	}
}

namespace openxr_ui
{
	struct panel_renderer::implementation
	{
		enum class graphics_api
		{
			none,
			d3d11,
			d3d12,
		};

		enum class drag_target
		{
			none,
			technique_scroll,
			value_slider,
			parameter_scroll,
		};

		ImGuiContext *context = nullptr;
		graphics_api api = graphics_api::none;
		ID3D11DeviceContext *device_context = nullptr;
		ID3D12DescriptorHeap *srv_descriptor_heap = nullptr;
		uint32_t srv_descriptor_increment = 0;
		std::vector<bool> srv_descriptors_in_use;
		uint32_t width = 0;
		uint32_t height = 0;
		size_t technique_scroll = 0;
		size_t parameter_scroll = 0;
		uint64_t selected_technique = 0;
		bool show_hidden = false;
		bool show_favourites_only = false;
		drag_target dragging = drag_target::none;
		uint64_t dragged_parameter = 0;
		uint32_t dragged_component = 0;
		float drag_start_pointer = 0.0f;
		float drag_start_fraction = 0.0f;
		uint32_t release_frame_count = 0;
		int joystick_scroll_direction = 0;
		uint32_t joystick_scroll_frame_count = 0;
		int save_feedback = 0;
		uint32_t save_feedback_frame_count = 0;
		int save_as_feedback = 0;
		uint32_t save_as_feedback_frame_count = 0;
		bool save_as_dialog_open = false;
		bool save_as_uppercase = true;
		std::string save_as_name;
	};

	panel_renderer::panel_renderer() :
		_implementation(std::make_unique<implementation>())
	{
	}

	panel_renderer::~panel_renderer()
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr)
			return;
		ImGui::SetCurrentContext(_implementation->context);
		if (_implementation->api == implementation::graphics_api::d3d11)
			ImGui_ImplDX11_Shutdown();
		else if (_implementation->api == implementation::graphics_api::d3d12)
			ImGui_ImplDX12_Shutdown();
		ImGui::DestroyContext(_implementation->context);
		if (_implementation->device_context != nullptr)
			_implementation->device_context->Release();
		if (_implementation->srv_descriptor_heap != nullptr)
			_implementation->srv_descriptor_heap->Release();
	}

	bool panel_renderer::initialize_context(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0 || _implementation->context != nullptr)
			return false;

		_implementation->context = ImGui::CreateContext();
		ImGui::SetCurrentContext(_implementation->context);
		ImGuiIO &io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		io.DisplaySize = { static_cast<float>(width), static_cast<float>(height) };
		ImFontConfig font_config;
		font_config.SizePixels = 22.0f;
		io.Fonts->Clear();
		io.Fonts->AddFontDefault(&font_config);
		apply_style();

		_implementation->width = width;
		_implementation->height = height;
		return true;
	}

	bool panel_renderer::initialize(ID3D11Device *device, ID3D11DeviceContext *context, uint32_t width, uint32_t height)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (device == nullptr || context == nullptr || !initialize_context(width, height))
			return false;

		if (!ImGui_ImplDX11_Init(device, context))
		{
			ImGui::DestroyContext(_implementation->context);
			_implementation->context = nullptr;
			return false;
		}

		_implementation->api = implementation::graphics_api::d3d11;
		_implementation->device_context = context;
		_implementation->device_context->AddRef();
		return true;
	}

	bool panel_renderer::initialize(ID3D12Device *device, ID3D12CommandQueue *queue, uint32_t frame_count, int64_t format, uint32_t width, uint32_t height)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (device == nullptr || queue == nullptr || frame_count == 0 || !initialize_context(width, height))
			return false;

		D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap_desc.NumDescriptors = 16;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&_implementation->srv_descriptor_heap))))
		{
			ImGui::DestroyContext(_implementation->context);
			_implementation->context = nullptr;
			return false;
		}
		_implementation->srv_descriptor_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		_implementation->srv_descriptors_in_use.resize(heap_desc.NumDescriptors, false);

		ImGui_ImplDX12_InitInfo init_info = {};
		init_info.Device = device;
		init_info.CommandQueue = queue;
		init_info.NumFramesInFlight = static_cast<int>(frame_count);
		init_info.RTVFormat = static_cast<DXGI_FORMAT>(format);
		init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
		init_info.UserData = _implementation.get();
		init_info.SrvDescriptorHeap = _implementation->srv_descriptor_heap;
		init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE *cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE *gpu_handle) {
			auto &renderer = *static_cast<implementation *>(info->UserData);
			for (size_t index = 0; index < renderer.srv_descriptors_in_use.size(); ++index)
			{
				if (renderer.srv_descriptors_in_use[index])
					continue;
				renderer.srv_descriptors_in_use[index] = true;
				*cpu_handle = renderer.srv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
				*gpu_handle = renderer.srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart();
				cpu_handle->ptr += index * renderer.srv_descriptor_increment;
				gpu_handle->ptr += index * renderer.srv_descriptor_increment;
				return;
			}
			*cpu_handle = {};
			*gpu_handle = {};
		};
		init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE) {
			auto &renderer = *static_cast<implementation *>(info->UserData);
			const D3D12_CPU_DESCRIPTOR_HANDLE start = renderer.srv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
			const size_t index = (cpu_handle.ptr - start.ptr) / renderer.srv_descriptor_increment;
			if (index < renderer.srv_descriptors_in_use.size())
				renderer.srv_descriptors_in_use[index] = false;
		};
		if (!ImGui_ImplDX12_Init(&init_info))
		{
			_implementation->srv_descriptor_heap->Release();
			_implementation->srv_descriptor_heap = nullptr;
			ImGui::DestroyContext(_implementation->context);
			_implementation->context = nullptr;
			return false;
		}

		_implementation->api = implementation::graphics_api::d3d12;
		return true;
	}

	void panel_renderer::notify_save_result(bool succeeded)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		_implementation->save_feedback = succeeded ? 1 : -1;
		_implementation->save_feedback_frame_count = save_feedback_frames;
	}

	void panel_renderer::notify_save_as_started(bool started)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		_implementation->save_as_feedback = started ? 2 : -1;
		_implementation->save_as_feedback_frame_count = started ? 0 : save_feedback_frames;
		if (started)
		{
			_implementation->save_as_dialog_open = false;
			_implementation->save_as_name.clear();
		}
	}

	void panel_renderer::notify_save_as_result(bool succeeded)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		_implementation->save_as_feedback = succeeded ? 1 : -1;
		_implementation->save_as_feedback_frame_count = save_feedback_frames;
	}

	render_result panel_renderer::render_ui(const technique_snapshot &snapshot, const render_input &input)
	{
		render_result result;
		const char *const graphics_api_label = _implementation->api == implementation::graphics_api::d3d11 ? "DX11" :
			_implementation->api == implementation::graphics_api::d3d12 ? "DX12" : "Unknown";
		ImGuiIO &io = ImGui::GetIO();
		const float display_height = input.expanded ? static_cast<float>(expanded_panel_height) : static_cast<float>(compact_panel_height);
		io.DisplaySize = { static_cast<float>(_implementation->width), display_height };
		io.DeltaTime = 1.0f / 90.0f;
		io.AddMousePosEvent(input.pointer_visible ? input.pointer_x : -FLT_MAX, input.pointer_visible ? input.pointer_y : -FLT_MAX);
		if (_implementation->dragging != implementation::drag_target::none)
		{
			if (input.select_down)
				_implementation->release_frame_count = 0;
			else if (input.select_released || _implementation->release_frame_count != 0)
			{
				if (++_implementation->release_frame_count >= drag_release_frames)
				{
					_implementation->dragging = implementation::drag_target::none;
					_implementation->release_frame_count = 0;
				}
			}
		}

		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 0.0f, 0.0f });
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("ReShade VR UI Add-on", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

		const auto selected_it = std::find_if(snapshot.techniques.begin(), snapshot.techniques.end(), [this](const technique &entry) {
			return entry.id == _implementation->selected_technique;
		});
		if (_implementation->selected_technique != 0 && selected_it == snapshot.techniques.end())
			_implementation->selected_technique = 0;

		if (_implementation->save_as_dialog_open)
		{
			ImGui::TextUnformatted("Save preset as");
			ImGui::SameLine();
			ImGui::TextDisabled("Graphics: %s", graphics_api_label);
			ImGui::SetCursorScreenPos({ io.DisplaySize.x - outer_padding - 34.0f, 18.0f });
			const bool close_dialog_hovered = draw_text_button("close_save_as", "X", { 34.0f, 30.0f });

			ImGui::SetCursorScreenPos({ outer_padding, 58.0f });
			ImGui::TextUnformatted("Preset name");
			const ImVec2 name_min = { outer_padding, 88.0f };
			const ImVec2 name_max = { io.DisplaySize.x - outer_padding, 136.0f };
			ImGui::GetWindowDrawList()->AddRectFilled(name_min, name_max, IM_COL32(13, 29, 38, 255), 3.0f);
			ImGui::GetWindowDrawList()->AddRect(name_min, name_max, IM_COL32(0, 174, 197, 255), 3.0f, 0, 2.0f);
			const std::string displayed_name = fit_text(_implementation->save_as_name.c_str(), name_max.x - name_min.x - 24.0f);
			ImGui::GetWindowDrawList()->AddText({ name_min.x + 12.0f, name_min.y + (name_max.y - name_min.y - ImGui::GetFontSize()) * 0.5f },
				IM_COL32(235, 250, 255, 255), displayed_name.c_str());

			auto draw_keyboard_row = [&](const char *keys, float start_x, float y) {
				for (size_t index = 0; keys[index] != '\0'; ++index)
				{
					ImGui::PushID(static_cast<int>(y * 100.0f) + static_cast<int>(index));
					ImGui::SetCursorScreenPos({ start_x + static_cast<float>(index) * 92.0f, y });
					char label[2] = { keys[index], '\0' };
					if (!_implementation->save_as_uppercase && label[0] >= 'A' && label[0] <= 'Z')
						label[0] = static_cast<char>(label[0] - 'A' + 'a');
					const bool hovered = draw_text_button("key", label, { 84.0f, 42.0f });
					if (input.select_clicked && hovered && _implementation->save_as_name.size() < 120)
						_implementation->save_as_name.push_back(label[0]);
					ImGui::PopID();
				}
			};
			draw_keyboard_row("1234567890", 56.0f, 158.0f);
			draw_keyboard_row("QWERTYUIOP", 56.0f, 208.0f);
			draw_keyboard_row("ASDFGHJKL", 102.0f, 258.0f);
			draw_keyboard_row("ZXCVBNM", 194.0f, 308.0f);

			ImGui::SetCursorScreenPos({ 56.0f, 366.0f });
			const bool case_hovered = draw_text_button("case", _implementation->save_as_uppercase ? "ABC" : "abc", { 104.0f, 42.0f }, true,
				_implementation->save_as_uppercase ? IM_COL32(22, 132, 72, 255) : 0);
			ImGui::SetCursorScreenPos({ 168.0f, 366.0f });
			const bool space_hovered = draw_text_button("space", "Space", { 320.0f, 42.0f });
			ImGui::SetCursorScreenPos({ 496.0f, 366.0f });
			const bool backspace_hovered = draw_text_button("backspace", "Backspace", { 160.0f, 42.0f });
			ImGui::SetCursorScreenPos({ 664.0f, 366.0f });
			const bool clear_hovered = draw_text_button("clear", "Clear", { 112.0f, 42.0f });

			ImGui::SetCursorScreenPos({ 56.0f, 430.0f });
			const bool cancel_hovered = draw_text_button("cancel_save_as", "Cancel", { 160.0f, 46.0f });
			const bool valid_name = !_implementation->save_as_name.empty() &&
				_implementation->save_as_name.back() != ' ' && _implementation->save_as_name.back() != '.';
			const char *const create_label = _implementation->save_as_feedback < 0 ? "Failed" : "Create";
			ImGui::SetCursorScreenPos({ io.DisplaySize.x - outer_padding - 160.0f, 430.0f });
			const bool create_hovered = draw_text_button("create_preset", create_label, { 160.0f, 46.0f }, valid_name,
				_implementation->save_as_feedback < 0 ? IM_COL32(166, 48, 48, 255) : IM_COL32(22, 132, 72, 255));

			if (input.select_clicked && (close_dialog_hovered || cancel_hovered))
			{
				_implementation->save_as_dialog_open = false;
				_implementation->save_as_name.clear();
			}
			else if (input.select_clicked && case_hovered)
				_implementation->save_as_uppercase = !_implementation->save_as_uppercase;
			else if (input.select_clicked && space_hovered && !_implementation->save_as_name.empty() &&
				_implementation->save_as_name.back() != ' ' && _implementation->save_as_name.size() < 120)
				_implementation->save_as_name.push_back(' ');
			else if (input.select_clicked && backspace_hovered && !_implementation->save_as_name.empty())
				_implementation->save_as_name.pop_back();
			else if (input.select_clicked && clear_hovered)
				_implementation->save_as_name.clear();
			else if (input.select_clicked && create_hovered)
			{
				result.action = render_result::command::save_preset_as;
				result.text = _implementation->save_as_name;
			}
		}
		else if (_implementation->selected_technique == 0)
		{
			std::vector<const technique *> displayed_techniques;
			displayed_techniques.reserve(snapshot.techniques.size());
			for (const technique &entry : snapshot.techniques)
				if ((_implementation->show_hidden || !entry.hidden) && (!_implementation->show_favourites_only || entry.favourite))
					displayed_techniques.push_back(&entry);

			const size_t visible_techniques = input.expanded ? compact_techniques_per_page * 2 : compact_techniques_per_page;
			const size_t maximum_scroll = displayed_techniques.size() > visible_techniques ? displayed_techniques.size() - visible_techniques : 0;
			_implementation->technique_scroll = (std::min)(_implementation->technique_scroll, maximum_scroll);
			if (_implementation->dragging == implementation::drag_target::none)
			{
				const int scroll_direction = input.scroll_y > joystick_scroll_dead_zone ? -1 :
					input.scroll_y < -joystick_scroll_dead_zone ? 1 : 0;
				bool advance_scroll = false;
				if (scroll_direction != _implementation->joystick_scroll_direction)
				{
					_implementation->joystick_scroll_direction = scroll_direction;
					_implementation->joystick_scroll_frame_count = 0;
					advance_scroll = scroll_direction != 0;
				}
				else if (scroll_direction != 0 && ++_implementation->joystick_scroll_frame_count >= joystick_scroll_repeat_frames)
				{
					_implementation->joystick_scroll_frame_count = 0;
					advance_scroll = true;
				}
				if (advance_scroll && scroll_direction < 0 && _implementation->technique_scroll > 0)
					--_implementation->technique_scroll;
				else if (advance_scroll && scroll_direction > 0 && _implementation->technique_scroll < maximum_scroll)
					++_implementation->technique_scroll;
			}
			else
			{
				_implementation->joystick_scroll_direction = 0;
				_implementation->joystick_scroll_frame_count = 0;
			}
			char page_text[64];
			std::snprintf(page_text, sizeof(page_text), "%zu shown  |  Graphics: %s", displayed_techniques.size(), graphics_api_label);
			const ImVec2 header_origin = ImGui::GetCursorScreenPos();
			ImGui::TextUnformatted("ReShade Techniques");
			ImGui::SameLine();
			ImGui::TextDisabled("%s", page_text);
			ImGui::SetCursorScreenPos({ ImGui::GetWindowWidth() - outer_padding - 34.0f, header_origin.y });
			const bool close_hovered = draw_text_button("close", "X", { 34.0f, 30.0f });

			ImGui::SetCursorScreenPos({ header_origin.x, header_origin.y + 36.0f });
			const bool active_first_hovered = draw_text_button("active_first", "Active first", { 140.0f, 30.0f });
			ImGui::SetCursorScreenPos({ header_origin.x + 148.0f, header_origin.y + 36.0f });
			const bool favourites_only_hovered = draw_text_button("favourites_only", "Starred only", { 150.0f, 30.0f }, true,
				_implementation->show_favourites_only ? IM_COL32(184, 138, 20, 255) : 0);
			ImGui::SetCursorScreenPos({ header_origin.x + 306.0f, header_origin.y + 36.0f });
			const bool show_hidden_hovered = draw_text_button("show_hidden", _implementation->show_hidden ? "Hide hidden" : "Show hidden", { 140.0f, 30.0f }, true,
				_implementation->show_hidden ? IM_COL32(22, 132, 72, 255) : 0);

			ImGui::SetCursorScreenPos({ header_origin.x, header_origin.y + 72.0f });
			ImGui::TextUnformatted("Current preset:");
			ImGui::SameLine();
			const float preset_controls_x = ImGui::GetWindowWidth() - outer_padding - 348.0f;
			const float preset_name_width = preset_controls_x - ImGui::GetCursorScreenPos().x - 8.0f;
			const std::string preset_label = fit_text(input.preset_name.empty() ? "No preset" : input.preset_name.c_str(), preset_name_width);
			ImGui::TextUnformatted(preset_label.c_str());
			ImGui::SameLine(preset_controls_x);
			const bool previous_preset_hovered = draw_text_button("previous_preset", "<", { 34.0f, 30.0f }, !input.shaders_temporarily_disabled);
			ImGui::SameLine();
			const bool next_preset_hovered = draw_text_button("next_preset", ">", { 34.0f, 30.0f }, !input.shaders_temporarily_disabled);
			ImGui::SameLine();
			const char *const save_label = input.preset_dirty ? "Save" : _implementation->save_feedback > 0 ? "Saved" : _implementation->save_feedback < 0 ? "Failed" : "Save";
			const ImU32 save_color = input.preset_dirty ? IM_COL32(184, 138, 20, 255) :
				_implementation->save_feedback > 0 ? IM_COL32(22, 132, 72, 255) :
				_implementation->save_feedback < 0 ? IM_COL32(166, 48, 48, 255) :
				IM_COL32(22, 132, 72, 255);
			const bool save_hovered = draw_text_button("save", save_label, { 68.0f, 30.0f }, !input.shaders_temporarily_disabled, save_color);
			ImGui::SameLine();
			const char *const save_as_label = _implementation->save_as_feedback == 2 ? "Creating..." :
				_implementation->save_as_feedback > 0 ? "Created" : _implementation->save_as_feedback < 0 ? "Failed" : "Save As";
			const ImU32 save_as_color = _implementation->save_as_feedback == 2 ? IM_COL32(184, 138, 20, 255) :
				_implementation->save_as_feedback > 0 ? IM_COL32(22, 132, 72, 255) :
				_implementation->save_as_feedback < 0 ? IM_COL32(166, 48, 48, 255) : 0;
			const bool save_as_hovered = draw_text_button("save_as", save_as_label, { 88.0f, 30.0f },
				!input.shaders_temporarily_disabled && _implementation->save_as_feedback != 2, save_as_color);
			ImGui::SameLine();
			const bool delete_preset_hovered = draw_text_button("delete_preset", "Delete", { 76.0f, 30.0f },
				!input.shaders_temporarily_disabled && _implementation->save_as_feedback != 2, IM_COL32(166, 48, 48, 255));
			ImGui::SetCursorScreenPos({ header_origin.x, header_origin.y + 108.0f });
			if (input.select_clicked && close_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::hide_panel;
			else if (input.select_clicked && active_first_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::move_active_to_top;
				_implementation->technique_scroll = 0;
			}
			else if (input.select_clicked && favourites_only_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->show_favourites_only = !_implementation->show_favourites_only;
				_implementation->technique_scroll = 0;
			}
			else if (input.select_clicked && show_hidden_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->show_hidden = !_implementation->show_hidden;
				_implementation->technique_scroll = 0;
			}
			ImGui::Separator();
			if (!input.effect_runtime_available)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.22f, 1.0f), "ReShade effect runtime unavailable");
				ImGui::TextWrapped("ReShade did not initialize OpenXR effects. Check ReShade.log for an unproxied graphics device.");
				ImGui::Separator();
			}
			else if (snapshot.techniques.empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.22f, 1.0f), "No ReShade techniques found");
				ImGui::TextWrapped("Check EffectSearchPaths in ReShadeVR.ini or ReShade.ini.");
				ImGui::Separator();
			}

			const float list_height = ImGui::GetContentRegionAvail().y - main_footer_height;
			const float row_height = (list_height - row_spacing * (visible_techniques - 1)) / visible_techniques;
			const ImVec2 list_min = ImGui::GetCursorScreenPos();
			const float row_width = ImGui::GetContentRegionAvail().x - 42.0f;
			const size_t first = _implementation->technique_scroll;
			const size_t last = (std::min)(first + visible_techniques, displayed_techniques.size());
			for (size_t index = first; index < first + visible_techniques; ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				const ImVec2 size = { row_width, row_height };
				ImGui::InvisibleButton("technique", size);
				const bool row_hovered = ImGui::IsItemHovered();
				const ImVec2 next_row = ImGui::GetCursorScreenPos();
				const ImVec2 minimum = ImGui::GetItemRectMin();
				const ImVec2 maximum = ImGui::GetItemRectMax();
				ImDrawList *const draw_list = ImGui::GetWindowDrawList();
				draw_list->AddRectFilled(minimum, maximum, IM_COL32(13, 29, 38, 255), 3.0f);

				if (index < last)
				{
					const technique &entry = *displayed_techniques[index];
					ImGui::SetCursorScreenPos({ minimum.x + 4.0f, minimum.y + 2.0f });
					ImGui::InvisibleButton("toggle", { 40.0f, row_height - 4.0f });
					const bool toggle_hovered = ImGui::IsItemHovered();
					const ImVec2 indicator_min = { minimum.x + 12.0f, minimum.y + (row_height - 16.0f) * 0.5f };
					const ImVec2 indicator_max = { indicator_min.x + 16.0f, indicator_min.y + 16.0f };
					draw_list->AddRectFilled(indicator_min, indicator_max, entry.enabled ? IM_COL32(26, 196, 100, 255) : IM_COL32(72, 82, 86, 255), 3.0f);
					if (entry.enabled)
					{
						draw_list->AddLine({ indicator_min.x + 3.0f, indicator_min.y + 8.0f }, { indicator_min.x + 7.0f, indicator_min.y + 12.0f }, IM_COL32_WHITE, 2.0f);
						draw_list->AddLine({ indicator_min.x + 7.0f, indicator_min.y + 12.0f }, { indicator_min.x + 14.0f, indicator_min.y + 4.0f }, IM_COL32_WHITE, 2.0f);
					}
					const ImVec2 text_min = { minimum.x + 50.0f, minimum.y + (row_height - ImGui::GetFontSize()) * 0.5f };
					char label[320];
					std::snprintf(label, sizeof(label), "%s  [%s]", entry.name.c_str(), entry.effect_name.c_str());
					const ImVec2 text_max = { maximum.x - 348.0f, maximum.y };
					const std::string fitted_label = fit_text(label, text_max.x - text_min.x);
					draw_list->AddText(text_min, entry.has_non_default_values ? changed_value_color : IM_COL32(222, 242, 245, 255), fitted_label.c_str());
					draw_hover_details(text_min, text_max, label, entry.tooltip.c_str());
					const auto order_it = std::find_if(snapshot.techniques.begin(), snapshot.techniques.end(), [&entry](const technique &candidate) {
						return candidate.id == entry.id;
					});
					const bool can_move_up = order_it != snapshot.techniques.end() && order_it != snapshot.techniques.begin();
					const bool can_move_down = order_it != snapshot.techniques.end() && order_it + 1 != snapshot.techniques.end();
					ImGui::SetCursorScreenPos({ maximum.x - 338.0f, minimum.y + 4.0f });
					const bool move_up_hovered = draw_text_button("move_up", "^", { 34.0f, row_height - 8.0f }, !input.shaders_temporarily_disabled && can_move_up);
					ImGui::SameLine();
					ImGui::SetCursorScreenPos({ maximum.x - 298.0f, minimum.y + 4.0f });
					const bool move_down_hovered = draw_text_button("move_down", "v", { 34.0f, row_height - 8.0f }, !input.shaders_temporarily_disabled && can_move_down);
					const bool move_controls_hovered = ImGui::IsMouseHoveringRect(
						{ maximum.x - 338.0f, minimum.y + 4.0f }, { maximum.x - 264.0f, maximum.y - 4.0f });
					ImGui::SameLine();
					ImGui::SetCursorScreenPos({ maximum.x - 258.0f, minimum.y + 4.0f });
					const bool favourite_hovered = draw_star_button("favourite", { 40.0f, row_height - 8.0f }, entry.favourite);
					ImGui::SameLine();
					ImGui::SetCursorScreenPos({ maximum.x - 212.0f, minimum.y + 4.0f });
					const bool hide_hovered = draw_text_button("hide", entry.hidden ? "Unhide" : "Hide", { 100.0f, row_height - 8.0f });
					ImGui::SameLine();
					ImGui::SetCursorScreenPos({ maximum.x - 106.0f, minimum.y + 4.0f });
					const bool open_hovered = draw_text_button("open", "Open", { 100.0f, row_height - 8.0f }, !input.shaders_temporarily_disabled);
					if (input.select_clicked && open_hovered && _implementation->dragging == implementation::drag_target::none)
					{
						_implementation->selected_technique = entry.id;
						_implementation->parameter_scroll = 0;
					}
					else if (input.select_clicked && hide_hovered && _implementation->dragging == implementation::drag_target::none)
					{
						result.action = render_result::command::set_technique_hidden;
						result.target_id = entry.id;
						result.state = !entry.hidden;
					}
					else if (input.select_clicked && favourite_hovered && _implementation->dragging == implementation::drag_target::none)
					{
						result.action = render_result::command::set_technique_favourite;
						result.target_id = entry.id;
						result.state = !entry.favourite;
					}
					else if (input.select_clicked && move_controls_hovered && _implementation->dragging == implementation::drag_target::none)
					{
						if (move_up_hovered || move_down_hovered)
						{
							result.action = render_result::command::move_technique;
							result.target_id = entry.id;
							result.state = move_up_hovered;
						}
					}
					else if (input.select_clicked && !input.shaders_temporarily_disabled && (row_hovered || toggle_hovered) && _implementation->dragging == implementation::drag_target::none)
					{
						result.action = render_result::command::toggle_technique;
						result.target_id = entry.id;
					}
				}
				ImGui::SetCursorScreenPos(next_row);
				ImGui::PopID();
			}

			const ImVec2 footer_min = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos({ list_min.x + row_width + 8.0f, list_min.y });
			const float scroll_fraction = maximum_scroll > 0 ? static_cast<float>(_implementation->technique_scroll) / static_cast<float>(maximum_scroll) : 0.0f;
			const slider_interaction scroll_slider = draw_vertical_slider("technique_scroll", { 34.0f, list_height }, scroll_fraction, maximum_scroll > 0);
			if (input.select_clicked && scroll_slider.hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->dragging = implementation::drag_target::technique_scroll;
				_implementation->release_frame_count = 0;
				_implementation->drag_start_pointer = io.MousePos.y;
				_implementation->drag_start_fraction = scroll_fraction;
			}
			if (_implementation->dragging == implementation::drag_target::technique_scroll && (input.select_clicked || input.select_down))
			{
				const float drag_fraction = std::clamp(_implementation->drag_start_fraction +
					(io.MousePos.y - _implementation->drag_start_pointer) / list_height * 0.9f, 0.0f, 1.0f);
				_implementation->technique_scroll = static_cast<size_t>(std::round(drag_fraction * static_cast<float>(maximum_scroll)));
			}

			ImGui::SetCursorScreenPos(footer_min);
			const bool performance_hovered = draw_tick_box("performance_mode", "Performance next start", input.performance_mode, 230.0f);
			ImGui::SetCursorScreenPos({ footer_min.x + 330.0f, footer_min.y });
			const bool statistics_hovered = draw_tick_box("always_show_statistics", "Always show FPS", input.always_show_statistics, 190.0f);
			ImGui::SetCursorScreenPos({ footer_min.x + 330.0f, footer_min.y + 39.0f });
			char frame_rate_label[48];
			std::snprintf(frame_rate_label, sizeof(frame_rate_label), "FPS target: %s", frame_rate_divisor_name(input.frame_rate_divisor));
			const bool frame_rate_hovered = draw_text_button("frame_rate_divisor", frame_rate_label, { 240.0f, 30.0f });
			ImGui::SetCursorScreenPos({ footer_min.x, footer_min.y + 37.0f });
			const bool starts_open_hovered = draw_tick_box("starts_open", "UI starts open", input.ui_starts_open, 184.0f);
			ImGui::SetCursorScreenPos({ footer_min.x, footer_min.y + 74.0f });
			const bool long_press_hovered = draw_tick_box("long_press", "Hide/show: hold L3 + R3 1.5s", input.long_press_enabled);
			ImGui::SetCursorScreenPos({ footer_min.x, footer_min.y + 111.0f });
			const bool disable_all_hovered = draw_text_button("disable_all",
				input.shaders_temporarily_disabled ? "Restore shaders" : "Disable all", { 150.0f, 34.0f }, true,
				input.shaders_temporarily_disabled ? IM_COL32(22, 132, 72, 255) : 0);
			ImGui::SameLine(ImGui::GetWindowWidth() - outer_padding - 104.0f);
			const bool expand_hovered = draw_text_button("expand", input.expanded ? "Collapse" : "Expand", { 104.0f, 34.0f });
			if (input.select_clicked && previous_preset_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::previous_preset;
			else if (input.select_clicked && next_preset_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::next_preset;
			else if (input.select_clicked && save_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::save_preset;
			else if (input.select_clicked && save_as_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->save_as_dialog_open = true;
				_implementation->save_as_name = input.preset_name.empty() ? "New Preset" : input.preset_name + " Copy";
				_implementation->save_as_feedback = 0;
				_implementation->save_as_feedback_frame_count = 0;
			}
			else if (input.select_clicked && delete_preset_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::delete_preset;
			else if (input.select_clicked && disable_all_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::toggle_all_techniques;
			else if (input.select_clicked && expand_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::toggle_panel_size;
			else if (input.select_clicked && long_press_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::toggle_long_press;
			else if (input.select_clicked && starts_open_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::set_ui_starts_open;
				result.state = !input.ui_starts_open;
			}
			else if (input.select_clicked && performance_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::set_performance_mode;
				result.state = !input.performance_mode;
			}
			else if (input.select_clicked && statistics_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::set_always_show_statistics;
				result.state = !input.always_show_statistics;
			}
			else if (input.select_clicked && frame_rate_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::set_frame_rate_divisor;
				result.value = input.frame_rate_divisor % max_frame_rate_divisor + 1;
			}
		}
		else
		{
			const technique &selected = *selected_it;
			const bool back_hovered = draw_text_button("back", "Back", { 64.0f, 30.0f });
			ImGui::SameLine();
			char selected_label[320];
			std::snprintf(selected_label, sizeof(selected_label), "%s  [%s]", selected.name.c_str(), selected.effect_name.c_str());
			const float controls_x = ImGui::GetWindowWidth() - outer_padding - 430.0f;
			const float selected_text_width = controls_x - ImGui::GetCursorScreenPos().x - 8.0f;
			const std::string fitted_selected_label = fit_text(selected_label, selected_text_width);
			ImGui::TextUnformatted(fitted_selected_label.c_str());
			draw_hover_details(ImGui::GetItemRectMin(), { ImGui::GetItemRectMin().x + selected_text_width, ImGui::GetItemRectMax().y }, selected_label, selected.tooltip.c_str());
			ImGui::SameLine(controls_x);
			const bool toggle_hovered = draw_text_button("toggle", selected.enabled ? "Disable" : "Enable", { 92.0f, 30.0f });
			ImGui::SameLine();
			const bool reset_all_hovered = draw_text_button("reset_all", "Reset All", { 110.0f, 30.0f });
			ImGui::SameLine();
			const bool revert_restore_hovered = draw_text_button("revert_restore", selected.has_restore_state ? "Restore" : "Revert", { 90.0f, 30.0f },
				(selected.has_restore_state || selected.differs_from_saved) && !input.shaders_temporarily_disabled);
			ImGui::SameLine();
			const char *const save_label = input.preset_dirty ? "Save" : _implementation->save_feedback > 0 ? "Saved" : _implementation->save_feedback < 0 ? "Failed" : "Save";
			const ImU32 save_color = input.preset_dirty ? IM_COL32(184, 138, 20, 255) :
				_implementation->save_feedback > 0 ? IM_COL32(22, 132, 72, 255) :
				_implementation->save_feedback < 0 ? IM_COL32(166, 48, 48, 255) :
				IM_COL32(22, 132, 72, 255);
			const bool save_hovered = draw_text_button("save", save_label, { 100.0f, 30.0f }, true, save_color);
			if (input.select_clicked && back_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->selected_technique = 0;
				_implementation->parameter_scroll = 0;
			}
			else if (input.select_clicked && toggle_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::toggle_technique;
				result.target_id = selected.id;
			}
			else if (input.select_clicked && reset_all_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::reset_all_parameters;
				result.target_id = selected.id;
			}
			else if (input.select_clicked && revert_restore_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = selected.has_restore_state ? render_result::command::restore_technique : render_result::command::revert_technique;
				result.target_id = selected.id;
			}
			else if (input.select_clicked && save_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::save_preset;
			ImGui::Separator();
			if (!selected.tooltip.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, default_value_color);
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
				ImGui::TextUnformatted(selected.tooltip.c_str());
				ImGui::PopTextWrapPos();
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}

			struct component_reference
			{
				const parameter *entry;
				uint32_t component;
			};
			std::vector<component_reference> components;
			for (const parameter &entry : snapshot.parameters)
				if (entry.effect_name == selected.effect_name)
					for (uint32_t component = 0; component < entry.values.size(); ++component)
						components.push_back({ &entry, component });

			const size_t visible_parameters = input.expanded ? compact_visible_parameters * 2 : compact_visible_parameters;
			const size_t maximum_scroll = components.size() > visible_parameters ? components.size() - visible_parameters : 0;
			_implementation->parameter_scroll = (std::min)(_implementation->parameter_scroll, maximum_scroll);
			if (_implementation->dragging == implementation::drag_target::none)
			{
				const int scroll_direction = input.scroll_y > joystick_scroll_dead_zone ? -1 :
					input.scroll_y < -joystick_scroll_dead_zone ? 1 : 0;
				bool advance_scroll = false;
				if (scroll_direction != _implementation->joystick_scroll_direction)
				{
					_implementation->joystick_scroll_direction = scroll_direction;
					_implementation->joystick_scroll_frame_count = 0;
					advance_scroll = scroll_direction != 0;
				}
				else if (scroll_direction != 0 && ++_implementation->joystick_scroll_frame_count >= joystick_scroll_repeat_frames)
				{
					_implementation->joystick_scroll_frame_count = 0;
					advance_scroll = true;
				}
				if (advance_scroll && scroll_direction < 0 && _implementation->parameter_scroll > 0)
					--_implementation->parameter_scroll;
				else if (advance_scroll && scroll_direction > 0 && _implementation->parameter_scroll < maximum_scroll)
					++_implementation->parameter_scroll;
			}
			else
			{
				_implementation->joystick_scroll_direction = 0;
				_implementation->joystick_scroll_frame_count = 0;
			}
			const float list_height = ImGui::GetContentRegionAvail().y - detail_footer_height;
			const float row_height = (list_height - row_spacing * (visible_parameters - 1)) / visible_parameters;
			const ImVec2 list_min = ImGui::GetCursorScreenPos();
			const float row_width = ImGui::GetContentRegionAvail().x - 42.0f;
			const size_t first = _implementation->parameter_scroll;
			const size_t last = (std::min)(first + visible_parameters, components.size());
			for (size_t index = first; index < first + visible_parameters; ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				const ImVec2 row_min = ImGui::GetCursorScreenPos();
				const ImVec2 row_max = { row_min.x + row_width, row_min.y + row_height };
				ImGui::GetWindowDrawList()->AddRectFilled(row_min, row_max, IM_COL32(13, 29, 38, 255), 3.0f);
				if (index < last)
				{
					const component_reference &reference = components[index];
					const parameter &entry = *reference.entry;
					char label[256];
					if (entry.values.size() == 1)
						std::snprintf(label, sizeof(label), "%s", entry.name.c_str());
					else
						std::snprintf(label, sizeof(label), "%s [%u]", entry.name.c_str(), reference.component + 1);
					constexpr float label_font_size = 18.0f;
					const ImVec2 label_min = { row_min.x + 10.0f, row_min.y };
					const ImVec2 label_max = { row_max.x - 10.0f, row_min.y + label_font_size + 3.0f };
					const std::string fitted_label = fit_text(label, label_max.x - label_min.x);
					const bool changed_from_default = openxr_ui::component_differs_from_default(entry, reference.component);

					char value_text[64];
					if (entry.type == parameter_type::boolean)
						std::snprintf(value_text, sizeof(value_text), "%s", entry.values[reference.component] != 0.0 ? "On" : "Off");
					else if (entry.type == parameter_type::floating_point)
						std::snprintf(value_text, sizeof(value_text), "%.4g", entry.values[reference.component]);
					else
						std::snprintf(value_text, sizeof(value_text), "%.0f", entry.values[reference.component]);
					const bool has_slider = entry.type != parameter_type::boolean && std::isfinite(entry.minimum) && std::isfinite(entry.maximum) && entry.minimum < entry.maximum;
					const float slider_fraction = has_slider ? static_cast<float>((entry.values[reference.component] - entry.minimum) / (entry.maximum - entry.minimum)) : 0.0f;
					const float control_height = row_height - label_font_size - 7.0f;
					const float control_y = row_max.y - control_height - 3.0f;
					ImGui::SetCursorScreenPos({ row_max.x - 810.0f, control_y });
					const slider_interaction value_slider = draw_slider("value_slider", { 350.0f, control_height }, slider_fraction, has_slider);
					if (input.select_clicked && value_slider.hovered && _implementation->dragging == implementation::drag_target::none)
					{
						_implementation->dragging = implementation::drag_target::value_slider;
						_implementation->release_frame_count = 0;
						_implementation->dragged_parameter = entry.id;
						_implementation->dragged_component = reference.component;
					}

					const ImVec2 value_size = ImGui::CalcTextSize(value_text);
					ImGui::GetWindowDrawList()->AddText({ row_max.x - 362.0f - value_size.x, control_y + (control_height - ImGui::GetFontSize()) * 0.5f },
						changed_from_default ? changed_value_color : default_value_color, value_text);

					bool coarse_decrease_hovered = false;
					bool decrease_hovered = false;
					bool increase_hovered = false;
					bool coarse_increase_hovered = false;
					ImGui::SetCursorScreenPos({ row_max.x - 350.0f, control_y });
					if (entry.type == parameter_type::boolean)
						increase_hovered = draw_text_button("change", "Toggle", { 92.0f, control_height });
					else
					{
						coarse_decrease_hovered = draw_text_button("coarse_decrease", "-10x", { 52.0f, control_height });
						ImGui::SameLine();
						decrease_hovered = draw_text_button("decrease", "-", { 42.0f, control_height });
						ImGui::SameLine();
						increase_hovered = draw_text_button("increase", "+", { 42.0f, control_height });
						ImGui::SameLine();
						coarse_increase_hovered = draw_text_button("coarse_increase", "+10x", { 52.0f, control_height });
					}
					ImGui::SameLine();
					char reset_label[64] = "*";
					if (entry.resettable && reference.component < entry.default_values.size())
					{
						if (entry.type == parameter_type::boolean)
							std::snprintf(reset_label, sizeof(reset_label), "* %s", entry.default_values[reference.component] != 0.0 ? "On" : "Off");
						else if (entry.type == parameter_type::floating_point)
							std::snprintf(reset_label, sizeof(reset_label), "* %.3g", entry.default_values[reference.component]);
						else
							std::snprintf(reset_label, sizeof(reset_label), "* %.0f", entry.default_values[reference.component]);
					}
					const bool reset_hovered = draw_text_button("reset", reset_label, { 92.0f, control_height }, entry.resettable);
					if (reset_hovered && ImGui::BeginTooltip())
					{
						ImGui::TextUnformatted("Revert to default");
						ImGui::EndTooltip();
					}
					const bool dragging_value = _implementation->dragging == implementation::drag_target::value_slider &&
						_implementation->dragged_parameter == entry.id && _implementation->dragged_component == reference.component;
					if (result.action == render_result::command::none &&
						((input.select_clicked && _implementation->dragging == implementation::drag_target::none) ||
						(dragging_value && (input.select_clicked || input.select_down))))
					{
						if (value_slider.hovered || dragging_value)
						{
							double value = entry.minimum + (entry.maximum - entry.minimum) * value_slider.fraction;
							if (entry.type != parameter_type::floating_point)
								value = std::round(value);
							result.action = render_result::command::set_parameter;
							result.target_id = entry.id;
							result.component = reference.component;
							result.value = value;
						}
						else if (coarse_decrease_hovered || decrease_hovered || increase_hovered || coarse_increase_hovered)
						{
							result.action = render_result::command::set_parameter;
							result.target_id = entry.id;
							result.component = reference.component;
							result.value = entry.type == parameter_type::boolean ? (entry.values[reference.component] == 0.0 ? 1.0 : 0.0) :
								entry.values[reference.component] + (coarse_increase_hovered ? 10.0 * entry.step :
									increase_hovered ? entry.step : coarse_decrease_hovered ? -10.0 * entry.step : -entry.step);
						}
						else if (reset_hovered)
						{
							result.action = render_result::command::reset_parameter;
							result.target_id = entry.id;
						}
					}
					ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), label_font_size, label_min,
						changed_from_default ? changed_value_color : IM_COL32(222, 242, 245, 255), fitted_label.c_str());
					draw_hover_details(label_min, label_max, label, entry.tooltip.c_str());
				}
				ImGui::SetCursorScreenPos({ row_min.x, row_max.y + row_spacing });
				ImGui::PopID();
			}

			const ImVec2 footer_min = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos({ list_min.x + row_width + 8.0f, list_min.y });
			const float scroll_fraction = maximum_scroll > 0 ? static_cast<float>(_implementation->parameter_scroll) / static_cast<float>(maximum_scroll) : 0.0f;
			const slider_interaction scroll_slider = draw_vertical_slider("parameter_scroll", { 34.0f, list_height }, scroll_fraction, maximum_scroll > 0);
			if (input.select_clicked && scroll_slider.hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->dragging = implementation::drag_target::parameter_scroll;
				_implementation->release_frame_count = 0;
				_implementation->drag_start_pointer = io.MousePos.y;
				_implementation->drag_start_fraction = scroll_fraction;
			}
			if (_implementation->dragging == implementation::drag_target::parameter_scroll && (input.select_clicked || input.select_down))
			{
				const float drag_fraction = std::clamp(_implementation->drag_start_fraction +
					(io.MousePos.y - _implementation->drag_start_pointer) / list_height * 0.9f, 0.0f, 1.0f);
				_implementation->parameter_scroll = static_cast<size_t>(std::round(drag_fraction * static_cast<float>(maximum_scroll)));
			}

			ImGui::SetCursorScreenPos(footer_min);
			ImGui::TextDisabled("%zu values  |  API: %s", components.size(), graphics_api_label);
			ImGui::SameLine(ImGui::GetWindowWidth() - outer_padding - 104.0f);
			const bool expand_hovered = draw_text_button("expand", input.expanded ? "Collapse" : "Expand", { 104.0f, 34.0f });
			if (result.action == render_result::command::none && input.select_clicked && expand_hovered &&
				_implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::toggle_panel_size;
		}

		if (_implementation->save_feedback_frame_count != 0)
		{
			const char *const message = _implementation->save_feedback > 0 ? "Preset saved" : "Preset save failed";
			const ImVec2 text_size = ImGui::CalcTextSize(message);
			const ImVec2 minimum = { (io.DisplaySize.x - text_size.x) * 0.5f - 18.0f, 54.0f };
			const ImVec2 maximum = { minimum.x + text_size.x + 36.0f, minimum.y + text_size.y + 18.0f };
			ImDrawList *const foreground = ImGui::GetForegroundDrawList();
			foreground->AddRectFilled(minimum, maximum,
				_implementation->save_feedback > 0 ? IM_COL32(18, 116, 62, 245) : IM_COL32(156, 38, 38, 245), 4.0f);
			foreground->AddRect(minimum, maximum, IM_COL32(225, 250, 235, 255), 4.0f, 0, 2.0f);
			foreground->AddText({ minimum.x + 18.0f, minimum.y + 9.0f }, IM_COL32_WHITE, message);
			if (--_implementation->save_feedback_frame_count == 0)
				_implementation->save_feedback = 0;
		}
		if (_implementation->save_as_feedback_frame_count != 0 && --_implementation->save_as_feedback_frame_count == 0)
			_implementation->save_as_feedback = 0;

		if (input.pointer_visible)
			draw_crosshair(ImGui::GetForegroundDrawList(), io.MousePos);
		ImGui::End();
		ImGui::Render();
		return result;
	}

	render_result panel_renderer::render(ID3D11RenderTargetView *render_target, const technique_snapshot &snapshot, const render_input &input)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr || _implementation->api != implementation::graphics_api::d3d11 || render_target == nullptr)
			return {};

		ImGui::SetCurrentContext(_implementation->context);
		ImGui_ImplDX11_NewFrame();
		const render_result result = render_ui(snapshot, input);
		submit(render_target);
		return result;
	}

	render_result panel_renderer::render(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target,
		const technique_snapshot &snapshot, const render_input &input)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr || _implementation->api != implementation::graphics_api::d3d12 ||
			command_list == nullptr || render_target.ptr == 0)
			return {};

		ImGui::SetCurrentContext(_implementation->context);
		ImGui_ImplDX12_NewFrame();
		const render_result result = render_ui(snapshot, input);
		submit(command_list, render_target);
		return result;
	}

	void panel_renderer::render_statistics(ID3D11RenderTargetView *render_target, const frame_statistics &statistics)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr || _implementation->api != implementation::graphics_api::d3d11 || render_target == nullptr)
			return;

		ImGui::SetCurrentContext(_implementation->context);
		ImGui_ImplDX11_NewFrame();
		render_statistics_ui(statistics);
		submit(render_target);
	}

	void panel_renderer::render_statistics(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target, const frame_statistics &statistics)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr || _implementation->api != implementation::graphics_api::d3d12 ||
			command_list == nullptr || render_target.ptr == 0)
			return;

		ImGui::SetCurrentContext(_implementation->context);
		ImGui_ImplDX12_NewFrame();
		render_statistics_ui(statistics);
		submit(command_list, render_target);
	}

	void panel_renderer::render_statistics_ui(const frame_statistics &statistics)
	{
		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = { static_cast<float>(_implementation->width), static_cast<float>(_implementation->height) };
		io.DeltaTime = 1.0f / 90.0f;
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 0.0f, 0.0f });
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("Frame statistics", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
		ImGui::SetWindowFontScale(1.25f);
		if (statistics.revision == 0)
			ImGui::TextUnformatted("-- FPS");
		else
			ImGui::Text("%.0f FPS   %.1f ms", statistics.average_fps, statistics.frame_time_ms);
		char graph_label[32];
		openxr_ui::format_frame_graph_label(statistics, graph_label);
		ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(graph_label).x);
		ImGui::TextDisabled("%s", graph_label);
		openxr_ui::draw_frame_graph(statistics, ImGui::GetContentRegionAvail());
		ImGui::End();
		ImGui::Render();
	}

	void panel_renderer::submit(ID3D11RenderTargetView *render_target)
	{
		ID3D11RenderTargetView *previous_render_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView *previous_depth_stencil = nullptr;
		_implementation->device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous_render_targets, &previous_depth_stencil);
		const float transparent[4] = {};
		_implementation->device_context->ClearRenderTargetView(render_target, transparent);
		_implementation->device_context->OMSetRenderTargets(1, &render_target, nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		_implementation->device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous_render_targets, previous_depth_stencil);
		for (ID3D11RenderTargetView *previous_render_target : previous_render_targets)
			if (previous_render_target != nullptr)
				previous_render_target->Release();
		if (previous_depth_stencil != nullptr)
			previous_depth_stencil->Release();
	}

	void panel_renderer::submit(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target)
	{
		const float transparent[4] = {};
		command_list->ClearRenderTargetView(render_target, transparent, 0, nullptr);
		command_list->OMSetRenderTargets(1, &render_target, FALSE, nullptr);
		ID3D12DescriptorHeap *const heaps[] = { _implementation->srv_descriptor_heap };
		command_list->SetDescriptorHeaps(1, heaps);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
	}
}