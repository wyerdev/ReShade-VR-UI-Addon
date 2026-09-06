#include "openxr_ui_preset.hpp"

#include "openxr_ui_model.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace
{
	std::filesystem::path current_preset_path(reshade::api::effect_runtime *runtime)
	{
		size_t path_size = 0;
		runtime->get_current_preset_path(nullptr, &path_size);
		if (path_size <= 1)
			return {};
		std::string path(path_size, '\0');
		runtime->get_current_preset_path(path.data(), &path_size);
		path.resize(path_size);
		return std::filesystem::u8path(path);
	}

	bool paths_refer_to_same_file(const std::filesystem::path &left, const std::filesystem::path &right)
	{
		std::error_code error;
		if (std::filesystem::equivalent(left, right, error))
			return true;
		return CompareStringOrdinal(left.lexically_normal().c_str(), -1, right.lexically_normal().c_str(), -1, TRUE) == CSTR_EQUAL;
	}

	bool valid_preset_name(const std::string &name)
	{
		if (name.empty() || name.size() > 120 || name == "." || name == ".." || name.back() == ' ' || name.back() == '.')
			return false;
		return std::none_of(name.begin(), name.end(), [](unsigned char character) {
			return character < 32 || std::strchr("<>:\"/\\|?*", character) != nullptr;
		});
	}
}

void openxr_ui::preset_dirty_state::clear()
{
	_saved_technique_states.clear();
	_saved_parameter_values.clear();
	_dirty = false;
}

void openxr_ui::preset_dirty_state::mark_saved(technique_snapshot &snapshot)
{
	_saved_technique_states.clear();
	for (technique &entry : snapshot.techniques)
	{
		_saved_technique_states.emplace(entry.id, entry.enabled);
		entry.differs_from_saved = false;
	}
	_saved_parameter_values.clear();
	for (const parameter &entry : snapshot.parameters)
		_saved_parameter_values.emplace(entry.id, entry.values);
	_dirty = false;
}

void openxr_ui::preset_dirty_state::update(technique_snapshot &snapshot)
{
	for (technique &entry : snapshot.techniques)
	{
		const auto saved_technique = _saved_technique_states.find(entry.id);
		entry.differs_from_saved = saved_technique == _saved_technique_states.end() || saved_technique->second != entry.enabled ||
			std::any_of(snapshot.parameters.begin(), snapshot.parameters.end(), [this, &entry](const parameter &parameter_entry) {
				if (parameter_entry.effect_name != entry.effect_name)
					return false;
				const auto saved_parameter = _saved_parameter_values.find(parameter_entry.id);
				return saved_parameter == _saved_parameter_values.end() || saved_parameter->second != parameter_entry.values;
			});
	}
	_dirty = _saved_technique_states.size() != snapshot.techniques.size() ||
		_saved_parameter_values.size() != snapshot.parameters.size() ||
		std::any_of(snapshot.techniques.begin(), snapshot.techniques.end(), [](const technique &entry) {
			return entry.differs_from_saved;
		});
}

bool openxr_ui::preset_dirty_state::is_dirty() const
{
	return _dirty;
}

bool openxr_ui::preset_dirty_state::get_saved_technique_state(uint64_t technique_id, bool &enabled) const
{
	const auto saved_it = _saved_technique_states.find(technique_id);
	if (saved_it == _saved_technique_states.end())
		return false;
	enabled = saved_it->second;
	return true;
}

bool openxr_ui::preset_dirty_state::get_saved_parameter_values(uint64_t parameter_id, std::vector<double> &values) const
{
	const auto saved_it = _saved_parameter_values.find(parameter_id);
	if (saved_it == _saved_parameter_values.end())
		return false;
	values = saved_it->second;
	return true;
}

struct openxr_ui::inherited_preset_copy::state
{
	reshade::api::effect_runtime *runtime = nullptr;
	std::filesystem::path source_path;
	std::filesystem::path destination_path;
	std::filesystem::file_time_type source_write_time;
	bool wait_for_source_update = false;
	std::chrono::steady_clock::time_point copy_after;
	std::chrono::steady_clock::time_point fail_after;
};

openxr_ui::inherited_preset_copy::inherited_preset_copy() = default;
openxr_ui::inherited_preset_copy::~inherited_preset_copy() = default;

bool openxr_ui::inherited_preset_copy::begin(
	reshade::api::effect_runtime *runtime,
	const std::string &preset_name,
	bool wait_for_source_update)
{
	if (runtime == nullptr || _state != nullptr || !valid_preset_name(preset_name))
		return false;
	const std::filesystem::path source_path = current_preset_path(runtime);
	if (source_path.empty())
		return false;
	const std::filesystem::path extension = source_path.extension().empty() ? L".ini" : source_path.extension();
	const std::filesystem::path destination_path = source_path.parent_path() /
		(std::filesystem::u8path(preset_name).wstring() + extension.wstring());
	std::error_code error;
	if (std::filesystem::exists(destination_path, error) || error)
		return false;
	const std::filesystem::file_time_type source_write_time = std::filesystem::last_write_time(source_path, error);

	runtime->save_current_preset();
	_state = std::make_unique<state>();
	_state->runtime = runtime;
	_state->source_path = source_path;
	_state->destination_path = destination_path;
	_state->source_write_time = source_write_time;
	_state->wait_for_source_update = wait_for_source_update && !error;
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	_state->copy_after = now + std::chrono::milliseconds(1250);
	_state->fail_after = now + std::chrono::seconds(3);
	return true;
}

openxr_ui::preset_copy_result openxr_ui::inherited_preset_copy::update(reshade::api::effect_runtime *runtime)
{
	if (_state == nullptr || std::chrono::steady_clock::now() < _state->copy_after)
		return {};
	if (_state->runtime != runtime || !paths_refer_to_same_file(current_preset_path(runtime), _state->source_path))
	{
		_state.reset();
		return {};
	}

	std::error_code error;
	if (_state->wait_for_source_update)
	{
		const std::filesystem::file_time_type source_write_time = std::filesystem::last_write_time(_state->source_path, error);
		if (error || source_write_time == _state->source_write_time)
		{
			if (std::chrono::steady_clock::now() < _state->fail_after)
				return {};
			_state.reset();
			return { preset_copy_status::flush_failed, {} };
		}
	}
	if (!std::filesystem::copy_file(_state->source_path, _state->destination_path, std::filesystem::copy_options::none, error))
	{
		_state.reset();
		return { preset_copy_status::copy_failed, {} };
	}

	const std::filesystem::path destination_path = _state->destination_path;
	const std::string destination = destination_path.u8string();
	_state->runtime->set_current_preset_path(destination.c_str());
	if (!paths_refer_to_same_file(current_preset_path(_state->runtime), destination_path))
	{
		std::filesystem::remove(destination_path, error);
		_state.reset();
		return { preset_copy_status::preset_rejected, {} };
	}
	_state.reset();
	return { preset_copy_status::created, destination };
}

void openxr_ui::inherited_preset_copy::cancel(reshade::api::effect_runtime *runtime)
{
	if (_state != nullptr && _state->runtime == runtime)
		_state.reset();
}

bool openxr_ui::inherited_preset_copy::is_pending() const
{
	return _state != nullptr;
}

struct openxr_ui::pending_preset_delete::state
{
	reshade::api::effect_runtime *runtime = nullptr;
	std::filesystem::path deleted_path;
	std::chrono::steady_clock::time_point delete_after;
	std::chrono::steady_clock::time_point fail_after;
};

openxr_ui::pending_preset_delete::pending_preset_delete() = default;
openxr_ui::pending_preset_delete::~pending_preset_delete() = default;

bool openxr_ui::pending_preset_delete::begin(reshade::api::effect_runtime *runtime)
{
	if (runtime == nullptr || _state != nullptr)
		return false;
	const std::filesystem::path deleted_path = current_preset_path(runtime);
	if (deleted_path.empty() || !switch_to_sibling_preset(runtime, false))
		return false;

	_state = std::make_unique<state>();
	_state->runtime = runtime;
	_state->deleted_path = deleted_path;
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	_state->delete_after = now + std::chrono::milliseconds(1250);
	_state->fail_after = now + std::chrono::seconds(3);
	return true;
}

openxr_ui::preset_delete_status openxr_ui::pending_preset_delete::update(reshade::api::effect_runtime *runtime)
{
	if (_state == nullptr || std::chrono::steady_clock::now() < _state->delete_after)
		return preset_delete_status::none;
	if (_state->runtime != runtime)
	{
		_state.reset();
		return preset_delete_status::none;
	}

	std::error_code error;
	const bool removed = std::filesystem::remove(_state->deleted_path, error);
	if (removed || (!error && !std::filesystem::exists(_state->deleted_path, error)))
	{
		_state.reset();
		return preset_delete_status::deleted;
	}
	if (std::chrono::steady_clock::now() < _state->fail_after)
		return preset_delete_status::none;

	_state.reset();
	return preset_delete_status::delete_failed;
}

void openxr_ui::pending_preset_delete::cancel(reshade::api::effect_runtime *runtime)
{
	if (_state != nullptr && _state->runtime == runtime)
		_state.reset();
}

bool openxr_ui::pending_preset_delete::is_pending() const
{
	return _state != nullptr;
}

bool openxr_ui::save_current_preset(
	reshade::api::effect_runtime *runtime,
	technique_snapshot &snapshot,
	preset_dirty_state &dirty_state)
{
	if (runtime == nullptr)
		return false;
	runtime->save_current_preset();
	dirty_state.mark_saved(snapshot);
	return true;
}

std::string openxr_ui::current_preset_name(reshade::api::effect_runtime *runtime)
{
	return runtime != nullptr ? current_preset_path(runtime).stem().u8string() : std::string();
}

bool openxr_ui::switch_to_sibling_preset(reshade::api::effect_runtime *runtime, bool previous)
{
	if (runtime == nullptr)
		return false;
	const std::filesystem::path current_path = current_preset_path(runtime);
	if (current_path.empty())
		return false;

	std::error_code error;
	std::vector<std::filesystem::path> preset_paths;
	for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(
		current_path.parent_path(), std::filesystem::directory_options::skip_permission_denied, error))
	{
		const std::filesystem::path extension = entry.path().extension();
		if (entry.is_regular_file(error) && (extension == L".ini" || extension == L".txt"))
			preset_paths.push_back(entry.path());
	}
	if (preset_paths.size() <= 1)
		return false;

	size_t current_index = preset_paths.size();
	for (size_t index = 0; index < preset_paths.size(); ++index)
		if (paths_refer_to_same_file(preset_paths[index], current_path))
		{
			current_index = index;
			break;
		}

	for (size_t offset = current_index == preset_paths.size() ? 0 : 1; offset < preset_paths.size(); ++offset)
	{
		const size_t index = current_index == preset_paths.size() ?
			(previous ? preset_paths.size() - 1 - offset : offset) :
			(previous ? (current_index + preset_paths.size() - offset) % preset_paths.size() : (current_index + offset) % preset_paths.size());
		if (paths_refer_to_same_file(preset_paths[index], current_path))
			continue;
		const std::string candidate = preset_paths[index].u8string();
		runtime->set_current_preset_path(candidate.c_str());
		if (paths_refer_to_same_file(current_preset_path(runtime), preset_paths[index]))
			return true;
	}
	return false;
}