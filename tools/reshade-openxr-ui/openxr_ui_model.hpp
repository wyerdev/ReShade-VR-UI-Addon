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

	// Effect files with the same name in more than one effect folder; ReShade loads every copy.
	enum class duplicate_effect_state
	{
		none,
		offered,
		// The user chose to move the extra copies; ReShade's own thread moves them.
		moving,
		moved,
		failed,
	};

	struct duplicate_effect_status
	{
		duplicate_effect_state state = duplicate_effect_state::none;
		uint32_t effect_count = 0;
		uint32_t extra_count = 0;
		uint32_t moved_count = 0;
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

	// Presets and starred effects shared between games (see openxr_ui_global_library.hpp), as the menus show them.
	enum class global_preset_state
	{
		not_in_game,
		in_game,
		// This game has a copy of an older version.
		update_available,
	};

	struct global_preset_row
	{
		std::string name;
		uint32_t version = 0;
		uint32_t effect_count = 0;
		// Effects of the preset that this game does not have.
		uint32_t missing_effect_count = 0;
		global_preset_state state = global_preset_state::not_in_game;
		uint32_t imported_version = 0;
		// This game's copy is the preset in use.
		bool current = false;
	};

	// A starred effect that this game does not have but the shared library can add.
	struct missing_starred_effect
	{
		std::string persistence_id;
		std::string name;
		std::string effect_name;
	};

	enum class global_import_kind
	{
		import_preset,
		update_preset,
		add_effect,
	};

	// What an import would do, for the confirmation box.
	struct global_import_summary
	{
		global_import_kind kind = global_import_kind::import_preset;
		std::string name;
		uint32_t version = 0;
		std::vector<std::string> added_effect_files;
		std::vector<std::string> added_textures;
		uint32_t existing_file_count = 0;
		std::vector<std::string> different_files;
		std::vector<std::string> incomplete_effects;
	};

	enum class global_notice_level
	{
		none,
		info,
		warning,
		error,
	};

	struct global_library_snapshot
	{
		// A save, import or update is running on the worker thread.
		bool busy = false;
		std::vector<global_preset_row> presets;
		std::vector<missing_starred_effect> missing_starred;
		// The global preset the current preset is a copy of; empty when it is not a copy.
		std::string current_source;
		uint32_t current_source_version = 0;
		bool current_changed = false;
		bool confirmation_pending = false;
		global_import_summary confirmation;
		global_notice_level notice_level = global_notice_level::none;
		std::string notice;
	};

	inline std::string count_text(size_t count, const char *singular, const char *plural)
	{
		return std::to_string(count) + ' ' + (count == 1 ? singular : plural);
	}

	inline std::string global_preset_status_text(const global_preset_row &row)
	{
		switch (row.state)
		{
		case global_preset_state::in_game:
			return row.current ? "In this game, loaded" : "In this game";
		case global_preset_state::update_available:
			return 'v' + std::to_string(row.version) + " is new (this game has v" + std::to_string(row.imported_version) + ')';
		default:
			return "Not in this game";
		}
	}

	inline const char *global_preset_action_text(const global_preset_row &row)
	{
		return row.state == global_preset_state::in_game ? (row.current ? "Loaded" : "Load") :
			row.state == global_preset_state::update_available ? "Update" : "Import";
	}

	inline std::string global_preset_effects_text(const global_preset_row &row)
	{
		return row.missing_effect_count == 0 ? std::to_string(row.effect_count) :
			std::to_string(row.effect_count) + ", " + std::to_string(row.missing_effect_count) + " missing";
	}

	inline std::string global_current_text(const global_library_snapshot &library, const std::string &preset_name, bool unsaved_changes)
	{
		if (preset_name.empty())
			return "No preset";
		if (library.current_source.empty())
			return preset_name + " (only in this game)";
		return preset_name + " (copy of global \"" + library.current_source + "\" v" + std::to_string(library.current_source_version) +
			(library.current_changed || unsaved_changes ? ", changed here)" : ")");
	}

	inline std::string global_import_title(const global_import_summary &summary)
	{
		switch (summary.kind)
		{
		case global_import_kind::update_preset:
			return "Update \"" + summary.name + "\" in this game to v" + std::to_string(summary.version) + '?';
		case global_import_kind::add_effect:
			return "Add \"" + summary.name + "\" to this game?";
		default:
			return "Import \"" + summary.name + "\" into this game?";
		}
	}

	inline const char *global_import_action_text(global_import_kind kind)
	{
		return kind == global_import_kind::update_preset ? "Update and load" : kind == global_import_kind::add_effect ? "Add effect" : "Import and load";
	}

	inline std::string global_import_adds_text(const global_import_summary &summary)
	{
		const std::string effects = count_text(summary.added_effect_files.size(), "effect file", "effect files");
		const std::string textures = count_text(summary.added_textures.size(), "texture", "textures");
		if (summary.added_effect_files.empty() && summary.added_textures.empty())
			return "Adds no files: this game already has everything";
		if (summary.added_textures.empty())
			return "Adds " + effects;
		if (summary.added_effect_files.empty())
			return "Adds " + textures;
		return "Adds " + effects + " and " + textures;
	}

	inline std::string global_import_existing_text(const global_import_summary &summary)
	{
		return "Uses " + count_text(summary.existing_file_count, "file", "files") + " this game already has";
	}

	inline std::string global_import_different_text(const global_import_summary &summary)
	{
		return std::to_string(summary.different_files.size()) + (summary.different_files.size() == 1 ? " of them is" : " of them are") +
			" a different version in this game";
	}

	inline std::string global_import_incomplete_text(const global_import_summary &summary)
	{
		return count_text(summary.incomplete_effects.size(), "effect", "effects") + " may not work: some files were not found when saving";
	}

	// One name per line; a long list ends with "and N more".
	inline std::string global_name_list(const std::vector<std::string> &names, size_t maximum = 12)
	{
		std::string text;
		for (size_t index = 0; index < names.size() && index < maximum; ++index)
			text += (index == 0 ? "" : "\n") + names[index];
		if (names.size() > maximum)
			text += "\nand " + std::to_string(names.size() - maximum) + " more";
		return text;
	}
}