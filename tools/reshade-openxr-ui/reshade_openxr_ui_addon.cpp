#include <reshade.hpp>

#include "openxr_ui_preset.hpp"
#include "vr_ui_state.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace reshade::api;

namespace
{
	std::mutex s_mutex;
	std::unordered_set<effect_runtime *> s_vr_runtimes;
	HMODULE s_module = nullptr;
	bool s_addon_registered = false;
	effect_runtime *s_effect_runtime = nullptr;
	std::shared_ptr<const openxr_ui::technique_snapshot> s_techniques = std::make_shared<openxr_ui::technique_snapshot>();
	std::unordered_set<std::string> s_hidden_techniques;
	bool s_hidden_techniques_loaded = false;
	std::unordered_set<std::string> s_favourite_techniques;
	bool s_favourite_techniques_loaded = false;
	bool s_ui_starts_open = true;
	bool s_ui_settings_loaded = false;
	bool s_performance_mode = false;
	bool s_openvr_runtime_sync_disabled = false;
	bool s_openvr_dashboard_pending = false;
	bool s_shaders_temporarily_disabled = false;
	std::unordered_map<uint64_t, bool> s_temporary_technique_states;
	openxr_ui::preset_dirty_state s_preset_dirty_state;
	openxr_ui::inherited_preset_copy s_inherited_preset_copy;
	openxr_ui::pending_preset_delete s_pending_preset_delete;
	std::atomic<bool> s_runtime_state_dirty = false;
	std::string s_observed_preset_path;
	uint32_t s_empty_inventory_retry_frame = 0;

	std::filesystem::path configuration_directory()
	{
		wchar_t local_app_data[MAX_PATH] = {};
		const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
			return {};

		const std::filesystem::path directory = std::filesystem::path(local_app_data) / L"ReShade VR UI Add-on";
		const std::filesystem::path legacy_directories[] = {
			std::filesystem::path(local_app_data) / L"ReShade OpenXR UI",
			std::filesystem::path(local_app_data) / L"ReShadeOpenXRUI",
			std::filesystem::path(local_app_data) / L"OpenXRUIProbe"
		};
		std::error_code error;
		if (!std::filesystem::exists(directory, error))
		{
			for (const std::filesystem::path &legacy_directory : legacy_directories)
			{
				error.clear();
				if (!std::filesystem::exists(legacy_directory, error))
					continue;
				error.clear();
				std::filesystem::rename(legacy_directory, directory, error);
				if (error)
					return legacy_directory;
				break;
			}
		}
		return directory;
	}

	std::filesystem::path hidden_techniques_path()
	{
		return configuration_directory() / L"hidden-shaders.txt";
	}

	std::filesystem::path favourite_techniques_path()
	{
		return configuration_directory() / L"favourite-shaders.txt";
	}

	std::filesystem::path ui_settings_path()
	{
		return configuration_directory() / L"settings.ini";
	}

	void load_ui_settings()
	{
		if (s_ui_settings_loaded)
			return;
		s_ui_settings_loaded = true;
		const std::filesystem::path path = ui_settings_path();
		if (!path.empty())
			s_ui_starts_open = GetPrivateProfileIntW(L"UI", L"StartsOpen", 1, path.c_str()) != 0;
	}

	void load_technique_identities(std::unordered_set<std::string> &identities, const std::filesystem::path &path)
	{
		std::ifstream stream(path);
		std::string identity;
		while (std::getline(stream, identity))
			if (!identity.empty())
				identities.insert(std::move(identity));
	}

	bool save_technique_identities(const std::unordered_set<std::string> &persisted_identities, const std::filesystem::path &path)
	{
		if (path.empty())
			return false;
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		if (error)
			return false;

		std::vector<std::string> identities(persisted_identities.begin(), persisted_identities.end());
		std::sort(identities.begin(), identities.end());
		const std::filesystem::path temporary_path = path.native() + L".tmp";
		std::ofstream stream(temporary_path, std::ios::trunc);
		for (const std::string &identity : identities)
			stream << identity << '\n';
		stream.close();
		if (!stream)
			return false;
		return MoveFileExW(temporary_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
	}

	void load_hidden_techniques()
	{
		if (s_hidden_techniques_loaded)
			return;
		s_hidden_techniques_loaded = true;
		load_technique_identities(s_hidden_techniques, hidden_techniques_path());
	}

	void load_favourite_techniques()
	{
		if (s_favourite_techniques_loaded)
			return;
		s_favourite_techniques_loaded = true;
		load_technique_identities(s_favourite_techniques, favourite_techniques_path());
	}

	std::string display_name(effect_runtime *runtime, effect_uniform_variable variable, const char *fallback)
	{
		char label[256] = {};
		if (runtime->get_annotation_string_from_uniform_variable(variable, "ui_label", label))
			return label;
		return fallback;
	}

	std::string display_name(effect_runtime *runtime, effect_technique technique, const char *fallback)
	{
		char label[256] = {};
		if (runtime->get_annotation_string_from_technique(technique, "ui_label", label))
			return label;
		return fallback;
	}

	void read_parameter_values(effect_runtime *runtime, effect_uniform_variable variable, openxr_ui::parameter &entry)
	{
		entry.values.resize(static_cast<size_t>(entry.component_count) * entry.array_length);
		for (uint32_t array_index = 0; array_index < entry.array_length; ++array_index)
		{
			const size_t offset = static_cast<size_t>(array_index) * entry.component_count;
			switch (entry.type)
			{
			case openxr_ui::parameter_type::boolean:
				{
					std::unique_ptr<bool[]> values = std::make_unique<bool[]>(entry.component_count);
					runtime->get_uniform_value_bool(variable, values.get(), entry.component_count, array_index);
					for (uint32_t component = 0; component < entry.component_count; ++component)
						entry.values[offset + component] = values[component] ? 1.0 : 0.0;
				}
				break;
			case openxr_ui::parameter_type::signed_integer:
				{
					std::vector<int32_t> values(entry.component_count);
					runtime->get_uniform_value_int(variable, values.data(), values.size(), array_index);
					for (uint32_t component = 0; component < entry.component_count; ++component)
						entry.values[offset + component] = values[component];
				}
				break;
			case openxr_ui::parameter_type::unsigned_integer:
				{
					std::vector<uint32_t> values(entry.component_count);
					runtime->get_uniform_value_uint(variable, values.data(), values.size(), array_index);
					for (uint32_t component = 0; component < entry.component_count; ++component)
						entry.values[offset + component] = values[component];
				}
				break;
			case openxr_ui::parameter_type::floating_point:
				{
					std::vector<float> values(entry.component_count);
					runtime->get_uniform_value_float(variable, values.data(), values.size(), array_index);
					for (uint32_t component = 0; component < entry.component_count; ++component)
						entry.values[offset + component] = values[component];
				}
				break;
			}
		}
	}

	void write_parameter_values(effect_runtime *runtime, effect_uniform_variable variable, const openxr_ui::parameter &entry)
	{
		for (uint32_t array_index = 0; array_index < entry.array_length; ++array_index)
		{
			const size_t offset = static_cast<size_t>(array_index) * entry.component_count;
			switch (entry.type)
			{
			case openxr_ui::parameter_type::boolean:
				{
					std::unique_ptr<bool[]> values = std::make_unique<bool[]>(entry.component_count);
					for (uint32_t component = 0; component < entry.component_count; ++component)
						values[component] = entry.values[offset + component] != 0.0;
					runtime->set_uniform_value_bool(variable, values.get(), entry.component_count, array_index);
				}
				break;
			case openxr_ui::parameter_type::signed_integer:
				{
					std::vector<int32_t> values(entry.component_count);
					for (uint32_t component = 0; component < entry.component_count; ++component)
						values[component] = static_cast<int32_t>(std::llround(entry.values[offset + component]));
					runtime->set_uniform_value_int(variable, values.data(), values.size(), array_index);
				}
				break;
			case openxr_ui::parameter_type::unsigned_integer:
				{
					std::vector<uint32_t> values(entry.component_count);
					for (uint32_t component = 0; component < entry.component_count; ++component)
						values[component] = static_cast<uint32_t>(std::llround(entry.values[offset + component]));
					runtime->set_uniform_value_uint(variable, values.data(), values.size(), array_index);
				}
				break;
			case openxr_ui::parameter_type::floating_point:
				{
					std::vector<float> values(entry.component_count);
					for (uint32_t component = 0; component < entry.component_count; ++component)
						values[component] = static_cast<float>(entry.values[offset + component]);
					runtime->set_uniform_value_float(variable, values.data(), values.size(), array_index);
				}
				break;
			}
		}
	}

	std::string current_preset_path(effect_runtime *runtime)
	{
		if (runtime == nullptr)
			return {};
		size_t path_size = 0;
		runtime->get_current_preset_path(nullptr, &path_size);
		if (path_size <= 1)
			return {};
		std::string path(path_size, '\0');
		runtime->get_current_preset_path(path.data(), &path_size);
		path.resize(path_size);
		return path;
	}

	void log_runtime_message(reshade::log::level level, const char *message)
	{
		char buffer[512];
		std::snprintf(buffer, sizeof(buffer), "ReShade VR UI Add-on: %s", message);
		reshade::log::message(level, buffer);
	}

	void apply_and_save_technique_order(std::shared_ptr<openxr_ui::technique_snapshot> updated)
	{
		std::vector<effect_technique> ordered_techniques;
		ordered_techniques.reserve(updated->techniques.size());
		for (const openxr_ui::technique &entry : updated->techniques)
			ordered_techniques.push_back({ entry.id });
		s_effect_runtime->reorder_techniques(ordered_techniques.size(), ordered_techniques.data());
		s_techniques = std::move(updated);
		openxr_ui::save_current_preset(s_effect_runtime, *s_techniques, s_preset_dirty_state);
	}

	void on_init_effect_runtime(effect_runtime *runtime)
	{
		const bool openxr_loaded = GetModuleHandleW(L"openxr_loader.dll") != nullptr;
		const bool openvr_loaded = GetModuleHandleW(L"openvr_api.dll") != nullptr;
		if (openvr_loaded && !openxr_loaded && !s_openvr_runtime_sync_disabled)
		{
			reshade::set_config_value(nullptr, "ADDON", "SyncEffectRuntimes", false);
			s_openvr_runtime_sync_disabled = true;
			s_openvr_dashboard_pending = true;
			log_runtime_message(reshade::log::level::info, "disabled cross-runtime synchronization for OpenVR to prevent recursive preset and uniform propagation.");
		}
		if ((!openxr_loaded && !openvr_loaded) || runtime->get_hwnd() != nullptr)
			return;

		uint32_t width = 0;
		uint32_t height = 0;
		runtime->get_screenshot_width_and_height(&width, &height);

		{
			const std::lock_guard<std::mutex> lock(s_mutex);
			s_vr_runtimes.insert(runtime);
		}

		char message[256];
		std::snprintf(message, sizeof(message), "observed windowless %s effect runtime %p (%ux%u, %u back buffers).",
			openxr_loaded ? "OpenXR" : "OpenVR", runtime, width, height, runtime->get_back_buffer_count());
		log_runtime_message(reshade::log::level::info, message);
	}

	void on_destroy_effect_runtime(effect_runtime *runtime)
	{
		const std::lock_guard<std::mutex> lock(s_mutex);
		s_vr_runtimes.erase(runtime);
		if (s_effect_runtime == runtime)
		{
			s_inherited_preset_copy.cancel(runtime);
			s_pending_preset_delete.cancel(runtime);
			s_techniques = std::make_shared<openxr_ui::technique_snapshot>();
			s_effect_runtime = nullptr;
			s_preset_dirty_state.clear();
			s_temporary_technique_states.clear();
			s_shaders_temporarily_disabled = false;
			s_observed_preset_path.clear();
			s_runtime_state_dirty.store(false, std::memory_order_release);
			s_empty_inventory_retry_frame = 0;
		}
	}

	void on_reshade_reloaded_effects(effect_runtime *runtime)
	{
		const std::lock_guard<std::mutex> lock(s_mutex);
		if (s_vr_runtimes.find(runtime) == s_vr_runtimes.end())
			return;
		load_hidden_techniques();
		load_favourite_techniques();

		struct inventory
		{
			size_t enabled_technique_count = 0;
			size_t uniform_count = 0;
			std::shared_ptr<openxr_ui::technique_snapshot> snapshot = std::make_shared<openxr_ui::technique_snapshot>();
		} inventory;
		runtime->enumerate_techniques(nullptr, [&inventory](effect_runtime *current_runtime, effect_technique technique) {
			int32_t hidden = 0;
			current_runtime->get_annotation_int_from_technique(technique, "hidden", &hidden, 1);
			if (hidden != 0)
				return;

			openxr_ui::technique entry;
			entry.id = technique.handle;
			entry.enabled = current_runtime->get_technique_state(technique);
			if (entry.enabled)
				++inventory.enabled_technique_count;

			char technique_name[128] = {};
			char effect_name[128] = {};
			current_runtime->get_technique_name(technique, technique_name);
			current_runtime->get_technique_effect_name(technique, effect_name);
			const char *effect_file_name = effect_name;
			for (const char *cursor = effect_name; *cursor != '\0'; ++cursor)
				if (*cursor == '\\' || *cursor == '/')
					effect_file_name = cursor + 1;
			entry.name = display_name(current_runtime, technique, technique_name);
			char tooltip[1024] = {};
			if (current_runtime->get_annotation_string_from_technique(technique, "ui_tooltip", tooltip))
				entry.tooltip = tooltip;
			entry.effect_name = effect_file_name;
			entry.persistence_id = entry.effect_name + "::" + technique_name;
			entry.hidden = s_hidden_techniques.find(entry.persistence_id) != s_hidden_techniques.end();
			entry.favourite = s_favourite_techniques.find(entry.persistence_id) != s_favourite_techniques.end();
			inventory.snapshot->techniques.push_back(std::move(entry));
		});

		runtime->enumerate_uniform_variables(nullptr, [&inventory](effect_runtime *current_runtime, effect_uniform_variable variable) {
			++inventory.uniform_count;
			int32_t hidden = 0;
			uint32_t no_edit = 0;
			current_runtime->get_annotation_int_from_uniform_variable(variable, "hidden", &hidden, 1);
			current_runtime->get_annotation_uint_from_uniform_variable(variable, "noedit", &no_edit, 1);
			if (hidden != 0 || no_edit != 0)
				return;

			format base_type = format::unknown;
			uint32_t rows = 0, columns = 0, array_length = 0;
			current_runtime->get_uniform_variable_type(variable, &base_type, &rows, &columns, &array_length);
			openxr_ui::parameter entry;
			entry.id = variable.handle;
			entry.component_count = rows * columns;
			entry.array_length = (std::max)(1u, array_length);
			if (entry.component_count == 0)
				return;

			switch (base_type)
			{
			case format::r32_typeless:
				entry.type = openxr_ui::parameter_type::boolean;
				entry.step = 1.0;
				entry.minimum = 0.0;
				entry.maximum = 1.0;
				break;
			case format::r16_sint:
			case format::r32_sint:
				entry.type = openxr_ui::parameter_type::signed_integer;
				entry.step = 1.0;
				entry.minimum = std::numeric_limits<int32_t>::lowest();
				entry.maximum = (std::numeric_limits<int32_t>::max)();
				break;
			case format::r16_uint:
			case format::r32_uint:
				entry.type = openxr_ui::parameter_type::unsigned_integer;
				entry.step = 1.0;
				entry.minimum = 0.0;
				entry.maximum = (std::numeric_limits<uint32_t>::max)();
				break;
			case format::r16_float:
			case format::r32_float:
				entry.type = openxr_ui::parameter_type::floating_point;
				break;
			default:
				return;
			}

			char variable_name[128] = {};
			char effect_name[128] = {};
			current_runtime->get_uniform_variable_name(variable, variable_name);
			current_runtime->get_uniform_variable_effect_name(variable, effect_name);
			entry.name = display_name(current_runtime, variable, variable_name);
			char tooltip[1024] = {};
			if (current_runtime->get_annotation_string_from_uniform_variable(variable, "ui_tooltip", tooltip))
				entry.tooltip = tooltip;
			entry.effect_name = effect_name;
			uint32_t no_reset = 0;
			current_runtime->get_annotation_uint_from_uniform_variable(variable, "noreset", &no_reset, 1);
			entry.resettable = no_reset == 0;

			if (entry.type == openxr_ui::parameter_type::floating_point)
			{
				float annotation = 0.0f;
				if (current_runtime->get_annotation_float_from_uniform_variable(variable, "ui_min", &annotation, 1))
					entry.minimum = annotation;
				if (current_runtime->get_annotation_float_from_uniform_variable(variable, "ui_max", &annotation, 1))
					entry.maximum = annotation;
				if (current_runtime->get_annotation_float_from_uniform_variable(variable, "ui_step", &annotation, 1) && annotation > 0.0f)
					entry.step = annotation;
			}
			else if (entry.type != openxr_ui::parameter_type::boolean)
			{
				int32_t annotation = 0;
				if (current_runtime->get_annotation_int_from_uniform_variable(variable, "ui_min", &annotation, 1))
					entry.minimum = annotation;
				if (current_runtime->get_annotation_int_from_uniform_variable(variable, "ui_max", &annotation, 1))
					entry.maximum = annotation;
				if (current_runtime->get_annotation_int_from_uniform_variable(variable, "ui_step", &annotation, 1) && annotation > 0)
					entry.step = annotation;
			}
			read_parameter_values(current_runtime, variable, entry);
			if (entry.resettable)
			{
				current_runtime->reset_uniform_value(variable);
				openxr_ui::parameter defaults = entry;
				read_parameter_values(current_runtime, variable, defaults);
				entry.default_values = std::move(defaults.values);
				write_parameter_values(current_runtime, variable, entry);
			}
			inventory.snapshot->parameters.push_back(std::move(entry));
		});
		openxr_ui::update_technique_default_states(*inventory.snapshot);

		char message[512];
		std::snprintf(
			message,
			sizeof(message),
			"enumerated %zu techniques (%zu enabled) and %zu uniforms.",
			inventory.snapshot->techniques.size(),
			inventory.enabled_technique_count,
			inventory.uniform_count);
		log_runtime_message(reshade::log::level::info, message);

		s_effect_runtime = runtime;
		reshade::get_config_value(runtime, "GENERAL", "PerformanceMode", s_performance_mode);
		s_techniques = std::move(inventory.snapshot);
		s_observed_preset_path = current_preset_path(runtime);
		if (!s_techniques->techniques.empty())
			s_empty_inventory_retry_frame = 0;
		if (!s_shaders_temporarily_disabled)
			s_preset_dirty_state.mark_saved(*s_techniques);
		s_runtime_state_dirty.store(false, std::memory_order_release);
		log_runtime_message(reshade::log::level::info, "scrollable technique model ready for controller input.");
	}

	bool synchronize_runtime_model(effect_runtime *runtime, const std::string &preset_path)
	{
		const std::lock_guard<std::mutex> lock(s_mutex);
		if (runtime != s_effect_runtime)
			return true;

		std::vector<openxr_ui::runtime_technique_state> technique_states;
		runtime->enumerate_techniques(nullptr, [&technique_states](effect_runtime *current_runtime, effect_technique technique) {
			technique_states.push_back({ technique.handle, current_runtime->get_technique_state(technique) });
		});
		std::vector<openxr_ui::runtime_parameter_state> parameter_states;
		runtime->enumerate_uniform_variables(nullptr, [&parameter_states, &snapshot = *s_techniques](effect_runtime *current_runtime, effect_uniform_variable variable) {
			const auto match = std::find_if(snapshot.parameters.begin(), snapshot.parameters.end(), [variable](const openxr_ui::parameter &entry) {
				return entry.id == variable.handle;
			});
			if (match == snapshot.parameters.end())
				return;
			openxr_ui::parameter live_parameter = *match;
			read_parameter_values(current_runtime, variable, live_parameter);
			parameter_states.push_back({ variable.handle, std::move(live_parameter.values) });
		});
		if ((!s_techniques->techniques.empty() && technique_states.empty()) ||
			(!s_techniques->parameters.empty() && parameter_states.empty()))
			return false;

		auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
		const bool changed = openxr_ui::reconcile_runtime_state(*updated, technique_states, parameter_states);
		if (changed)
			s_techniques = std::move(updated);

		if (preset_path != s_observed_preset_path)
		{
			s_observed_preset_path = preset_path;
			s_temporary_technique_states.clear();
			s_shaders_temporarily_disabled = false;
			s_preset_dirty_state.mark_saved(*s_techniques);
		}
		else if (changed && !s_shaders_temporarily_disabled)
		{
			s_preset_dirty_state.update(*s_techniques);
		}
		return true;
	}

	void on_reshade_set_current_preset_path(effect_runtime *, const char *)
	{
		s_runtime_state_dirty.store(true, std::memory_order_release);
	}

	bool on_reshade_set_uniform_value(effect_runtime *, effect_uniform_variable, const void *, size_t)
	{
		s_runtime_state_dirty.store(true, std::memory_order_release);
		return false;
	}

	bool on_reshade_set_technique_state(effect_runtime *, effect_technique, bool)
	{
		s_runtime_state_dirty.store(true, std::memory_order_release);
		return false;
	}

	bool on_reshade_reorder_techniques(effect_runtime *, size_t, effect_technique *)
	{
		s_runtime_state_dirty.store(true, std::memory_order_release);
		return false;
	}

	void on_reshade_present(effect_runtime *runtime)
	{
		const std::string preset_path = current_preset_path(runtime);
		bool synchronize_model = s_runtime_state_dirty.exchange(false, std::memory_order_acq_rel);
		bool retry_inventory = false;
		{
			const std::lock_guard<std::mutex> lock(s_mutex);
			if (runtime == s_effect_runtime)
			{
				synchronize_model = synchronize_model || preset_path != s_observed_preset_path;
				if (s_techniques->techniques.empty() && s_empty_inventory_retry_frame < 600)
					retry_inventory = ++s_empty_inventory_retry_frame % 10 == 0;
			}
		}
		if (retry_inventory)
			on_reshade_reloaded_effects(runtime);
		else if (synchronize_model && !synchronize_runtime_model(runtime, preset_path))
			s_runtime_state_dirty.store(true, std::memory_order_release);

#ifdef RESHADE_OPENVR_FRONTEND
		bool show_dashboard = false;
		{
			const std::lock_guard<std::mutex> lock(s_mutex);
			show_dashboard = runtime == s_effect_runtime && s_openvr_dashboard_pending && !s_techniques->techniques.empty();
			if (show_dashboard)
				s_openvr_dashboard_pending = false;
		}
		if (show_dashboard)
		{
			const bool dashboard_shown = openvr_ui_show_dashboard();
			log_runtime_message(dashboard_shown ? reshade::log::level::info : reshade::log::level::error,
				dashboard_shown ? "requested the ReShade SteamVR dashboard after VR effects became ready." : "failed to show the ReShade SteamVR dashboard.");
		}
#endif
	}

	bool try_register_addon()
	{
		const std::lock_guard<std::mutex> lock(s_mutex);
		if (s_addon_registered)
			return true;
		if (s_module == nullptr)
			return false;
#ifdef RESHADE_OPENVR_FRONTEND
		if (!openvr_ui_register_addon(s_module))
#else
		if (!reshade::register_addon(s_module))
#endif
			return false;

		reshade::register_event<reshade::addon_event::init_effect_runtime>(on_init_effect_runtime);
		reshade::register_event<reshade::addon_event::destroy_effect_runtime>(on_destroy_effect_runtime);
		reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(on_reshade_reloaded_effects);
		reshade::register_event<reshade::addon_event::reshade_set_uniform_value>(on_reshade_set_uniform_value);
		reshade::register_event<reshade::addon_event::reshade_set_technique_state>(on_reshade_set_technique_state);
		reshade::register_event<reshade::addon_event::reshade_reorder_techniques>(on_reshade_reorder_techniques);
		reshade::register_event<reshade::addon_event::reshade_set_current_preset_path>(on_reshade_set_current_preset_path);
		reshade::register_event<reshade::addon_event::reshade_present>(on_reshade_present);
		s_addon_registered = true;
		return true;
	}
}

#ifdef RESHADE_OPENVR_FRONTEND
extern "C" __declspec(dllexport) const char *NAME = "ReShade VR UI Add-on for SteamVR";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Provides the SteamVR frontend of ReShade VR UI Add-on for controlling ReShade effects in OpenVR applications.";
#else
extern "C" __declspec(dllexport) const char *NAME = "ReShade VR UI Add-on";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Provides the native OpenXR frontend of ReShade VR UI Add-on for controlling ReShade effects.";
#endif

bool openxr_ui_ensure_addon_registered()
{
	return try_register_addon();
}

void openxr_ui_log_message(const char *message, bool error)
{
	OutputDebugStringA(message);
	OutputDebugStringA("\n");
	if (s_addon_registered)
		log_runtime_message(error ? reshade::log::level::error : reshade::log::level::info, message);
}

std::shared_ptr<const openxr_ui::technique_snapshot> openxr_ui_get_techniques()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return s_techniques;
}

bool openxr_ui_has_effect_runtime()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return s_effect_runtime != nullptr;
}

bool openxr_ui_uses_effect_runtime(effect_runtime *runtime)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return runtime != nullptr && runtime == s_effect_runtime;
}

bool openxr_ui_toggle_technique(uint64_t technique_id)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || technique_id == 0 || s_shaders_temporarily_disabled)
		return false;
	const auto technique_it = std::find_if(s_techniques->techniques.begin(), s_techniques->techniques.end(), [technique_id](const openxr_ui::technique &entry) {
		return entry.id == technique_id;
	});
	if (technique_it == s_techniques->techniques.end())
		return false;

	const size_t technique_index = static_cast<size_t>(technique_it - s_techniques->techniques.begin());
	const effect_technique technique = { technique_id };
	const bool enabled = !s_effect_runtime->get_technique_state(technique);
	s_effect_runtime->set_technique_state(technique, enabled);
	const bool changed = s_effect_runtime->get_technique_state(technique) == enabled;

	char message[512];
	std::snprintf(message, sizeof(message), "controller %s %s::%s.",
		changed ? (enabled ? "enabled" : "disabled") : "failed to toggle",
		technique_it->effect_name.c_str(), technique_it->name.c_str());
	log_runtime_message(changed ? reshade::log::level::info : reshade::log::level::error, message);
	if (changed)
	{
		auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
		updated->techniques[technique_index].enabled = enabled;
		s_techniques = std::move(updated);
		s_preset_dirty_state.update(*s_techniques);
	}
	return changed;
}

bool openxr_ui_are_shaders_temporarily_disabled()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return s_shaders_temporarily_disabled;
}

bool openxr_ui_toggle_all_techniques()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || s_techniques->techniques.empty())
		return false;

	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	if (!s_shaders_temporarily_disabled)
	{
		s_temporary_technique_states.clear();
		for (openxr_ui::technique &entry : updated->techniques)
		{
			s_temporary_technique_states.emplace(entry.id, entry.enabled);
			if (entry.enabled)
			{
				s_effect_runtime->set_technique_state({ entry.id }, false);
				entry.enabled = s_effect_runtime->get_technique_state({ entry.id });
			}
		}
		s_shaders_temporarily_disabled = true;
		log_runtime_message(reshade::log::level::info, "controller temporarily disabled all shaders without saving the preset.");
	}
	else
	{
		for (openxr_ui::technique &entry : updated->techniques)
		{
			const auto saved_it = s_temporary_technique_states.find(entry.id);
			if (saved_it == s_temporary_technique_states.end())
				continue;
			s_effect_runtime->set_technique_state({ entry.id }, saved_it->second);
			entry.enabled = s_effect_runtime->get_technique_state({ entry.id });
		}
		s_temporary_technique_states.clear();
		s_shaders_temporarily_disabled = false;
		log_runtime_message(reshade::log::level::info, "controller restored the shader states captured before temporary disable.");
	}
	s_techniques = std::move(updated);
	return true;
}

bool openxr_ui_is_preset_dirty()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return s_preset_dirty_state.is_dirty();
}

void openxr_ui_move_active_to_top()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || s_shaders_temporarily_disabled)
		return;

	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	openxr_ui::move_enabled_techniques_to_top(*updated);
	apply_and_save_technique_order(std::move(updated));
	log_runtime_message(reshade::log::level::info, "controller moved active shaders to the top and saved ReShade's current preset.");
}

bool openxr_ui_move_technique(uint64_t technique_id, bool move_up)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || s_shaders_temporarily_disabled)
		return false;

	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	if (!openxr_ui::move_technique(*updated, technique_id, move_up))
		return false;
	apply_and_save_technique_order(std::move(updated));
	log_runtime_message(reshade::log::level::info, move_up ?
		"controller moved a shader up and saved ReShade's current preset." :
		"controller moved a shader down and saved ReShade's current preset.");
	return true;
}

bool openxr_ui_set_technique_hidden(uint64_t technique_id, bool hidden)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	const auto technique_it = std::find_if(s_techniques->techniques.begin(), s_techniques->techniques.end(), [technique_id](const openxr_ui::technique &entry) {
		return entry.id == technique_id;
	});
	if (technique_it == s_techniques->techniques.end() || technique_it->persistence_id.empty() || technique_it->hidden == hidden)
		return false;

	if (hidden)
		s_hidden_techniques.insert(technique_it->persistence_id);
	else
		s_hidden_techniques.erase(technique_it->persistence_id);
	if (!save_technique_identities(s_hidden_techniques, hidden_techniques_path()))
	{
		if (hidden)
			s_hidden_techniques.erase(technique_it->persistence_id);
		else
			s_hidden_techniques.insert(technique_it->persistence_id);
		log_runtime_message(reshade::log::level::error, "failed to save the global hidden-shader list.");
		return false;
	}

	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	updated->techniques[static_cast<size_t>(technique_it - s_techniques->techniques.begin())].hidden = hidden;
	s_techniques = std::move(updated);
	log_runtime_message(reshade::log::level::info, hidden ? "controller hid a shader globally." : "controller unhid a shader globally.");
	return true;
}

bool openxr_ui_set_technique_favourite(uint64_t technique_id, bool favourite)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	const auto technique_it = std::find_if(s_techniques->techniques.begin(), s_techniques->techniques.end(), [technique_id](const openxr_ui::technique &entry) {
		return entry.id == technique_id;
	});
	if (technique_it == s_techniques->techniques.end() || technique_it->persistence_id.empty() || technique_it->favourite == favourite)
		return false;

	if (favourite)
		s_favourite_techniques.insert(technique_it->persistence_id);
	else
		s_favourite_techniques.erase(technique_it->persistence_id);
	if (!save_technique_identities(s_favourite_techniques, favourite_techniques_path()))
	{
		if (favourite)
			s_favourite_techniques.erase(technique_it->persistence_id);
		else
			s_favourite_techniques.insert(technique_it->persistence_id);
		log_runtime_message(reshade::log::level::error, "failed to save the global favourite-shader list.");
		return false;
	}

	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	updated->techniques[static_cast<size_t>(technique_it - s_techniques->techniques.begin())].favourite = favourite;
	s_techniques = std::move(updated);
	log_runtime_message(reshade::log::level::info, favourite ? "controller favourited a shader globally." : "controller unfavourited a shader globally.");
	return true;
}

bool openxr_ui_get_ui_starts_open()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	load_ui_settings();
	return s_ui_starts_open;
}

bool openxr_ui_set_ui_starts_open(bool starts_open)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	const std::filesystem::path path = ui_settings_path();
	if (path.empty())
		return false;
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	if (error || !WritePrivateProfileStringW(L"UI", L"StartsOpen", starts_open ? L"1" : L"0", path.c_str()))
	{
		log_runtime_message(reshade::log::level::error, "failed to save the UI startup setting.");
		return false;
	}
	s_ui_starts_open = starts_open;
	s_ui_settings_loaded = true;
	log_runtime_message(reshade::log::level::info, starts_open ? "UI will start open." : "UI will start closed.");
	return true;
}

bool openxr_ui_get_performance_mode()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return s_performance_mode;
}

bool openxr_ui_set_performance_mode(bool enabled)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr)
		return false;
	reshade::set_config_value(s_effect_runtime, "GENERAL", "PerformanceMode", enabled);
	bool saved_value = !enabled;
	if (!reshade::get_config_value(s_effect_runtime, "GENERAL", "PerformanceMode", saved_value) || saved_value != enabled)
	{
		log_runtime_message(reshade::log::level::error, "failed to save ReShade performance mode.");
		return false;
	}
	s_performance_mode = enabled;
	log_runtime_message(reshade::log::level::info,
		enabled ? "ReShade performance mode will be enabled on next start." : "ReShade performance mode will be disabled on next start.");
	return true;
}

bool openxr_ui_set_parameter(uint64_t parameter_id, uint32_t component, double value)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || parameter_id == 0)
		return false;
	const auto parameter_it = std::find_if(s_techniques->parameters.begin(), s_techniques->parameters.end(), [parameter_id](const openxr_ui::parameter &entry) {
		return entry.id == parameter_id;
	});
	if (parameter_it == s_techniques->parameters.end() || component >= parameter_it->values.size())
		return false;

	const size_t parameter_index = static_cast<size_t>(parameter_it - s_techniques->parameters.begin());
	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	openxr_ui::parameter &parameter = updated->parameters[parameter_index];
	value = std::clamp(value, parameter.minimum, parameter.maximum);
	parameter.values[component] = value;
	const uint32_t array_index = component / parameter.component_count;
	const size_t offset = static_cast<size_t>(array_index) * parameter.component_count;
	const effect_uniform_variable variable = { parameter_id };
	switch (parameter.type)
	{
	case openxr_ui::parameter_type::boolean:
		{
			std::unique_ptr<bool[]> values = std::make_unique<bool[]>(parameter.component_count);
			for (uint32_t index = 0; index < parameter.component_count; ++index)
				values[index] = parameter.values[offset + index] != 0.0;
			s_effect_runtime->set_uniform_value_bool(variable, values.get(), parameter.component_count, array_index);
		}
		break;
	case openxr_ui::parameter_type::signed_integer:
		{
			std::vector<int32_t> values(parameter.component_count);
			for (uint32_t index = 0; index < parameter.component_count; ++index)
				values[index] = static_cast<int32_t>(std::llround(parameter.values[offset + index]));
			s_effect_runtime->set_uniform_value_int(variable, values.data(), values.size(), array_index);
		}
		break;
	case openxr_ui::parameter_type::unsigned_integer:
		{
			std::vector<uint32_t> values(parameter.component_count);
			for (uint32_t index = 0; index < parameter.component_count; ++index)
				values[index] = static_cast<uint32_t>(std::llround(parameter.values[offset + index]));
			s_effect_runtime->set_uniform_value_uint(variable, values.data(), values.size(), array_index);
		}
		break;
	case openxr_ui::parameter_type::floating_point:
		{
			std::vector<float> values(parameter.component_count);
			for (uint32_t index = 0; index < parameter.component_count; ++index)
				values[index] = static_cast<float>(parameter.values[offset + index]);
			s_effect_runtime->set_uniform_value_float(variable, values.data(), values.size(), array_index);
		}
		break;
	}
	openxr_ui::update_technique_default_states(*updated);
	s_techniques = std::move(updated);
	s_preset_dirty_state.update(*s_techniques);
	return true;
}

bool openxr_ui_reset_parameter(uint64_t parameter_id)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || parameter_id == 0)
		return false;
	const auto parameter_it = std::find_if(s_techniques->parameters.begin(), s_techniques->parameters.end(), [parameter_id](const openxr_ui::parameter &entry) {
		return entry.id == parameter_id;
	});
	if (parameter_it == s_techniques->parameters.end() || !parameter_it->resettable)
		return false;

	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	openxr_ui::parameter &parameter = updated->parameters[static_cast<size_t>(parameter_it - s_techniques->parameters.begin())];
	s_effect_runtime->reset_uniform_value({ parameter_id });
	read_parameter_values(s_effect_runtime, { parameter_id }, parameter);
	openxr_ui::update_technique_default_states(*updated);
	s_techniques = std::move(updated);
	s_preset_dirty_state.update(*s_techniques);
	return true;
}

bool openxr_ui_reset_all_parameters(uint64_t technique_id)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || technique_id == 0)
		return false;
	const auto technique_it = std::find_if(s_techniques->techniques.begin(), s_techniques->techniques.end(), [technique_id](const openxr_ui::technique &entry) {
		return entry.id == technique_id;
	});
	if (technique_it == s_techniques->techniques.end())
		return false;

	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	bool reset_any = false;
	for (openxr_ui::parameter &parameter : updated->parameters)
	{
		if (parameter.effect_name != technique_it->effect_name || !parameter.resettable)
			continue;
		s_effect_runtime->reset_uniform_value({ parameter.id });
		read_parameter_values(s_effect_runtime, { parameter.id }, parameter);
		reset_any = true;
	}
	if (reset_any)
	{
		openxr_ui::update_technique_default_states(*updated);
		s_techniques = std::move(updated);
		s_preset_dirty_state.update(*s_techniques);
	}
	return reset_any;
}

bool openxr_ui_save_preset()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || s_shaders_temporarily_disabled)
		return false;
	openxr_ui::save_current_preset(s_effect_runtime, *s_techniques, s_preset_dirty_state);
	log_runtime_message(reshade::log::level::info, "controller saved all changes to the current preset.");
	return true;
}

std::string openxr_ui_get_preset_name()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return openxr_ui::current_preset_name(s_effect_runtime);
}

bool openxr_ui_switch_preset(bool previous)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_shaders_temporarily_disabled || s_inherited_preset_copy.is_pending() || s_pending_preset_delete.is_pending() ||
		!openxr_ui::switch_to_sibling_preset(s_effect_runtime, previous))
		return false;
	log_runtime_message(reshade::log::level::info, previous ?
		"controller switched to the previous ReShade preset." :
		"controller switched to the next ReShade preset.");
	return true;
}

bool openxr_ui_save_preset_as(const std::string &preset_name)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_shaders_temporarily_disabled || s_pending_preset_delete.is_pending() ||
		!s_inherited_preset_copy.begin(s_effect_runtime, preset_name, s_preset_dirty_state.is_dirty()))
		return false;
	log_runtime_message(reshade::log::level::info, "controller queued an inherited Save As after ReShade flushes the current preset.");
	return true;
}

bool openxr_ui_delete_preset()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_shaders_temporarily_disabled || s_inherited_preset_copy.is_pending() ||
		!s_pending_preset_delete.begin(s_effect_runtime))
		return false;
	log_runtime_message(reshade::log::level::info, "controller switched presets and queued deletion after ReShade flushes the outgoing preset.");
	return true;
}

int openxr_ui_update_preset_operations()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	switch (s_pending_preset_delete.update(s_effect_runtime))
	{
	case openxr_ui::preset_delete_status::deleted:
		log_runtime_message(reshade::log::level::info, "controller deleted the outgoing ReShade preset after its cached state was flushed.");
		break;
	case openxr_ui::preset_delete_status::delete_failed:
		log_runtime_message(reshade::log::level::error, "failed to delete the outgoing ReShade preset after switching to a sibling preset.");
		break;
	case openxr_ui::preset_delete_status::none:
		break;
	}
	const openxr_ui::preset_copy_result result = s_inherited_preset_copy.update(s_effect_runtime);
	switch (result.status)
	{
	case openxr_ui::preset_copy_status::created:
		{
			char message[512];
			std::snprintf(message, sizeof(message), "controller created and switched to inherited preset '%s'.", result.destination.c_str());
			log_runtime_message(reshade::log::level::info, message);
		}
		return 1;
	case openxr_ui::preset_copy_status::flush_failed:
		log_runtime_message(reshade::log::level::error, "ReShade did not flush the modified preset, so Save As was cancelled.");
		return -1;
	case openxr_ui::preset_copy_status::copy_failed:
		log_runtime_message(reshade::log::level::error, "failed to copy the current ReShade preset for Save As.");
		return -1;
	case openxr_ui::preset_copy_status::preset_rejected:
		log_runtime_message(reshade::log::level::error, "ReShade rejected the newly copied preset path.");
		return -1;
	case openxr_ui::preset_copy_status::none:
		break;
	}
	return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		s_module = module;
		DisableThreadLibraryCalls(module);
		try_register_addon();
		break;
	case DLL_PROCESS_DETACH:
		if (s_addon_registered)
		{
			reshade::unregister_event<reshade::addon_event::init_effect_runtime>(on_init_effect_runtime);
			reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(on_destroy_effect_runtime);
			reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(on_reshade_reloaded_effects);
			reshade::unregister_event<reshade::addon_event::reshade_set_uniform_value>(on_reshade_set_uniform_value);
			reshade::unregister_event<reshade::addon_event::reshade_set_technique_state>(on_reshade_set_technique_state);
			reshade::unregister_event<reshade::addon_event::reshade_reorder_techniques>(on_reshade_reorder_techniques);
			reshade::unregister_event<reshade::addon_event::reshade_set_current_preset_path>(on_reshade_set_current_preset_path);
			reshade::unregister_event<reshade::addon_event::reshade_present>(on_reshade_present);
#ifdef RESHADE_OPENVR_FRONTEND
			openvr_ui_unregister_overlay();
#endif
			reshade::unregister_addon(module);
		}
		break;
	}

	return TRUE;
}