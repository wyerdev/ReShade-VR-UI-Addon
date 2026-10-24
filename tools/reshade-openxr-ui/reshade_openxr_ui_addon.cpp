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
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace reshade::api;

namespace
{
	std::mutex s_mutex;
	std::unordered_set<effect_runtime *> s_vr_runtimes;
	std::unordered_set<effect_runtime *> s_desktop_runtimes;
	// Monitor runtimes whose effects this add-on switched off while the headset has its own effect runtime.
	std::unordered_set<effect_runtime *> s_paused_desktop_runtimes;
	bool s_default_values_pending = false;
	HMODULE s_module = nullptr;
	bool s_addon_registered = false;
	effect_runtime *s_effect_runtime = nullptr;
	std::shared_ptr<const openxr_ui::technique_snapshot> s_techniques = std::make_shared<openxr_ui::technique_snapshot>();
	std::unordered_set<std::string> s_hidden_techniques;
	bool s_hidden_techniques_loaded = false;
	std::unordered_set<std::string> s_favourite_techniques;
	bool s_favourite_techniques_loaded = false;
	bool s_ui_starts_open = true;
	bool s_always_show_statistics = false;
	uint32_t s_frame_rate_divisor = 1;
	bool s_ui_settings_loaded = false;
	bool s_performance_mode = false;
	bool s_performance_mode_at_start = false;
	constexpr char generic_depth_addon_name[] = "Generic Depth";
	bool s_depth_detection_loaded = false;
	bool s_depth_detection_disabled = false;
	bool s_depth_detection_disabled_at_start = false;
	bool s_openvr_runtime_sync_disabled = false;
	bool s_openvr_dashboard_pending = false;
	bool s_shaders_temporarily_disabled = false;
	std::unordered_map<uint64_t, bool> s_temporary_technique_states;
	struct technique_restore_state
	{
		bool enabled = false;
		std::unordered_map<uint64_t, std::vector<double>> parameter_values;
	};
	std::unordered_map<uint64_t, technique_restore_state> s_technique_restore_states;
	openxr_ui::preset_dirty_state s_preset_dirty_state;
	openxr_ui::inherited_preset_copy s_inherited_preset_copy;
	openxr_ui::pending_preset_delete s_pending_preset_delete;
	std::atomic<bool> s_runtime_state_dirty = false;
	std::string s_observed_preset_path;
	uint32_t s_empty_inventory_retry_frame = 0;
	struct effect_subfolder_scan
	{
		// EffectSearchPaths with "\**" added to every folder that hides effects.
		std::vector<std::string> search_paths;
		std::vector<std::string> effect_names;
		uint32_t folder_count = 0;
	};
	openxr_ui::effect_subfolder_state s_effect_subfolder_state = openxr_ui::effect_subfolder_state::none;
	effect_subfolder_scan s_effect_subfolder_scan;
	effect_runtime *s_effect_subfolder_scanned_runtime = nullptr;
	bool s_effect_subfolder_notice_dismissed = false;
	bool s_effect_subfolder_reload_started = false;
	uint32_t s_effect_subfolder_wait_frames = 0;
	std::chrono::steady_clock::time_point s_effect_subfolder_requested_at;
	std::atomic<bool> s_effect_subfolder_scan_pending = false;

	// Values written by something other than this panel (a mod, ReShade's own clock) are counted per frame.
	// A value written in this many separate frames is treated as set by the game.
	constexpr uint32_t set_by_game_frame_threshold = 60;
	struct external_write_count
	{
		uint64_t last_frame = 0;
		uint32_t frames = 0;
	};
	// Separate from s_mutex: ReShade reports writes synchronously, also while the panel holds s_mutex.
	std::mutex s_external_write_mutex;
	std::unordered_map<uint64_t, external_write_count> s_external_writes;
	std::atomic<uint64_t> s_present_frame = 0;
	std::atomic<bool> s_reshade_overlay_open = false;
	thread_local uint32_t t_panel_write_depth = 0;
	struct panel_write_scope
	{
		panel_write_scope() { ++t_panel_write_depth; }
		~panel_write_scope() { --t_panel_write_depth; }
		panel_write_scope(const panel_write_scope &) = delete;
		panel_write_scope &operator=(const panel_write_scope &) = delete;
	};

	bool is_set_by_game(uint64_t parameter_id)
	{
		const std::lock_guard<std::mutex> lock(s_external_write_mutex);
		const auto it = s_external_writes.find(parameter_id);
		return it != s_external_writes.end() && it->second.frames >= set_by_game_frame_threshold;
	}

	bool mark_parameters_set_by_game(openxr_ui::technique_snapshot &snapshot)
	{
		bool changed = false;
		for (openxr_ui::parameter &entry : snapshot.parameters)
		{
			const bool set_by_game = is_set_by_game(entry.id);
			changed = changed || set_by_game != entry.set_by_game;
			entry.set_by_game = set_by_game;
		}
		return changed;
	}
#ifdef RESHADE_OPENVR_FRONTEND
	// ReShade presents its OpenVR runtime once per frame, after the right eye.
	openxr_ui::frame_statistics_tracker s_openvr_frame_timing;
	uint32_t s_openvr_refresh_query_frame = 0;
#endif

#ifndef RESHADE_OPENVR_FRONTEND
#pragma comment(lib, "advapi32.lib")

	constexpr int own_reshade_unknown = 0;
	constexpr int own_reshade_no = 1;
	constexpr int own_reshade_yes = 2;
	std::atomic<int> s_own_reshade_state = own_reshade_unknown;
	std::mutex s_own_reshade_mutex;

	std::filesystem::path module_path(HMODULE module)
	{
		std::wstring path(32768, L'\0');
		const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0 || length >= path.size())
			return {};
		path.resize(length);
		return path;
	}

	// The "library_path" of an OpenXR API-layer manifest; the loader resolves a relative path from the manifest's folder.
	std::filesystem::path read_layer_library_path(const std::filesystem::path &manifest)
	{
		std::ifstream file(manifest, std::ios::binary);
		if (!file)
			return {};
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		const size_t key = text.find("\"library_path\"");
		if (key == std::string::npos)
			return {};
		const size_t colon = text.find(':', key + 14);
		if (colon == std::string::npos)
			return {};
		size_t position = text.find('"', colon + 1);
		if (position == std::string::npos)
			return {};
		std::string value;
		for (++position; position < text.size() && text[position] != '"'; ++position)
		{
			if (text[position] == '\\' && position + 1 < text.size())
				++position;
			value += text[position];
		}
		if (position >= text.size() || value.empty())
			return {};

		std::filesystem::path library = std::filesystem::u8path(value);
		if (library.is_relative())
			library = manifest.parent_path() / library;
		return library;
	}

	void add_registered_layer_manifests(HKEY root, const wchar_t *subkey, std::vector<std::filesystem::path> &manifests)
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
			return;
		for (DWORD index = 0;; ++index)
		{
			wchar_t name[2048] = {};
			DWORD name_length = static_cast<DWORD>(std::size(name));
			DWORD type = 0;
			DWORD disabled = 1;
			DWORD data_size = sizeof(disabled);
			const LSTATUS status = RegEnumValueW(key, index, name, &name_length, nullptr, &type, reinterpret_cast<BYTE *>(&disabled), &data_size);
			if (status == ERROR_NO_MORE_ITEMS)
				break;
			// The OpenXR loader uses a manifest whose value is the DWORD 0.
			if (status == ERROR_SUCCESS && type == REG_DWORD && disabled == 0)
				manifests.emplace_back(name);
		}
		RegCloseKey(key);
	}

	// True when the DLL is the library of an OpenXR API layer known to the loader: a registered
	// layer, or one found through XR_API_LAYER_PATH, which test setups use for their ReShade layer.
	bool is_openxr_layer_library(const std::filesystem::path &library)
	{
		std::vector<std::filesystem::path> manifests;
		for (const HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER })
		{
			add_registered_layer_manifests(root, L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit", manifests);
			add_registered_layer_manifests(root, L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Explicit", manifests);
		}

		std::wstring layer_path(32768, L'\0');
		const DWORD layer_path_length = GetEnvironmentVariableW(L"XR_API_LAYER_PATH", layer_path.data(), static_cast<DWORD>(layer_path.size()));
		layer_path.resize(layer_path_length < layer_path.size() ? layer_path_length : 0);
		for (size_t start = 0; start < layer_path.size();)
		{
			const size_t end = (std::min)(layer_path.find(L';', start), layer_path.size());
			const std::filesystem::path entry = layer_path.substr(start, end - start);
			start = end + 1;
			std::error_code error;
			if (entry.empty())
				continue;
			if (std::filesystem::is_regular_file(entry, error))
			{
				manifests.push_back(entry);
				continue;
			}
			for (std::filesystem::directory_iterator it(entry, error), end_it; !error && it != end_it; it.increment(error))
				if (it->path().extension() == L".json")
					manifests.push_back(it->path());
		}

		for (const std::filesystem::path &manifest : manifests)
		{
			const std::filesystem::path layer_library = read_layer_library_path(manifest);
			std::error_code error;
			if (!layer_library.empty() && std::filesystem::equivalent(layer_library, library, error))
				return true;
		}
		return false;
	}
#endif

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
		{
			s_ui_starts_open = GetPrivateProfileIntW(L"UI", L"StartsOpen", 1, path.c_str()) != 0;
			s_always_show_statistics = GetPrivateProfileIntW(L"UI", L"AlwaysShowFPS", 0, path.c_str()) != 0;
			const UINT divisor = GetPrivateProfileIntW(L"UI", L"FrameRateDivisor", 1, path.c_str());
			s_frame_rate_divisor = divisor >= 1 && divisor <= openxr_ui::max_frame_rate_divisor ? divisor : 1;
		}
#ifdef RESHADE_OPENVR_FRONTEND
		s_openvr_frame_timing.set_frame_rate_divisor(s_frame_rate_divisor);
#endif
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
		const panel_write_scope panel_write;
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
		openxr_ui::save_current_preset(s_effect_runtime, *updated, s_preset_dirty_state);
		s_techniques = std::move(updated);
	}

#ifdef RESHADE_OPENVR_FRONTEND
	void normalize_openvr_overlay_scale(effect_runtime *runtime)
	{
		bool migration_complete = false;
		if (reshade::get_config_value(runtime, "ADDON", "SteamVROverlayScaleNormalized", migration_complete) && migration_complete)
			return;

		float font_size = 13.0f;
		float font_scale = 1.0f;
		const bool has_font_size = reshade::get_config_value(runtime, "STYLE", "FontSize", font_size);
		const bool has_font_scale = reshade::get_config_value(runtime, "STYLE", "FontScale", font_scale);
		const auto is_default_scale = [](float value) {
			return std::fabs(value - 1.0f) < 0.001f || std::fabs(value - 1.5f) < 0.001f || std::fabs(value - 2.0f) < 0.001f;
		};

		if ((!has_font_size || std::fabs(font_size - 13.0f) < 0.001f) && (!has_font_scale || is_default_scale(font_scale)))
		{
			reshade::set_config_value(runtime, "STYLE", "FontSize", 12.0f);
			reshade::set_config_value(runtime, "STYLE", "FontScale", 1.0f);
			log_runtime_message(reshade::log::level::info, "normalized the original ReShade SteamVR dashboard scale for its fixed 500-pixel canvas.");
		}

		reshade::set_config_value(runtime, "ADDON", "SteamVROverlayScaleNormalized", true);
	}
#endif

	// ReShade returns list values as entries separated by '\0'.
	std::vector<std::string> read_config_list(effect_runtime *runtime, const char *section, const char *key)
	{
		size_t size = 0;
		if (!reshade::get_config_value(runtime, section, key, nullptr, &size) || size == 0)
			return {};
		std::string buffer(size, '\0');
		if (!reshade::get_config_value(runtime, section, key, buffer.data(), &size))
			return {};
		buffer.resize(size);
		std::vector<std::string> entries;
		for (size_t begin = 0; begin < buffer.size();)
		{
			const size_t end = (std::min)(buffer.find('\0', begin), buffer.size());
			if (end > begin)
				entries.push_back(buffer.substr(begin, end - begin));
			begin = end + 1;
		}
		return entries;
	}

	// ReShade resolves relative EffectSearchPaths from this folder.
	std::filesystem::path reshade_base_path()
	{
		using get_base_path_function = void (*)(char *, size_t *);
		const auto get_base_path = reinterpret_cast<get_base_path_function>(
			GetProcAddress(reshade::internal::get_reshade_module_handle(), "ReShadeGetBasePath"));
		if (get_base_path == nullptr)
			return {};
		size_t size = 0;
		get_base_path(nullptr, &size);
		if (size <= 1)
			return {};
		std::string path(size, '\0');
		get_base_path(path.data(), &size);
		path.resize(size);
		return std::filesystem::u8path(path);
	}

	bool is_inside_folder(const std::filesystem::path &path, const std::filesystem::path &folder)
	{
		return std::mismatch(folder.begin(), folder.end(), path.begin(), path.end()).first == folder.end();
	}

	// Finds effect files that ReShade skips: they sit in a subfolder of a search folder listed without "\**".
	effect_subfolder_scan scan_effect_subfolders(const std::vector<std::string> &entries, const std::filesystem::path &base_path)
	{
		struct search_folder
		{
			std::filesystem::path path;
			bool recursive = false;
		};
		std::vector<search_folder> folders(entries.size());
		for (size_t index = 0; index < entries.size(); ++index)
		{
			// Leave folders that use ReShade's ${...} macros alone.
			if (entries[index].find("${") != std::string::npos)
				continue;
			std::filesystem::path path = std::filesystem::u8path(entries[index]);
			folders[index].recursive = path.filename() == L"**";
			if (folders[index].recursive)
				path = path.parent_path();
			if (path.is_relative())
			{
				if (base_path.empty())
					continue;
				path = base_path / path;
			}
			std::error_code error;
			std::filesystem::path canonical = std::filesystem::canonical(path, error);
			if (!error)
				folders[index].path = std::move(canonical);
		}
		const auto searched_by_reshade = [&folders](const std::filesystem::path &directory) {
			return std::any_of(folders.begin(), folders.end(), [&directory](const search_folder &folder) {
				return !folder.path.empty() && (folder.recursive ? is_inside_folder(directory, folder.path) : directory == folder.path);
			});
		};

		effect_subfolder_scan result;
		result.search_paths = entries;
		std::unordered_set<std::wstring> hidden_folders;
		// Keeps a huge or looping folder tree from stalling the frame that runs the scan.
		size_t remaining_entries = 20000;
		for (size_t index = 0; index < folders.size(); ++index)
		{
			const search_folder &folder = folders[index];
			if (folder.path.empty() || folder.recursive)
				continue;
			bool hides_effects = false;
			std::error_code error;
			for (std::filesystem::recursive_directory_iterator it(folder.path, std::filesystem::directory_options::skip_permission_denied, error), end;
				!error && it != end && remaining_entries != 0; it.increment(error), --remaining_entries)
			{
				std::error_code type_error;
				if (it.depth() == 0 || !it->is_regular_file(type_error))
					continue;
				const std::wstring extension = it->path().extension().native();
				if (_wcsicmp(extension.c_str(), L".fx") != 0 && _wcsicmp(extension.c_str(), L".addonfx") != 0)
					continue;
				const std::filesystem::path directory = it->path().parent_path();
				if (searched_by_reshade(directory))
					continue;
				hides_effects = true;
				hidden_folders.insert(directory.native());
				result.effect_names.push_back(it->path().filename().u8string());
			}
			if (hides_effects)
			{
				std::string &entry = result.search_paths[index];
				if (entry.back() != '\\' && entry.back() != '/')
					entry += '\\';
				entry += "**";
			}
		}
		result.folder_count = static_cast<uint32_t>(hidden_folders.size());
		return result;
	}

	// Runs on ReShade's thread; disk access happens outside s_mutex so the headset panel never waits for it.
	void refresh_effect_subfolder_scan(effect_runtime *runtime)
	{
		effect_subfolder_scan scan = scan_effect_subfolders(read_config_list(runtime, "GENERAL", "EffectSearchPaths"), reshade_base_path());
		const std::lock_guard<std::mutex> lock(s_mutex);
		if (runtime != s_effect_runtime || s_effect_subfolder_notice_dismissed ||
			s_effect_subfolder_state != openxr_ui::effect_subfolder_state::none || scan.effect_names.empty())
			return;
		char message[256];
		std::snprintf(message, sizeof(message),
			"%zu effect files in %u subfolders are not loaded because EffectSearchPaths does not include subfolders; the panel offers to include them.",
			scan.effect_names.size(), scan.folder_count);
		log_runtime_message(reshade::log::level::info, message);
		s_effect_subfolder_scan = std::move(scan);
		s_effect_subfolder_state = openxr_ui::effect_subfolder_state::offered;
	}

	// Runs on ReShade's thread, because saving the setting makes ReShade re-read its whole configuration.
	void apply_effect_subfolder_fix(effect_runtime *runtime)
	{
		std::vector<std::string> search_paths;
		{
			const std::lock_guard<std::mutex> lock(s_mutex);
			if (runtime != s_effect_runtime || s_effect_subfolder_state != openxr_ui::effect_subfolder_state::saving)
				return;
			search_paths = s_effect_subfolder_scan.search_paths;
		}
		std::string joined;
		for (const std::string &path : search_paths)
			joined.append(path).push_back('\0');
		if (!joined.empty())
			reshade::set_config_value(runtime, "GENERAL", "EffectSearchPaths", joined.c_str(), joined.size() - 1);
		const bool saved = !search_paths.empty() && read_config_list(runtime, "GENERAL", "EffectSearchPaths") == search_paths;
		if (saved)
			runtime->reload_effect_next_frame(nullptr);

		const std::lock_guard<std::mutex> lock(s_mutex);
		s_effect_subfolder_state = saved ? openxr_ui::effect_subfolder_state::loading : openxr_ui::effect_subfolder_state::failed;
		s_effect_subfolder_reload_started = false;
		s_effect_subfolder_wait_frames = 0;
		log_runtime_message(saved ? reshade::log::level::info : reshade::log::level::error, saved ?
			"EffectSearchPaths now includes subfolders; ReShade is reloading all effects." :
			"failed to save EffectSearchPaths with subfolders.");
	}

	// Called with s_mutex held after each inventory of the panel's runtime.
	void note_effect_subfolder_inventory(effect_runtime *runtime)
	{
		if (s_effect_subfolder_state == openxr_ui::effect_subfolder_state::loading)
		{
			// ReShade empties the list when a reload starts and fills it again when the reload is done.
			if (s_techniques->techniques.empty())
			{
				s_effect_subfolder_reload_started = true;
				return;
			}
			if (!s_effect_subfolder_reload_started)
				return;
			const std::vector<std::string> &names = s_effect_subfolder_scan.effect_names;
			const bool loaded = std::any_of(s_techniques->techniques.begin(), s_techniques->techniques.end(), [&names](const openxr_ui::technique &entry) {
				return std::any_of(names.begin(), names.end(), [&entry](const std::string &name) {
					return _stricmp(name.c_str(), entry.effect_name.c_str()) == 0;
				});
			});
			s_effect_subfolder_state = loaded ? openxr_ui::effect_subfolder_state::loaded : openxr_ui::effect_subfolder_state::restart_needed;
			char message[192];
			std::snprintf(message, sizeof(message), loaded ?
				"effects from the subfolders are loaded after %lld s." :
				"the reload found no effects from the subfolders after %lld s; they load after a game restart.",
				static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - s_effect_subfolder_requested_at).count()));
			log_runtime_message(reshade::log::level::info, message);
			return;
		}
		if (s_effect_subfolder_scanned_runtime != runtime)
		{
			s_effect_subfolder_scanned_runtime = runtime;
			s_effect_subfolder_scan_pending.store(true, std::memory_order_release);
		}
	}

	void on_init_effect_runtime(effect_runtime *runtime)
	{
		const bool own_reshade = openxr_ui_game_brought_own_reshade();
		const bool openxr_loaded = GetModuleHandleW(L"openxr_loader.dll") != nullptr;
		const bool openvr_loaded = GetModuleHandleW(L"openvr_api.dll") != nullptr;
		if (openvr_loaded && !openxr_loaded && !s_openvr_runtime_sync_disabled)
		{
			reshade::set_config_value(nullptr, "ADDON", "SyncEffectRuntimes", false);
			s_openvr_runtime_sync_disabled = true;
			s_openvr_dashboard_pending = true;
			log_runtime_message(reshade::log::level::info, "disabled cross-runtime synchronization for OpenVR to prevent recursive preset and uniform propagation.");
		}
		if (runtime->get_hwnd() != nullptr)
		{
			const std::lock_guard<std::mutex> lock(s_mutex);
			// The game's own ReShade draws the headset picture through its window, so its effects stay on.
			if (own_reshade)
			{
				if (s_vr_runtimes.empty())
					s_vr_runtimes.insert(runtime);
				return;
			}
			s_desktop_runtimes.insert(runtime);
			if (!s_vr_runtimes.empty() && runtime->get_effects_state())
			{
				runtime->set_effects_state(false);
				s_paused_desktop_runtimes.insert(runtime);
			}
			return;
		}
		if (!openxr_loaded && !openvr_loaded)
			return;

#ifdef RESHADE_OPENVR_FRONTEND
		if (openvr_loaded && !openxr_loaded)
			normalize_openvr_overlay_scale(runtime);
#endif

		uint32_t width = 0;
		uint32_t height = 0;
		runtime->get_screenshot_width_and_height(&width, &height);

		size_t paused_count = 0;
		{
			const std::lock_guard<std::mutex> lock(s_mutex);
			s_vr_runtimes.insert(runtime);
			// The monitor image never reaches the headset, so its effects would only cost performance.
			for (effect_runtime *const desktop_runtime : s_desktop_runtimes)
			{
				if (own_reshade || !desktop_runtime->get_effects_state())
					continue;
				desktop_runtime->set_effects_state(false);
				s_paused_desktop_runtimes.insert(desktop_runtime);
				++paused_count;
			}
		}

		char message[256];
		std::snprintf(message, sizeof(message), "observed windowless %s effect runtime %p (%ux%u, %u back buffers).",
			openxr_loaded ? "OpenXR" : "OpenVR", runtime, width, height, runtime->get_back_buffer_count());
		log_runtime_message(reshade::log::level::info, message);
		if (paused_count != 0)
			log_runtime_message(reshade::log::level::info, "switched off ReShade effects on the monitor window while the headset shows them.");
	}

	void on_destroy_effect_runtime(effect_runtime *runtime)
	{
		const std::lock_guard<std::mutex> lock(s_mutex);
		s_desktop_runtimes.erase(runtime);
		s_paused_desktop_runtimes.erase(runtime);
		if (s_vr_runtimes.erase(runtime) != 0 && s_vr_runtimes.empty())
		{
			for (effect_runtime *const desktop_runtime : s_paused_desktop_runtimes)
				desktop_runtime->set_effects_state(true);
			s_paused_desktop_runtimes.clear();
		}
		if (s_effect_runtime == runtime)
		{
			s_default_values_pending = false;
			s_inherited_preset_copy.cancel(runtime);
			s_pending_preset_delete.cancel(runtime);
			s_techniques = std::make_shared<openxr_ui::technique_snapshot>();
			s_effect_runtime = nullptr;
			s_preset_dirty_state.clear();
			s_temporary_technique_states.clear();
			s_technique_restore_states.clear();
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
			// ReShade fills "source" values (timer, frame count, ...) itself every frame; its own menu hides them too.
			char source[32] = {};
			if (current_runtime->get_annotation_string_from_uniform_variable(variable, "source", source))
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
		// A reload starts with no effects; the old value handles are gone and may be reused by new values.
		if (inventory.snapshot->techniques.empty())
		{
			const std::lock_guard<std::mutex> external_lock(s_external_write_mutex);
			s_external_writes.clear();
		}
		mark_parameters_set_by_game(*inventory.snapshot);

		const bool inventory_changed = s_effect_runtime != runtime ||
			s_techniques->techniques.size() != inventory.snapshot->techniques.size() ||
			s_techniques->parameters.size() != inventory.snapshot->parameters.size() ||
			std::any_of(inventory.snapshot->techniques.begin(), inventory.snapshot->techniques.end(), [](const openxr_ui::technique &entry) {
				return std::none_of(s_techniques->techniques.begin(), s_techniques->techniques.end(), [&entry](const openxr_ui::technique &existing) {
					return existing.id == entry.id;
				});
			}) ||
			std::any_of(inventory.snapshot->parameters.begin(), inventory.snapshot->parameters.end(), [](const openxr_ui::parameter &entry) {
				return std::none_of(s_techniques->parameters.begin(), s_techniques->parameters.end(), [&entry](const openxr_ui::parameter &existing) {
					return existing.id == entry.id;
				});
			});
		if (!inventory_changed)
		{
			for (openxr_ui::technique &entry : inventory.snapshot->techniques)
			{
				const auto existing = std::find_if(s_techniques->techniques.begin(), s_techniques->techniques.end(), [&entry](const openxr_ui::technique &candidate) {
					return candidate.id == entry.id;
				});
				entry.has_restore_state = existing->has_restore_state;
			}
			for (openxr_ui::parameter &entry : inventory.snapshot->parameters)
			{
				const auto existing = std::find_if(s_techniques->parameters.begin(), s_techniques->parameters.end(), [&entry](const openxr_ui::parameter &candidate) {
					return candidate.id == entry.id;
				});
				entry.default_values = existing->default_values;
			}
			openxr_ui::update_technique_default_states(*inventory.snapshot);
		}
		s_default_values_pending = std::any_of(inventory.snapshot->parameters.begin(), inventory.snapshot->parameters.end(), [](const openxr_ui::parameter &entry) {
			return entry.resettable && entry.default_values.empty();
		});
		const bool runtime_changed = s_effect_runtime != runtime;
		s_effect_runtime = runtime;
		reshade::get_config_value(runtime, "GENERAL", "PerformanceMode", s_performance_mode);
		// ReShade reads PerformanceMode only when it creates the effect runtime.
		if (runtime_changed)
			s_performance_mode_at_start = s_performance_mode;
		// A preset switch seen first by this refresh must start a new saved baseline, as in synchronize_runtime_model.
		std::string preset_path = current_preset_path(runtime);
		const bool preset_changed = preset_path != s_observed_preset_path;
		s_observed_preset_path = std::move(preset_path);
		if (!inventory.snapshot->techniques.empty())
			s_empty_inventory_retry_frame = 0;
		if (inventory_changed || preset_changed)
			s_technique_restore_states.clear();
		if (preset_changed)
		{
			s_temporary_technique_states.clear();
			s_shaders_temporarily_disabled = false;
			for (openxr_ui::technique &entry : inventory.snapshot->techniques)
				entry.has_restore_state = false;
		}
		if (!s_shaders_temporarily_disabled)
		{
			if (inventory_changed || preset_changed)
			{
				s_preset_dirty_state.mark_saved(*inventory.snapshot);
			}
			else
			{
				s_preset_dirty_state.update(*inventory.snapshot);
			}
		}
		s_techniques = std::move(inventory.snapshot);
		s_runtime_state_dirty.store(false, std::memory_order_release);
		note_effect_subfolder_inventory(runtime);
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
		const bool values_changed = openxr_ui::reconcile_runtime_state(*updated, technique_states, parameter_states);
		const bool changed = mark_parameters_set_by_game(*updated) || values_changed;
		if (preset_path != s_observed_preset_path)
		{
			s_observed_preset_path = preset_path;
			s_temporary_technique_states.clear();
			s_technique_restore_states.clear();
			s_shaders_temporarily_disabled = false;
			for (openxr_ui::technique &entry : updated->techniques)
				entry.has_restore_state = false;
			s_preset_dirty_state.mark_saved(*updated);
			s_techniques = std::move(updated);
		}
		else
		{
			if (changed && !s_shaders_temporarily_disabled)
				s_preset_dirty_state.update(*updated);
			if (changed)
				s_techniques = std::move(updated);
		}
		return true;
	}

	void on_reshade_set_current_preset_path(effect_runtime *, const char *)
	{
		s_runtime_state_dirty.store(true, std::memory_order_release);
	}

	bool on_reshade_set_uniform_value(effect_runtime *, effect_uniform_variable variable, const void *, size_t)
	{
		s_runtime_state_dirty.store(true, std::memory_order_release);
		// Writes while ReShade's own menu is open may be the user's, so they are not counted.
		if (t_panel_write_depth == 0 && !s_reshade_overlay_open.load(std::memory_order_relaxed))
		{
			const uint64_t frame = s_present_frame.load(std::memory_order_relaxed);
			const std::lock_guard<std::mutex> lock(s_external_write_mutex);
			external_write_count &count = s_external_writes[variable.handle];
			if (count.last_frame != frame)
			{
				count.last_frame = frame;
				count.frames = (std::min)(count.frames + 1, set_by_game_frame_threshold);
			}
		}
		return false;
	}

	bool on_reshade_open_overlay(effect_runtime *, bool open, reshade::api::input_source)
	{
		s_reshade_overlay_open.store(open, std::memory_order_relaxed);
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
		s_present_frame.fetch_add(1, std::memory_order_relaxed);
		bool retry_inventory = false;
		bool scan_effect_subfolders_now = false;
		bool apply_effect_subfolders_now = false;
		// ReShade does not repeat init_effect_runtime for an add-on that registers late, and the
		// game's own ReShade usually runs before the OpenXR loader loads this add-on.
		const bool adopt_game_runtime = openxr_ui_game_brought_own_reshade() && runtime->get_hwnd() != nullptr;
		{
			const std::lock_guard<std::mutex> lock(s_mutex);
			if (adopt_game_runtime && s_vr_runtimes.empty())
			{
				s_vr_runtimes.insert(runtime);
				retry_inventory = true;
			}
			if (runtime == s_effect_runtime)
			{
				synchronize_model = synchronize_model || preset_path != s_observed_preset_path;
				if (s_techniques->techniques.empty() && s_empty_inventory_retry_frame < 600)
					retry_inventory = ++s_empty_inventory_retry_frame % 10 == 0;
				scan_effect_subfolders_now = s_effect_subfolder_scan_pending.exchange(false, std::memory_order_acq_rel);
				apply_effect_subfolders_now = s_effect_subfolder_state == openxr_ui::effect_subfolder_state::saving;
				// A ReShade that ignores the reload request loads the new folders only at the next game start.
				if (s_effect_subfolder_state == openxr_ui::effect_subfolder_state::loading && !s_effect_subfolder_reload_started &&
					++s_effect_subfolder_wait_frames > 900)
				{
					s_effect_subfolder_state = openxr_ui::effect_subfolder_state::restart_needed;
					log_runtime_message(reshade::log::level::info, "ReShade did not reload the effects; the subfolders load after a game restart.");
				}
			}
		}
		if (retry_inventory)
			on_reshade_reloaded_effects(runtime);
		else if (synchronize_model && !synchronize_runtime_model(runtime, preset_path))
			s_runtime_state_dirty.store(true, std::memory_order_release);
		if (scan_effect_subfolders_now)
			refresh_effect_subfolder_scan(runtime);
		if (apply_effect_subfolders_now)
			apply_effect_subfolder_fix(runtime);

#ifdef RESHADE_OPENVR_FRONTEND
		bool show_dashboard = false;
		bool query_refresh_rate = false;
		bool query_cpu_time = false;
		{
			const std::lock_guard<std::mutex> lock(s_mutex);
			if (runtime == s_effect_runtime)
			{
				s_openvr_frame_timing.add_frame(std::chrono::steady_clock::now());
				// SteamVR calls can be slow, so ask about every five seconds for the rate and twice a second for CPU time.
				query_refresh_rate = s_openvr_refresh_query_frame % 450 == 0;
				query_cpu_time = s_openvr_refresh_query_frame % 45 == 0;
				++s_openvr_refresh_query_frame;
			}
			show_dashboard = runtime == s_effect_runtime && s_openvr_dashboard_pending && !s_techniques->techniques.empty();
			if (show_dashboard)
				s_openvr_dashboard_pending = false;
		}
		if (query_refresh_rate)
		{
			const float refresh_hz = openvr_ui_get_display_frequency();
			const std::lock_guard<std::mutex> lock(s_mutex);
			s_openvr_frame_timing.set_refresh_hz(refresh_hz);
		}
		if (query_cpu_time)
		{
			const float cpu_ms = openvr_ui_get_cpu_ms();
			const std::lock_guard<std::mutex> lock(s_mutex);
			s_openvr_frame_timing.set_cpu_ms(cpu_ms);
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
		reshade::register_event<reshade::addon_event::reshade_open_overlay>(on_reshade_open_overlay);
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

bool openxr_ui_is_reshade_loaded()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return s_addon_registered;
}

bool openxr_ui_game_brought_own_reshade()
{
#ifdef RESHADE_OPENVR_FRONTEND
	return false;
#else
	const int known = s_own_reshade_state.load(std::memory_order_acquire);
	if (known != own_reshade_unknown)
		return known == own_reshade_yes;

	const std::lock_guard<std::mutex> lock(s_own_reshade_mutex);
	if (const int decided = s_own_reshade_state.load(std::memory_order_relaxed); decided != own_reshade_unknown)
		return decided == own_reshade_yes;
	// Without ReShade there is nothing to decide yet; a game may still load its own later.
	const HMODULE reshade_module = reshade::internal::get_reshade_module_handle();
	if (reshade_module == nullptr)
		return false;

	const std::filesystem::path reshade_path = module_path(reshade_module);
	const bool own = !reshade_path.empty() && !is_openxr_layer_library(reshade_path);
	s_own_reshade_state.store(own ? own_reshade_yes : own_reshade_no, std::memory_order_release);
	if (own)
	{
		const std::string message = "this game loaded its own ReShade (" + reshade_path.u8string() +
			"), not ReShade's OpenXR layer. The panel controls that ReShade; this add-on does not start its own "
			"headset effects and does not switch off the game's ReShade effects.";
		openxr_ui_log_message(message.c_str());
	}
	return own;
#endif
}

void openxr_ui_log_message(const char *message, bool error)
{
	OutputDebugStringA(message);
	OutputDebugStringA("\n");
	if (s_addon_registered)
		log_runtime_message(error ? reshade::log::level::error : reshade::log::level::info, message);
}

namespace
{
	// ReShade's desktop/headset effect sync stops its own echo only when a change starts outside
	// another add-on's ReShade event. Resetting uniforms inside our callbacks bounced between the two
	// runtimes until the stack overflowed, so defaults are captured here, from the menu path, instead.
	void capture_pending_default_values()
	{
		const panel_write_scope panel_write;
		if (!s_default_values_pending || s_effect_runtime == nullptr)
			return;

		constexpr size_t batch_size = 32;
		auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
		size_t captured = 0;
		for (openxr_ui::parameter &entry : updated->parameters)
		{
			if (!entry.resettable || !entry.default_values.empty())
				continue;
			if (captured == batch_size)
				break;
			const effect_uniform_variable variable = { entry.id };
			openxr_ui::parameter current = entry;
			read_parameter_values(s_effect_runtime, variable, current);
			s_effect_runtime->reset_uniform_value(variable);
			openxr_ui::parameter defaults = entry;
			read_parameter_values(s_effect_runtime, variable, defaults);
			entry.default_values = std::move(defaults.values);
			write_parameter_values(s_effect_runtime, variable, current);
			++captured;
		}
		s_default_values_pending = std::any_of(updated->parameters.begin(), updated->parameters.end(), [](const openxr_ui::parameter &entry) {
			return entry.resettable && entry.default_values.empty();
		});
		openxr_ui::update_technique_default_states(*updated);
		s_techniques = std::move(updated);
	}
}

std::shared_ptr<const openxr_ui::technique_snapshot> openxr_ui_get_techniques()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	capture_pending_default_values();
	return s_techniques;
}

bool openxr_ui_has_effect_runtime()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return s_effect_runtime != nullptr;
}

bool openxr_ui_has_vr_runtime()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return !s_vr_runtimes.empty();
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
		s_preset_dirty_state.update(*updated);
		s_techniques = std::move(updated);
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

bool openxr_ui_get_always_show_statistics()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	load_ui_settings();
	return s_always_show_statistics;
}

bool openxr_ui_set_always_show_statistics(bool enabled)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	load_ui_settings();
	const std::filesystem::path path = ui_settings_path();
	if (path.empty())
		return false;
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	if (error || !WritePrivateProfileStringW(L"UI", L"AlwaysShowFPS", enabled ? L"1" : L"0", path.c_str()))
	{
		log_runtime_message(reshade::log::level::error, "failed to save the FPS counter setting.");
		return false;
	}
	s_always_show_statistics = enabled;
	log_runtime_message(reshade::log::level::info, enabled ? "FPS counter is always shown." : "FPS counter is shown only with the UI.");
	return true;
}

uint32_t openxr_ui_get_frame_rate_divisor()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	load_ui_settings();
	return s_frame_rate_divisor;
}

bool openxr_ui_set_frame_rate_divisor(uint32_t divisor)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	load_ui_settings();
	const std::filesystem::path path = ui_settings_path();
	if (path.empty() || divisor < 1 || divisor > openxr_ui::max_frame_rate_divisor)
		return false;
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	if (error || !WritePrivateProfileStringW(L"UI", L"FrameRateDivisor", std::to_wstring(divisor).c_str(), path.c_str()))
	{
		log_runtime_message(reshade::log::level::error, "failed to save the FPS target setting.");
		return false;
	}
	s_frame_rate_divisor = divisor;
#ifdef RESHADE_OPENVR_FRONTEND
	s_openvr_frame_timing.set_frame_rate_divisor(divisor);
#endif
	char message[64];
	std::snprintf(message, sizeof(message), "FPS target is %s.", openxr_ui::frame_rate_divisor_name(divisor));
	log_runtime_message(reshade::log::level::info, message);
	return true;
}

#ifdef RESHADE_OPENVR_FRONTEND
openxr_ui::frame_statistics openvr_ui_get_frame_statistics()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	load_ui_settings();
	return s_openvr_frame_timing.statistics();
}
#endif

bool openxr_ui_get_performance_mode()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return s_performance_mode;
}

bool openxr_ui_performance_mode_restart_pending()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return s_effect_runtime != nullptr && s_performance_mode != s_performance_mode_at_start;
}

bool openxr_ui_performance_mode_active()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return s_effect_runtime != nullptr && s_performance_mode_at_start;
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

namespace
{
	std::vector<std::string> read_disabled_addons()
	{
		return read_config_list(nullptr, "ADDON", "DisabledAddons");
	}

	bool is_generic_depth_entry(const std::string &name)
	{
		return name == generic_depth_addon_name || name.rfind(std::string(generic_depth_addon_name) + '@', 0) == 0;
	}

	void load_depth_detection_setting()
	{
		if (s_depth_detection_loaded || !s_addon_registered)
			return;
		const std::vector<std::string> names = read_disabled_addons();
		s_depth_detection_disabled = std::any_of(names.begin(), names.end(), is_generic_depth_entry);
		s_depth_detection_disabled_at_start = s_depth_detection_disabled;
		s_depth_detection_loaded = true;
	}
}

bool openxr_ui_is_depth_detection_disabled()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	load_depth_detection_setting();
	return s_depth_detection_disabled;
}

bool openxr_ui_depth_detection_restart_pending()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	load_depth_detection_setting();
	return s_depth_detection_disabled != s_depth_detection_disabled_at_start;
}

bool openxr_ui_set_depth_detection_disabled(bool disabled)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	load_depth_detection_setting();
	if (!s_depth_detection_loaded)
		return false;
	std::vector<std::string> names = read_disabled_addons();
	names.erase(std::remove_if(names.begin(), names.end(), is_generic_depth_entry), names.end());
	if (disabled)
		names.push_back(generic_depth_addon_name);
	if (names.empty())
	{
		reshade::set_config_value(nullptr, "ADDON", "DisabledAddons", static_cast<const char *>(nullptr));
	}
	else
	{
		std::string joined;
		for (const std::string &name : names)
			joined.append(name).push_back('\0');
		reshade::set_config_value(nullptr, "ADDON", "DisabledAddons", joined.c_str(), joined.size() - 1);
	}
	const std::vector<std::string> saved = read_disabled_addons();
	if (std::any_of(saved.begin(), saved.end(), is_generic_depth_entry) != disabled)
	{
		log_runtime_message(reshade::log::level::error, "failed to save the ReShade depth detection setting.");
		return false;
	}
	s_depth_detection_disabled = disabled;
	log_runtime_message(reshade::log::level::info, disabled ?
		"ReShade depth detection (Generic Depth) will be off from the next game start." :
		"ReShade depth detection (Generic Depth) will be on from the next game start.");
	return true;
}

openxr_ui::effect_subfolder_status openxr_ui_get_effect_subfolder_status()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	const bool busy = s_effect_subfolder_state == openxr_ui::effect_subfolder_state::saving ||
		s_effect_subfolder_state == openxr_ui::effect_subfolder_state::loading;
	const uint32_t seconds = busy ? static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now() - s_effect_subfolder_requested_at).count()) : 0;
	return { s_effect_subfolder_state, static_cast<uint32_t>(s_effect_subfolder_scan.effect_names.size()), s_effect_subfolder_scan.folder_count, seconds };
}

bool openxr_ui_include_effect_subfolders()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	// Unsaved preset changes are not a blocker: mods such as D2R-PCVR change their shader values every frame,
	// so the preset never stays saved there. The panel warns that a reload drops them instead.
	if (s_effect_runtime == nullptr || s_effect_subfolder_state != openxr_ui::effect_subfolder_state::offered ||
		s_shaders_temporarily_disabled)
		return false;
	s_effect_subfolder_state = openxr_ui::effect_subfolder_state::saving;
	s_effect_subfolder_requested_at = std::chrono::steady_clock::now();
	log_runtime_message(reshade::log::level::info, "controller asked to load the effects in subfolders.");
	return true;
}

void openxr_ui_dismiss_effect_subfolder_notice()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_subfolder_state == openxr_ui::effect_subfolder_state::saving || s_effect_subfolder_state == openxr_ui::effect_subfolder_state::loading)
		return;
	s_effect_subfolder_state = openxr_ui::effect_subfolder_state::none;
	s_effect_subfolder_notice_dismissed = true;
}

bool openxr_ui_set_parameter(uint64_t parameter_id, uint32_t component, double value)
{
	const panel_write_scope panel_write;
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
	s_preset_dirty_state.update(*updated);
	s_techniques = std::move(updated);
	return true;
}

bool openxr_ui_reset_parameter(uint64_t parameter_id)
{
	const panel_write_scope panel_write;
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
	s_preset_dirty_state.update(*updated);
	s_techniques = std::move(updated);
	return true;
}

bool openxr_ui_reset_all_parameters(uint64_t technique_id)
{
	const panel_write_scope panel_write;
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
		s_preset_dirty_state.update(*updated);
		s_techniques = std::move(updated);
	}
	return reset_any;
}

bool openxr_ui_revert_technique(uint64_t technique_id)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || technique_id == 0 || s_shaders_temporarily_disabled)
		return false;
	const auto technique_it = std::find_if(s_techniques->techniques.begin(), s_techniques->techniques.end(), [technique_id](const openxr_ui::technique &entry) {
		return entry.id == technique_id;
	});
	if (technique_it == s_techniques->techniques.end())
		return false;

	bool saved_enabled = false;
	if (!s_preset_dirty_state.get_saved_technique_state(technique_id, saved_enabled))
		return false;

	technique_restore_state restore_state;
	restore_state.enabled = technique_it->enabled;
	for (const openxr_ui::parameter &parameter : s_techniques->parameters)
		if (parameter.effect_name == technique_it->effect_name)
			restore_state.parameter_values.emplace(parameter.id, parameter.values);

	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	const size_t technique_index = static_cast<size_t>(technique_it - s_techniques->techniques.begin());
	s_effect_runtime->set_technique_state({ technique_id }, saved_enabled);
	updated->techniques[technique_index].enabled = s_effect_runtime->get_technique_state({ technique_id });
	for (openxr_ui::parameter &parameter : updated->parameters)
	{
		if (parameter.effect_name != technique_it->effect_name)
			continue;
		std::vector<double> saved_values;
		if (!s_preset_dirty_state.get_saved_parameter_values(parameter.id, saved_values) || saved_values.size() != parameter.values.size())
			continue;
		parameter.values = std::move(saved_values);
		write_parameter_values(s_effect_runtime, { parameter.id }, parameter);
		read_parameter_values(s_effect_runtime, { parameter.id }, parameter);
	}
	updated->techniques[technique_index].has_restore_state = true;
	openxr_ui::update_technique_default_states(*updated);
	s_technique_restore_states[technique_id] = std::move(restore_state);
	s_preset_dirty_state.update(*updated);
	s_techniques = std::move(updated);
	return true;
}

bool openxr_ui_restore_technique(uint64_t technique_id)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || technique_id == 0 || s_shaders_temporarily_disabled)
		return false;
	const auto restore_it = s_technique_restore_states.find(technique_id);
	const auto technique_it = std::find_if(s_techniques->techniques.begin(), s_techniques->techniques.end(), [technique_id](const openxr_ui::technique &entry) {
		return entry.id == technique_id;
	});
	if (restore_it == s_technique_restore_states.end() || technique_it == s_techniques->techniques.end())
		return false;

	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	const size_t technique_index = static_cast<size_t>(technique_it - s_techniques->techniques.begin());
	s_effect_runtime->set_technique_state({ technique_id }, restore_it->second.enabled);
	updated->techniques[technique_index].enabled = s_effect_runtime->get_technique_state({ technique_id });
	for (openxr_ui::parameter &parameter : updated->parameters)
	{
		if (parameter.effect_name != technique_it->effect_name)
			continue;
		const auto values_it = restore_it->second.parameter_values.find(parameter.id);
		if (values_it == restore_it->second.parameter_values.end() || values_it->second.size() != parameter.values.size())
			continue;
		parameter.values = values_it->second;
		write_parameter_values(s_effect_runtime, { parameter.id }, parameter);
		read_parameter_values(s_effect_runtime, { parameter.id }, parameter);
	}
	updated->techniques[technique_index].has_restore_state = false;
	openxr_ui::update_technique_default_states(*updated);
	s_technique_restore_states.erase(restore_it);
	s_preset_dirty_state.update(*updated);
	s_techniques = std::move(updated);
	return true;
}

bool openxr_ui_save_preset()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	if (s_effect_runtime == nullptr || s_shaders_temporarily_disabled)
		return false;
	auto updated = std::make_shared<openxr_ui::technique_snapshot>(*s_techniques);
	openxr_ui::save_current_preset(s_effect_runtime, *updated, s_preset_dirty_state);
	s_techniques = std::move(updated);
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
			reshade::unregister_event<reshade::addon_event::reshade_open_overlay>(on_reshade_open_overlay);
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