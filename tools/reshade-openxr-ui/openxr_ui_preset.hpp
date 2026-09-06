#pragma once

#include <reshade.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openxr_ui
{
	struct technique_snapshot;

	class preset_dirty_state
	{
	public:
		void clear();
		void mark_saved(technique_snapshot &snapshot);
		void update(technique_snapshot &snapshot);
		bool is_dirty() const;
		bool get_saved_technique_state(uint64_t technique_id, bool &enabled) const;
		bool get_saved_parameter_values(uint64_t parameter_id, std::vector<double> &values) const;

	private:
		bool _dirty = false;
		std::unordered_map<uint64_t, bool> _saved_technique_states;
		std::unordered_map<uint64_t, std::vector<double>> _saved_parameter_values;
	};

	enum class preset_copy_status
	{
		none,
		created,
		flush_failed,
		copy_failed,
		preset_rejected,
	};

	struct preset_copy_result
	{
		preset_copy_status status = preset_copy_status::none;
		std::string destination;
	};

	class inherited_preset_copy
	{
	public:
		inherited_preset_copy();
		~inherited_preset_copy();

		inherited_preset_copy(const inherited_preset_copy &) = delete;
		inherited_preset_copy &operator=(const inherited_preset_copy &) = delete;

		bool begin(reshade::api::effect_runtime *runtime, const std::string &preset_name, bool wait_for_source_update);
		preset_copy_result update(reshade::api::effect_runtime *runtime);
		void cancel(reshade::api::effect_runtime *runtime);
		bool is_pending() const;

	private:
		struct state;
		std::unique_ptr<state> _state;
	};

	enum class preset_delete_status
	{
		none,
		deleted,
		delete_failed,
	};

	class pending_preset_delete
	{
	public:
		pending_preset_delete();
		~pending_preset_delete();

		pending_preset_delete(const pending_preset_delete &) = delete;
		pending_preset_delete &operator=(const pending_preset_delete &) = delete;

		bool begin(reshade::api::effect_runtime *runtime);
		preset_delete_status update(reshade::api::effect_runtime *runtime);
		void cancel(reshade::api::effect_runtime *runtime);
		bool is_pending() const;

	private:
		struct state;
		std::unique_ptr<state> _state;
	};

	bool save_current_preset(
		reshade::api::effect_runtime *runtime,
		technique_snapshot &snapshot,
		preset_dirty_state &dirty_state);
	std::string current_preset_name(reshade::api::effect_runtime *runtime);
	bool switch_to_sibling_preset(reshade::api::effect_runtime *runtime, bool previous);
}