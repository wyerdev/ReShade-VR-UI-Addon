#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace openxr_ui
{
	struct technique
	{
		uint64_t id = 0;
		std::string name;
		std::string tooltip;
		std::string effect_name;
		bool enabled = false;
		bool has_non_default_values = false;
		bool hidden = false;
		bool favourite = false;
		std::string persistence_id;
		bool differs_from_saved = false;
		bool has_restore_state = false;
	};

	// Effect files inside subfolders of an EffectSearchPaths folder that ReShade does not search recursively.
	enum class effect_subfolder_state
	{
		none,
		offered,
		// The user chose to include the subfolders; ReShade's own thread saves the change.
		saving,
		loading,
		loaded,
		// Saved, but this ReShade loads the new effects only when the game starts again.
		restart_needed,
		failed,
	};

	struct effect_subfolder_status
	{
		effect_subfolder_state state = effect_subfolder_state::none;
		uint32_t effect_count = 0;
		uint32_t folder_count = 0;
		// Whole seconds since the user pressed the button, while saving or loading.
		uint32_t loading_seconds = 0;
	};

	enum class parameter_type
	{
		boolean,
		signed_integer,
		unsigned_integer,
		floating_point,
	};

	struct parameter
	{
		uint64_t id = 0;
		std::string name;
		std::string tooltip;
		std::string effect_name;
		parameter_type type = parameter_type::floating_point;
		std::vector<double> values;
		std::vector<double> default_values;
		uint32_t component_count = 1;
		uint32_t array_length = 1;
		double minimum = std::numeric_limits<double>::lowest();
		double maximum = (std::numeric_limits<double>::max)();
		double step = 0.001;
		bool resettable = true;
		// Another add-on (for example a VR mod) keeps writing this value, so it does not count as an unsaved change.
		bool set_by_game = false;
	};

	struct technique_snapshot
	{
		std::vector<technique> techniques;
		std::vector<parameter> parameters;
	};

	struct runtime_technique_state
	{
		uint64_t id = 0;
		bool enabled = false;
	};

	struct runtime_parameter_state
	{
		uint64_t id = 0;
		std::vector<double> values;
	};

	inline void move_enabled_techniques_to_top(technique_snapshot &snapshot)
	{
		std::stable_partition(snapshot.techniques.begin(), snapshot.techniques.end(), [](const technique &entry) {
			return entry.enabled;
		});
	}

	inline bool move_technique(technique_snapshot &snapshot, uint64_t technique_id, bool move_up)
	{
		const auto technique_it = std::find_if(snapshot.techniques.begin(), snapshot.techniques.end(), [technique_id](const technique &entry) {
			return entry.id == technique_id;
		});
		if (technique_it == snapshot.techniques.end() ||
			(move_up && technique_it == snapshot.techniques.begin()) ||
			(!move_up && technique_it + 1 == snapshot.techniques.end()))
			return false;

		std::iter_swap(technique_it, move_up ? technique_it - 1 : technique_it + 1);
		return true;
	}

	inline void preserve_technique_order(technique_snapshot &snapshot, const technique_snapshot &previous)
	{
		std::vector<technique> ordered;
		ordered.reserve(snapshot.techniques.size());
		for (const technique &previous_entry : previous.techniques)
		{
			const auto match = std::find_if(snapshot.techniques.begin(), snapshot.techniques.end(), [&previous_entry](const technique &entry) {
				return entry.persistence_id == previous_entry.persistence_id;
			});
			if (match != snapshot.techniques.end())
			{
				ordered.push_back(std::move(*match));
				snapshot.techniques.erase(match);
			}
		}
		ordered.insert(ordered.end(), std::make_move_iterator(snapshot.techniques.begin()), std::make_move_iterator(snapshot.techniques.end()));
		snapshot.techniques = std::move(ordered);
	}

	inline bool component_differs_from_default(const parameter &entry, uint32_t component)
	{
		if (component >= entry.values.size() || component >= entry.default_values.size())
			return false;
		if (entry.type == parameter_type::floating_point)
		{
			const double scale = (std::max)({ 1.0, std::abs(entry.values[component]), std::abs(entry.default_values[component]) });
			return std::abs(entry.values[component] - entry.default_values[component]) > scale * 1.0e-6;
		}
		return entry.values[component] != entry.default_values[component];
	}

	inline void update_technique_default_states(technique_snapshot &snapshot)
	{
		for (technique &entry : snapshot.techniques)
		{
			entry.has_non_default_values = std::any_of(snapshot.parameters.begin(), snapshot.parameters.end(), [&entry](const parameter &parameter_entry) {
				if (parameter_entry.effect_name != entry.effect_name)
					return false;
				for (uint32_t component = 0; component < parameter_entry.values.size(); ++component)
					if (component_differs_from_default(parameter_entry, component))
						return true;
				return false;
			});
		}
	}

	inline bool reconcile_runtime_state(
		technique_snapshot &snapshot,
		const std::vector<runtime_technique_state> &technique_states,
		const std::vector<runtime_parameter_state> &parameter_states)
	{
		bool changed = false;
		std::vector<technique> ordered_techniques;
		ordered_techniques.reserve(snapshot.techniques.size());
		for (const runtime_technique_state &state : technique_states)
		{
			const auto match = std::find_if(snapshot.techniques.begin(), snapshot.techniques.end(), [&state](const technique &entry) {
				return entry.id == state.id;
			});
			if (match == snapshot.techniques.end())
				continue;
			technique entry = *match;
			if (entry.enabled != state.enabled)
			{
				entry.enabled = state.enabled;
				changed = true;
			}
			ordered_techniques.push_back(std::move(entry));
		}
		for (const technique &entry : snapshot.techniques)
		{
			if (std::none_of(ordered_techniques.begin(), ordered_techniques.end(), [&entry](const technique &ordered) {
				return ordered.id == entry.id;
			}))
				ordered_techniques.push_back(entry);
		}
		if (!std::equal(ordered_techniques.begin(), ordered_techniques.end(), snapshot.techniques.begin(), snapshot.techniques.end(),
			[](const technique &left, const technique &right) { return left.id == right.id; }))
			changed = true;
		snapshot.techniques = std::move(ordered_techniques);

		bool parameter_changed = false;
		for (const runtime_parameter_state &state : parameter_states)
		{
			const auto match = std::find_if(snapshot.parameters.begin(), snapshot.parameters.end(), [&state](const parameter &entry) {
				return entry.id == state.id;
			});
			if (match != snapshot.parameters.end() && match->values != state.values)
			{
				match->values = state.values;
				parameter_changed = true;
			}
		}
		if (parameter_changed)
			update_technique_default_states(snapshot);
		return changed || parameter_changed;
	}
}