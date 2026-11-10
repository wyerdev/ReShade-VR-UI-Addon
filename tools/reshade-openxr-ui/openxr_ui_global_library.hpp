#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The library of presets and starred effects shared by all games: its folder layout, finding the files an effect
// needs, comparing them with a game's folders and copying the missing ones. Everything here reads or writes the
// disk, so call it from a worker thread only. It uses no graphics API and no ReShade API.
//
// Layout below the library folder:
//   Presets\<name>.ini        the preset in ReShade's format
//   Presets\<name>.txt        its version and the files it needs
//   Effects\<effect file>.txt the files a starred effect needs
//   Files\<hash>\<file name>  one folder per distinct file content
//
// The library folder must never be one of ReShade's search folders: ReShade loads every effect file it finds, so an
// effect a game already has would load twice. Effects reach a game only by copying the files it lacks.
namespace openxr_ui::global_library
{
	// One entry of ReShade's EffectSearchPaths or TextureSearchPaths, resolved to a full path.
	struct search_folder
	{
		std::filesystem::path path;
		// The entry ended with "\**", so ReShade also searches every folder inside it.
		bool recursive = false;
	};

	struct game_folders
	{
		std::vector<search_folder> effects;
		std::vector<search_folder> textures;
	};

	enum class file_kind
	{
		effect,
		include,
		texture,
	};

	// One "#include" or texture "source" as written in the file at index `parent`; an effect has no parent.
	struct file_reference
	{
		file_kind kind = file_kind::effect;
		std::string hash;
		std::string file_name;
		int parent = -1;
		std::string reference;
	};

	struct manifest
	{
		uint32_t version = 0;
		std::vector<file_reference> files;
		// Effects with a file that could not be found, for example a name built by the preprocessor.
		std::vector<std::string> incomplete_effects;
	};

	struct found_file
	{
		file_kind kind = file_kind::effect;
		std::filesystem::path source;
		int parent = -1;
		std::string reference;
	};

	struct effect_files
	{
		std::vector<found_file> files;
		// What could not be found, in words for the log. Empty when the effect is complete.
		std::vector<std::string> problems;
	};

	struct preset_entry
	{
		std::string name;
		manifest files;
	};

	struct planned_copy
	{
		std::filesystem::path source;
		std::filesystem::path destination;
	};

	struct import_plan
	{
		// Only files the game lacks; a file the game has is never replaced.
		std::vector<planned_copy> copies;
		std::vector<std::string> added_effect_files;
		std::vector<std::string> added_textures;
		uint32_t existing_file_count = 0;
		// Files the game has with other content than the library's.
		std::vector<std::string> different_files;
		// Not empty when the plan cannot be carried out.
		std::string error;
	};

	// What an import added to the game, so a failed import can take it out again.
	struct copy_result
	{
		std::vector<std::filesystem::path> files;
		std::vector<std::filesystem::path> folders;
	};

	// A preset in a game's preset folder that was imported from a global preset.
	struct import_record
	{
		std::filesystem::path preset_file;
		std::string name;
		uint32_t version = 0;
		// Content of the game's copy when it was imported or saved back.
		std::string hash;
	};

	bool valid_name(const std::string &name);
	bool same_name(const std::string &left, const std::string &right);
	// Empty when the file cannot be read.
	std::string hash_file(const std::filesystem::path &path);

	// Follows "#include" lines and texture "source" annotations from the effect file, the way ReShade looks them up.
	effect_files find_effect_files(const std::filesystem::path &effect_file, const game_folders &folders);
	// Effect file names of the techniques a preset turns on.
	std::vector<std::string> preset_effect_names(const std::filesystem::path &preset_file);
	// Lower-case names of all files in the game's effect folders.
	std::vector<std::wstring> effect_folder_file_names(const game_folders &folders);
	bool contains_file_name(const std::vector<std::wstring> &sorted_names, const std::string &file_name);

	// Saves the preset and every file its effects need; `version` receives the old version plus one.
	bool save_preset(const std::filesystem::path &library, const std::string &name, const std::filesystem::path &preset_file,
		const game_folders &folders, uint32_t &version, std::vector<std::string> &problems, std::string &error);
	bool save_starred_effect(const std::filesystem::path &library, const std::string &effect_file_name, const game_folders &folders,
		std::vector<std::string> &problems, std::string &error);
	std::vector<preset_entry> list_presets(const std::filesystem::path &library);
	bool load_preset_manifest(const std::filesystem::path &library, const std::string &name, manifest &result);
	bool load_effect_manifest(const std::filesystem::path &library, const std::string &effect_file_name, manifest &result);
	std::filesystem::path preset_path(const std::filesystem::path &library, const std::string &name);

	import_plan plan_import(const std::filesystem::path &library, const manifest &files, const game_folders &folders);
	// Copies the planned files. When one fails, the files already copied are removed again and `error` says why.
	bool copy_files(const import_plan &plan, copy_result &done, std::string &error);
	void undo_copy(const copy_result &done);
	// Puts the library's preset into the game's preset folder; `replace` allows overwriting the game's copy.
	bool copy_preset(const std::filesystem::path &library, const std::string &name, const std::filesystem::path &destination,
		bool replace, std::string &error);

	std::vector<import_record> load_records(const std::filesystem::path &records_file);
	bool save_record(const std::filesystem::path &records_file, const import_record &record);
}
