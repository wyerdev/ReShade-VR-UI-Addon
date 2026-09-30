#define ImTextureID ImU64
#include <imgui.h>
#include <openvr.h>
#include <reshade.hpp>

#include "vr_frame_graph.hpp"
#include "vr_ui_state.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	bool s_show_hidden = false;
	bool s_favourites_only = false;
	bool s_open_logged = false;
	char s_save_as_name[128] = {};

	template <typename T>
	T *get_openvr_interface(const char *version)
	{
		const HMODULE openvr_module = GetModuleHandleW(L"openvr_api.dll");
		if (openvr_module == nullptr)
			return nullptr;
		const auto get_interface = reinterpret_cast<void *(VR_CALLTYPE *)(const char *, vr::EVRInitError *)>(
			GetProcAddress(openvr_module, "VR_GetGenericInterface"));
		if (get_interface == nullptr)
			return nullptr;
		vr::EVRInitError error = vr::VRInitError_None;
		T *const result = static_cast<T *>(get_interface(version, &error));
		return error == vr::VRInitError_None ? result : nullptr;
	}

	void draw_parameter_controls(const openxr_ui::technique_snapshot &snapshot, const openxr_ui::technique &technique)
	{
		for (const openxr_ui::parameter &parameter : snapshot.parameters)
		{
			if (parameter.effect_name != technique.effect_name)
				continue;

			ImGui::PushID(reinterpret_cast<const void *>(static_cast<uintptr_t>(parameter.id)));
			for (uint32_t component = 0; component < parameter.values.size(); ++component)
			{
				ImGui::PushID(static_cast<int>(component));
				char label[256];
				if (parameter.values.size() == 1)
					std::snprintf(label, sizeof(label), "%s", parameter.name.c_str());
				else
					std::snprintf(label, sizeof(label), "%s [%u]", parameter.name.c_str(), component + 1);

				double value = parameter.values[component];
				bool changed = false;
				if (parameter.type == openxr_ui::parameter_type::boolean)
				{
					bool enabled = value != 0.0;
					if (ImGui::Checkbox(label, &enabled))
					{
						value = enabled ? 1.0 : 0.0;
						changed = true;
					}
				}
				else if (std::isfinite(parameter.minimum) && std::isfinite(parameter.maximum) && parameter.minimum < parameter.maximum)
				{
					const char *const format = parameter.type == openxr_ui::parameter_type::floating_point ? "%.4g" : "%.0f";
					changed = ImGui::SliderScalar(label, ImGuiDataType_Double, &value, &parameter.minimum, &parameter.maximum, format);
				}
				else
				{
					changed = ImGui::InputDouble(label, &value, parameter.step, parameter.step * 10.0,
						parameter.type == openxr_ui::parameter_type::floating_point ? "%.4g" : "%.0f");
				}

				if (changed)
				{
					if (parameter.type != openxr_ui::parameter_type::floating_point)
						value = std::round(value);
					openxr_ui_set_parameter(parameter.id, component, value);
				}
				if (parameter.resettable)
				{
					ImGui::SameLine();
					if (ImGui::SmallButton("Reset"))
						openxr_ui_reset_parameter(parameter.id);
				}
				ImGui::PopID();
			}
			ImGui::PopID();
		}
	}

	void draw_openvr_overlay(reshade::api::effect_runtime *runtime)
	{
		if (!s_open_logged)
		{
			openxr_ui_log_message("SteamVR VR Effects dashboard page opened.");
			s_open_logged = true;
		}
		ImGui::TextUnformatted("ReShade VR Effects");
		ImGui::Separator();
		if (GetModuleHandleW(L"openvr_api.dll") == nullptr)
		{
			ImGui::TextWrapped("This page becomes active in a SteamVR/OpenVR session.");
			return;
		}
		if (!openxr_ui_uses_effect_runtime(runtime))
		{
			ImGui::TextWrapped("Waiting for ReShade's windowless OpenVR effect runtime.");
			return;
		}

		const openxr_ui::frame_statistics statistics = openvr_ui_get_frame_statistics();
		if (statistics.revision == 0)
			ImGui::TextUnformatted("-- FPS");
		else
			ImGui::Text("%.0f FPS   %.1f ms", statistics.average_fps, statistics.frame_time_ms);
		char graph_label[32];
		openxr_ui::format_frame_graph_label(statistics, graph_label);
		ImGui::SameLine();
		ImGui::TextDisabled("(graph: %s)", graph_label);
		char cpu_label[48];
		openxr_ui::format_cpu_label(statistics, cpu_label);
		ImGui::TextUnformatted(cpu_label);
		// The size changes only with SteamVR's resolution setting, so ask about every five seconds instead of every draw.
		static uint32_t s_eye_width = 0;
		static uint32_t s_eye_height = 0;
		static uint32_t s_resolution_query_frame = 0;
		if (s_resolution_query_frame++ % 450 == 0)
		{
			if (vr::IVRSystem *const system = get_openvr_interface<vr::IVRSystem>(vr::IVRSystem_Version))
				system->GetRecommendedRenderTargetSize(&s_eye_width, &s_eye_height);
		}
		if (s_eye_width != 0 && s_eye_height != 0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("|  Headset: %ux%u per eye", s_eye_width, s_eye_height);
		}
		openxr_ui::draw_frame_graph(statistics, { ImGui::GetContentRegionAvail().x, 90.0f });
		ImGui::TextUnformatted("FPS target:");
		for (uint32_t divisor = 1; divisor <= openxr_ui::max_frame_rate_divisor; ++divisor)
		{
			ImGui::SameLine();
			if (ImGui::RadioButton(openxr_ui::frame_rate_divisor_name(divisor), statistics.frame_rate_divisor == divisor))
				openxr_ui_set_frame_rate_divisor(divisor);
		}

		const std::shared_ptr<const openxr_ui::technique_snapshot> snapshot = openxr_ui_get_techniques();
		ImGui::Text("Current preset: %s", openxr_ui_get_preset_name().c_str());
		if (ImGui::Button("< Previous"))
			openxr_ui_switch_preset(true);
		ImGui::SameLine();
		if (ImGui::Button("Next >"))
			openxr_ui_switch_preset(false);
		ImGui::SameLine();
		if (ImGui::Button(openxr_ui_is_preset_dirty() ? "Save *" : "Save"))
			openxr_ui_save_preset();
		ImGui::SameLine();
		if (ImGui::Button("Delete"))
			openxr_ui_delete_preset();

		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##save_as", "New preset name", s_save_as_name, sizeof(s_save_as_name));
		ImGui::SameLine();
		if (ImGui::Button("Save As") && s_save_as_name[0] != '\0' && openxr_ui_save_preset_as(s_save_as_name))
			s_save_as_name[0] = '\0';

		if (ImGui::Button("Active first"))
			openxr_ui_move_active_to_top();
		ImGui::SameLine();
		if (ImGui::Button(openxr_ui_are_shaders_temporarily_disabled() ? "Restore shaders" : "Disable all"))
			openxr_ui_toggle_all_techniques();
		ImGui::SameLine();
		ImGui::Checkbox("Show hidden", &s_show_hidden);
		ImGui::SameLine();
		ImGui::Checkbox("Starred only", &s_favourites_only);

		bool performance_mode = openxr_ui_get_performance_mode();
		if (ImGui::Checkbox("Performance mode next start", &performance_mode))
			openxr_ui_set_performance_mode(performance_mode);

		ImGui::Separator();
		if (snapshot->techniques.empty())
		{
			ImGui::TextWrapped("No ReShade techniques found. Check EffectSearchPaths in ReShadeVR.ini or ReShade.ini.");
			return;
		}

		ImGui::BeginChild("##vr_effects", { 0.0f, 0.0f }, ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		for (const openxr_ui::technique &technique : snapshot->techniques)
		{
			if ((!s_show_hidden && technique.hidden) || (s_favourites_only && !technique.favourite))
				continue;

			ImGui::PushID(reinterpret_cast<const void *>(static_cast<uintptr_t>(technique.id)));
			bool enabled = technique.enabled;
			if (ImGui::Checkbox("##enabled", &enabled))
				openxr_ui_toggle_technique(technique.id);
			ImGui::SameLine();
			char label[320];
			std::snprintf(label, sizeof(label), "%s  [%s]", technique.name.c_str(), technique.effect_name.c_str());
			if (ImGui::CollapsingHeader(label))
			{
				if (!technique.tooltip.empty())
					ImGui::TextWrapped("%s", technique.tooltip.c_str());
				if (ImGui::SmallButton("Up"))
					openxr_ui_move_technique(technique.id, true);
				ImGui::SameLine();
				if (ImGui::SmallButton("Down"))
					openxr_ui_move_technique(technique.id, false);
				ImGui::SameLine();
				if (ImGui::SmallButton(technique.favourite ? "Unstar" : "Star"))
					openxr_ui_set_technique_favourite(technique.id, !technique.favourite);
				ImGui::SameLine();
				if (ImGui::SmallButton(technique.hidden ? "Unhide" : "Hide"))
					openxr_ui_set_technique_hidden(technique.id, !technique.hidden);
				ImGui::SameLine();
				if (ImGui::SmallButton("Reset all parameters"))
					openxr_ui_reset_all_parameters(technique.id);
				ImGui::SameLine();
				ImGui::BeginDisabled(!technique.has_restore_state && !technique.differs_from_saved);
				if (ImGui::SmallButton(technique.has_restore_state ? "Restore" : "Revert"))
				{
					if (technique.has_restore_state)
						openxr_ui_restore_technique(technique.id);
					else
						openxr_ui_revert_technique(technique.id);
				}
				ImGui::EndDisabled();
				draw_parameter_controls(*snapshot, technique);
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
	}
}

bool openvr_ui_register_addon(void *module)
{
	if (!reshade::register_addon(module))
		return false;
	reshade::register_overlay(nullptr, draw_openvr_overlay);
	return true;
}

void openvr_ui_unregister_overlay()
{
	reshade::unregister_overlay(nullptr, draw_openvr_overlay);
}

bool openvr_ui_show_dashboard()
{
	vr::IVROverlay *const overlay = get_openvr_interface<vr::IVROverlay>(vr::IVROverlay_Version);
	if (overlay == nullptr)
		return false;
	overlay->ShowDashboard("reshade");
	return true;
}

float openvr_ui_get_display_frequency()
{
	vr::IVRSystem *const system = get_openvr_interface<vr::IVRSystem>(vr::IVRSystem_Version);
	if (system == nullptr)
		return 0.0f;
	vr::ETrackedPropertyError property_error = vr::TrackedProp_Success;
	const float frequency = system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &property_error);
	return property_error == vr::TrackedProp_Success ? frequency : 0.0f;
}

float openvr_ui_get_cpu_ms()
{
	vr::IVRCompositor *const compositor = get_openvr_interface<vr::IVRCompositor>(vr::IVRCompositor_Version);
	if (compositor == nullptr)
		return -1.0f;
	vr::Compositor_FrameTiming timings[45] = {};
	timings[0].m_nSize = sizeof(vr::Compositor_FrameTiming);
	const uint32_t count = compositor->GetFrameTimings(timings, 45);
	float total_ms = 0.0f;
	uint32_t frames = 0;
	// The newest entry may still be in progress.
	for (uint32_t index = 0; index + 1 < count && index < 45; ++index)
	{
		const vr::Compositor_FrameTiming &timing = timings[index];
		if (timing.m_flClientFrameIntervalMs <= 0.0f)
			continue;
		const float wait_ms = (std::max)(0.0f, timing.m_flNewPosesReadyMs - timing.m_flWaitGetPosesCalledMs);
		total_ms += (std::max)(0.0f, timing.m_flClientFrameIntervalMs - wait_ms);
		++frames;
	}
	return frames != 0 ? total_ms / static_cast<float>(frames) : -1.0f;
}