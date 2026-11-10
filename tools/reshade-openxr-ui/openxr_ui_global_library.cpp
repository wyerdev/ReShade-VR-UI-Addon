#include "openxr_ui_global_library.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

#pragma comment(lib, "bcrypt.lib")

namespace
{
	using namespace openxr_ui::global_library;

	// Keeps a huge or looping folder tree from stalling the worker for minutes.
	constexpr size_t maximum_folders = 20000;
	constexpr size_t maximum_indexed_files = 200000;
	constexpr size_t maximum_effect_files = 2000;
	constexpr uintmax_t maximum_text_file_size = 16 * 1024 * 1024;

	std::wstring lower(std::wstring text)
	{
		for (wchar_t &character : text)
			character = static_cast<wchar_t>(std::towlower(character));
		return text;
	}

	std::wstring path_key(const std::filesystem::path &path)
	{
		return lower(path.lexically_normal().native());
	}

	bool is_file(const std::filesystem::path &path)
	{
		std::error_code error;
		return std::filesystem::is_regular_file(path, error);
	}

	bool read_text(const std::filesystem::path &path, std::string &text)
	{
		std::error_code error;
		const uintmax_t size = std::filesystem::file_size(path, error);
		if (error || size > maximum_text_file_size)
			return false;
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return true;
	}

	bool replace_file(const std::filesystem::path &temporary, const std::filesystem::path &destination)
	{
		return MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
	}

	bool write_text(const std::filesystem::path &path, const std::string &text)
	{
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		if (error)
			return false;
		const std::filesystem::path temporary = path.native() + L".tmp";
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		file.close();
		return file && replace_file(temporary, path);
	}

	bool copy_replacing(const std::filesystem::path &source, const std::filesystem::path &destination, std::string &error_text)
	{
		std::error_code error;
		std::filesystem::create_directories(destination.parent_path(), error);
		const std::filesystem::path temporary = destination.native() + L".tmp";
		if (!error)
			std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::overwrite_existing, error);
		if (error || !replace_file(temporary, destination))
		{
			error_text = "could not write " + destination.filename().u8string() + (error ? " (" + error.message() + ")" : std::string());
			std::filesystem::remove(temporary, error);
			return false;
		}
		return true;
	}

	// Every folder ReShade looks in: each search folder and, for recursive ones, all folders inside it.
	std::vector<std::filesystem::path> expand_folders(const std::vector<search_folder> &folders)
	{
		std::vector<std::filesystem::path> result;
		for (const search_folder &folder : folders)
		{
			std::error_code error;
			if (folder.path.empty() || !std::filesystem::is_directory(folder.path, error))
				continue;
			result.push_back(folder.path);
			if (!folder.recursive)
				continue;
			for (std::filesystem::recursive_directory_iterator it(folder.path, std::filesystem::directory_options::skip_permission_denied, error), end;
				!error && it != end && result.size() < maximum_folders; it.increment(error))
			{
				std::error_code type_error;
				if (it->is_directory(type_error))
					result.push_back(it->path());
			}
		}
		return result;
	}

	// Lower-case file name to the first file with that name, in ReShade's search order.
	std::unordered_map<std::wstring, std::filesystem::path> index_files(const std::vector<std::filesystem::path> &folders)
	{
		std::unordered_map<std::wstring, std::filesystem::path> index;
		for (const std::filesystem::path &folder : folders)
		{
			std::error_code error;
			for (std::filesystem::directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, error), end;
				!error && it != end && index.size() < maximum_indexed_files; it.increment(error))
			{
				std::error_code type_error;
				if (it->is_regular_file(type_error))
					index.try_emplace(lower(it->path().filename().native()), it->path());
			}
		}
		return index;
	}

	bool is_identifier_character(char character)
	{
		return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '_';
	}

	// Blanks out comments so commented-out includes and textures are not collected.
	void blank_comments(std::string &text)
	{
		enum class state { code, string, line_comment, block_comment } current = state::code;
		for (size_t index = 0; index < text.size(); ++index)
		{
			const char character = text[index];
			const char next = index + 1 < text.size() ? text[index + 1] : '\0';
			switch (current)
			{
			case state::code:
				if (character == '"')
					current = state::string;
				else if (character == '/' && next == '/')
					current = state::line_comment;
				else if (character == '/' && next == '*')
					current = state::block_comment;
				if (current == state::line_comment || current == state::block_comment)
					text[index] = ' ';
				break;
			case state::string:
				if (character == '\\')
					++index;
				else if (character == '"' || character == '\n')
					current = state::code;
				break;
			case state::line_comment:
				if (character == '\n')
					current = state::code;
				else
					text[index] = ' ';
				break;
			case state::block_comment:
				if (character == '*' && next == '/')
				{
					text[index] = text[index + 1] = ' ';
					++index;
					current = state::code;
				}
				else if (character != '\n')
					text[index] = ' ';
				break;
			}
		}
	}

	size_t skip_spaces(const std::string &text, size_t position)
	{
		while (position < text.size() && (text[position] == ' ' || text[position] == '\t'))
			++position;
		return position;
	}

	// True when `position` is inside an annotation list ("< name = value; ... >") rather than in shader code.
	bool inside_annotation(const std::string &text, size_t position)
	{
		for (size_t scanned = 0; position > 0 && scanned < 4000; ++scanned)
		{
			const char character = text[--position];
			if (character == '<')
				return true;
			if (character == '>' || character == '{' || character == '}')
				return false;
		}
		return false;
	}

	struct text_references
	{
		std::vector<std::string> includes;
		std::vector<std::string> textures;
		// A name that is not written out, for example one built by a preprocessor macro.
		bool unknown_include = false;
		bool unknown_texture = false;
	};

	void add_unique(std::vector<std::string> &values, std::string value)
	{
		if (std::none_of(values.begin(), values.end(), [&value](const std::string &existing) { return same_name(existing, value); }))
			values.push_back(std::move(value));
	}

	text_references find_references(std::string text)
	{
		blank_comments(text);
		text_references result;

		for (size_t line = 0; line < text.size(); line = text.find('\n', line) == std::string::npos ? text.size() : text.find('\n', line) + 1)
		{
			size_t position = skip_spaces(text, line);
			if (position >= text.size() || text[position] != '#')
				continue;
			position = skip_spaces(text, position + 1);
			if (text.compare(position, 7, "include") != 0 || (position + 7 < text.size() && is_identifier_character(text[position + 7])))
				continue;
			position = skip_spaces(text, position + 7);
			const size_t end = position < text.size() && text[position] == '"' ? text.find_first_of("\"\n", position + 1) : std::string::npos;
			if (end == std::string::npos || text[end] != '"')
				result.unknown_include = true;
			else if (end > position + 1)
				add_unique(result.includes, text.substr(position + 1, end - position - 1));
		}

		for (size_t found = text.find("source"); found != std::string::npos; found = text.find("source", found + 6))
		{
			if ((found > 0 && is_identifier_character(text[found - 1])) || (found + 6 < text.size() && is_identifier_character(text[found + 6])))
				continue;
			size_t position = skip_spaces(text, found + 6);
			if (position >= text.size() || text[position] != '=')
				continue;
			position = skip_spaces(text, position + 1);
			if (position >= text.size())
				continue;
			if (text[position] != '"')
			{
				if (is_identifier_character(text[position]) && inside_annotation(text, found))
					result.unknown_texture = true;
				continue;
			}
			const size_t end = text.find_first_of("\"\n", position + 1);
			if (end == std::string::npos || text[end] != '"')
				continue;
			// ReShade's own value sources ("timer", "frametime", ...) have no file extension.
			std::string value = text.substr(position + 1, end - position - 1);
			if (value.find('.') != std::string::npos)
				add_unique(result.textures, std::move(value));
		}
		return result;
	}

	effect_files collect_effect_files(const std::filesystem::path &effect_file, const std::vector<std::filesystem::path> &effect_folders,
		const std::vector<std::filesystem::path> &texture_folders)
	{
		effect_files result;
		result.files.push_back({ file_kind::effect, effect_file, -1, effect_file.filename().u8string() });
		std::unordered_set<std::wstring> read_files;
		for (size_t index = 0; index < result.files.size(); ++index)
		{
			const found_file file = result.files[index];
			if (file.kind == file_kind::texture || !read_files.insert(path_key(file.source)).second)
				continue;
			const std::string file_name = file.source.filename().u8string();
			std::string text;
			if (!read_text(file.source, text))
			{
				result.problems.push_back(file_name + " could not be read");
				continue;
			}
			const text_references references = find_references(std::move(text));
			if (references.unknown_include)
				result.problems.push_back(file_name + " has an #include whose file name is built by the preprocessor");
			if (references.unknown_texture)
				result.problems.push_back(file_name + " has a texture whose file name is built by the preprocessor");

			for (const std::string &reference : references.includes)
			{
				// ReShade looks next to the including file first, then in every effect folder.
				const std::filesystem::path relative = std::filesystem::u8path(reference);
				std::filesystem::path found = file.source.parent_path() / relative;
				if (!is_file(found))
				{
					found.clear();
					for (const std::filesystem::path &folder : effect_folders)
						if (is_file(folder / relative))
						{
							found = folder / relative;
							break;
						}
				}
				if (found.empty())
					result.problems.push_back(file_name + " includes \"" + reference + "\", which was not found");
				else if (result.files.size() < maximum_effect_files)
					result.files.push_back({ file_kind::include, found.lexically_normal(), static_cast<int>(index), reference });
			}
			for (const std::string &reference : references.textures)
			{
				const std::filesystem::path relative = std::filesystem::u8path(reference);
				std::filesystem::path found;
				if (!relative.is_absolute())
					for (const std::filesystem::path &folder : texture_folders)
						if (is_file(folder / relative))
						{
							found = folder / relative;
							break;
						}
				if (found.empty())
					result.problems.push_back(file_name + " uses the texture \"" + reference + "\", which was not found");
				else if (result.files.size() < maximum_effect_files)
					result.files.push_back({ file_kind::texture, found.lexically_normal(), static_cast<int>(index), reference });
			}
		}
		return result;
	}

	bool store_file(const std::filesystem::path &library, const std::filesystem::path &source, std::string &hash, std::string &error_text)
	{
		hash = hash_file(source);
		if (hash.empty())
		{
			error_text = "could not read " + source.filename().u8string();
			return false;
		}
		const std::filesystem::path destination = library / L"Files" / std::filesystem::u8path(hash) / source.filename();
		return is_file(destination) || copy_replacing(source, destination, error_text);
	}

	// Stores the files of one effect in the library and appends them to the manifest.
	bool add_effect(const std::filesystem::path &library, const std::filesystem::path &effect_file, const std::vector<std::filesystem::path> &effect_folders,
		const std::vector<std::filesystem::path> &texture_folders, manifest &result, std::vector<std::string> &problems, std::string &error_text)
	{
		const effect_files found = collect_effect_files(effect_file, effect_folders, texture_folders);
		const int base = static_cast<int>(result.files.size());
		for (const found_file &file : found.files)
		{
			file_reference entry;
			entry.kind = file.kind;
			entry.file_name = file.source.filename().u8string();
			entry.parent = file.parent < 0 ? -1 : file.parent + base;
			entry.reference = file.reference;
			if (!store_file(library, file.source, entry.hash, error_text))
				return false;
			result.files.push_back(std::move(entry));
		}
		if (!found.problems.empty())
			result.incomplete_effects.push_back(effect_file.filename().u8string());
		problems.insert(problems.end(), found.problems.begin(), found.problems.end());
		return true;
	}

	constexpr const char *kind_names[] = { "effect", "include", "texture" };

	std::string manifest_text(const manifest &files)
	{
		std::string text = "Version=" + std::to_string(files.version) + "\n";
		for (const file_reference &file : files.files)
			text += std::string("File=") + kind_names[static_cast<size_t>(file.kind)] + '|' + file.hash + '|' + std::to_string(file.parent) + '|' +
				file.file_name + '|' + file.reference + '\n';
		for (const std::string &effect : files.incomplete_effects)
			text += "Incomplete=" + effect + '\n';
		return text;
	}

	std::vector<std::string> split(const std::string &text, char separator, size_t maximum_parts)
	{
		std::vector<std::string> parts;
		size_t begin = 0;
		while (parts.size() + 1 < maximum_parts)
		{
			const size_t end = text.find(separator, begin);
			if (end == std::string::npos)
				break;
			parts.push_back(text.substr(begin, end - begin));
			begin = end + 1;
		}
		parts.push_back(text.substr(begin));
		return parts;
	}

	std::vector<std::string> text_lines(const std::string &text)
	{
		std::vector<std::string> lines;
		for (std::string &line : split(text, '\n', std::string::npos))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
				line.pop_back();
			if (!line.empty())
				lines.push_back(std::move(line));
		}
		return lines;
	}

	bool load_manifest(const std::filesystem::path &path, manifest &result)
	{
		std::string text;
		if (!read_text(path, text))
			return false;
		result = {};
		for (const std::string &line : text_lines(text))
		{
			if (line.rfind("Version=", 0) == 0)
				result.version = static_cast<uint32_t>(std::strtoul(line.c_str() + 8, nullptr, 10));
			else if (line.rfind("Incomplete=", 0) == 0)
				result.incomplete_effects.push_back(line.substr(11));
			else if (line.rfind("File=", 0) == 0)
			{
				const std::vector<std::string> parts = split(line.substr(5), '|', 5);
				const auto kind = std::find(std::begin(kind_names), std::end(kind_names), parts[0]);
				if (parts.size() != 5 || kind == std::end(kind_names))
					return false;
				file_reference file;
				file.kind = static_cast<file_kind>(kind - std::begin(kind_names));
				file.hash = parts[1];
				file.parent = std::atoi(parts[2].c_str());
				file.file_name = parts[3];
				file.reference = parts[4];
				// A parent always comes before the files it refers to.
				if (file.parent >= static_cast<int>(result.files.size()) || file.hash.empty() || file.file_name.empty())
					return false;
				result.files.push_back(std::move(file));
			}
		}
		return true;
	}

	std::filesystem::path preset_manifest_path(const std::filesystem::path &library, const std::string &name)
	{
		return library / L"Presets" / (std::filesystem::u8path(name).native() + L".txt");
	}

	std::filesystem::path effect_manifest_path(const std::filesystem::path &library, const std::string &effect_file_name)
	{
		return library / L"Effects" / (std::filesystem::u8path(effect_file_name).native() + L".txt");
	}

	bool is_inside(const std::filesystem::path &path, const std::filesystem::path &folder)
	{
		const std::wstring path_text = path_key(path);
		std::wstring folder_text = path_key(folder);
		if (folder_text.empty())
			return false;
		if (folder_text.back() != L'\\')
			folder_text += L'\\';
		return path_text.compare(0, folder_text.size(), folder_text) == 0;
	}

	// Creates the folder and remembers every folder that did not exist before, parents first.
	bool create_folder(const std::filesystem::path &folder, std::vector<std::filesystem::path> &created)
	{
		std::vector<std::filesystem::path> missing;
		std::error_code error;
		for (std::filesystem::path current = folder; !current.empty() && !std::filesystem::exists(current, error); current = current.parent_path())
		{
			missing.push_back(current);
			if (current == current.root_path())
				break;
		}
		std::filesystem::create_directories(folder, error);
		if (error)
			return false;
		created.insert(created.end(), missing.rbegin(), missing.rend());
		return true;
	}
}

bool openxr_ui::global_library::valid_name(const std::string &name)
{
	if (name.empty() || name.size() > 120 || name == "." || name == ".." || name.back() == ' ' || name.back() == '.')
		return false;
	return std::none_of(name.begin(), name.end(), [](unsigned char character) {
		return character < 32 || std::strchr("<>:\"/\\|?*", character) != nullptr;
	});
}

bool openxr_ui::global_library::same_name(const std::string &left, const std::string &right)
{
	const std::wstring wide_left = std::filesystem::u8path(left).native();
	const std::wstring wide_right = std::filesystem::u8path(right).native();
	return CompareStringOrdinal(wide_left.c_str(), -1, wide_right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::string openxr_ui::global_library::hash_file(const std::filesystem::path &path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return {};
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	unsigned char digest[32] = {};
	bool succeeded = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
		BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0));
	std::vector<char> buffer(64 * 1024);
	while (succeeded && file)
	{
		file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize count = file.gcount();
		if (count > 0)
			succeeded = BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0));
	}
	succeeded = succeeded && file.eof() && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
	if (hash != nullptr)
		BCryptDestroyHash(hash);
	if (algorithm != nullptr)
		BCryptCloseAlgorithmProvider(algorithm, 0);
	if (!succeeded)
		return {};

	// Half of the SHA-256 value keeps folder names short and is still far beyond any chance of two files matching.
	std::string text;
	for (size_t index = 0; index < 16; ++index)
	{
		constexpr char digits[] = "0123456789abcdef";
		text += digits[digest[index] >> 4];
		text += digits[digest[index] & 0xF];
	}
	return text;
}

effect_files openxr_ui::global_library::find_effect_files(const std::filesystem::path &effect_file, const game_folders &folders)
{
	return collect_effect_files(effect_file, expand_folders(folders.effects), expand_folders(folders.textures));
}

std::vector<std::string> openxr_ui::global_library::preset_effect_names(const std::filesystem::path &preset_file)
{
	std::vector<std::string> names;
	std::string text;
	if (!read_text(preset_file, text))
		return names;
	if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
		text.erase(0, 3);
	for (const std::string &line : text_lines(text))
	{
		const size_t start = line.find_first_not_of(" \t");
		// Techniques sits in the top section, before any [effect] section.
		if (line[start] == '[')
			break;
		const size_t assign = line.find('=', start);
		if (assign == std::string::npos || line.substr(start, line.find_last_not_of(" \t", assign - 1) + 1 - start) != "Techniques")
			continue;
		for (const std::string &technique : split(line.substr(assign + 1), ',', std::string::npos))
		{
			const size_t at = technique.find('@');
			if (at == std::string::npos)
				continue;
			std::string effect = technique.substr(at + 1);
			effect.erase(0, effect.find_first_not_of(" \t"));
			effect.erase(effect.find_last_not_of(" \t") + 1);
			if (!effect.empty())
				add_unique(names, std::move(effect));
		}
	}
	return names;
}

std::vector<std::wstring> openxr_ui::global_library::effect_folder_file_names(const game_folders &folders)
{
	std::vector<std::wstring> names;
	for (const auto &entry : index_files(expand_folders(folders.effects)))
		names.push_back(entry.first);
	std::sort(names.begin(), names.end());
	return names;
}

bool openxr_ui::global_library::contains_file_name(const std::vector<std::wstring> &sorted_names, const std::string &file_name)
{
	return std::binary_search(sorted_names.begin(), sorted_names.end(), lower(std::filesystem::u8path(file_name).native()));
}

bool openxr_ui::global_library::save_preset(const std::filesystem::path &library, const std::string &name, const std::filesystem::path &preset_file,
	const game_folders &folders, uint32_t &version, std::vector<std::string> &problems, std::string &error)
{
	if (!valid_name(name) || library.empty())
	{
		error = "the name cannot be used as a file name";
		return false;
	}
	if (!is_file(preset_file))
	{
		error = "the current preset file was not found";
		return false;
	}
	const std::vector<std::filesystem::path> effect_folders = expand_folders(folders.effects);
	const std::vector<std::filesystem::path> texture_folders = expand_folders(folders.textures);
	const std::unordered_map<std::wstring, std::filesystem::path> effects = index_files(effect_folders);

	manifest files;
	for (const std::string &effect_name : preset_effect_names(preset_file))
	{
		const auto effect = effects.find(lower(std::filesystem::u8path(effect_name).native()));
		if (effect == effects.end())
		{
			problems.push_back(effect_name + " is used by the preset but is not in this game's effect folders");
			files.incomplete_effects.push_back(effect_name);
			continue;
		}
		if (!add_effect(library, effect->second, effect_folders, texture_folders, files, problems, error))
			return false;
	}

	manifest previous;
	files.version = (load_preset_manifest(library, name, previous) ? previous.version : 0) + 1;
	if (!copy_replacing(preset_file, preset_path(library, name), error))
		return false;
	if (!write_text(preset_manifest_path(library, name), manifest_text(files)))
	{
		error = "could not write the file list of the global preset";
		return false;
	}
	version = files.version;
	return true;
}

bool openxr_ui::global_library::save_starred_effect(const std::filesystem::path &library, const std::string &effect_file_name,
	const game_folders &folders, std::vector<std::string> &problems, std::string &error)
{
	const std::vector<std::filesystem::path> effect_folders = expand_folders(folders.effects);
	const std::unordered_map<std::wstring, std::filesystem::path> effects = index_files(effect_folders);
	const auto effect = effects.find(lower(std::filesystem::u8path(effect_file_name).native()));
	if (library.empty() || effect == effects.end())
	{
		error = effect_file_name + " is not in this game's effect folders";
		return false;
	}
	manifest files;
	files.version = 1;
	if (!add_effect(library, effect->second, effect_folders, expand_folders(folders.textures), files, problems, error))
		return false;
	if (!write_text(effect_manifest_path(library, effect_file_name), manifest_text(files)))
	{
		error = "could not write the file list of " + effect_file_name;
		return false;
	}
	return true;
}

std::vector<preset_entry> openxr_ui::global_library::list_presets(const std::filesystem::path &library)
{
	std::vector<preset_entry> presets;
	std::error_code error;
	for (std::filesystem::directory_iterator it(library / L"Presets", std::filesystem::directory_options::skip_permission_denied, error), end;
		!error && it != end; it.increment(error))
	{
		std::error_code type_error;
		if (!it->is_regular_file(type_error) || lower(it->path().extension().native()) != L".ini")
			continue;
		preset_entry entry;
		entry.name = it->path().stem().u8string();
		if (load_preset_manifest(library, entry.name, entry.files))
			presets.push_back(std::move(entry));
	}
	std::sort(presets.begin(), presets.end(), [](const preset_entry &left, const preset_entry &right) {
		return lower(std::filesystem::u8path(left.name).native()) < lower(std::filesystem::u8path(right.name).native());
	});
	return presets;
}

bool openxr_ui::global_library::load_preset_manifest(const std::filesystem::path &library, const std::string &name, manifest &result)
{
	return valid_name(name) && load_manifest(preset_manifest_path(library, name), result);
}

bool openxr_ui::global_library::load_effect_manifest(const std::filesystem::path &library, const std::string &effect_file_name, manifest &result)
{
	return valid_name(effect_file_name) && load_manifest(effect_manifest_path(library, effect_file_name), result);
}

std::filesystem::path openxr_ui::global_library::preset_path(const std::filesystem::path &library, const std::string &name)
{
	return library / L"Presets" / (std::filesystem::u8path(name).native() + L".ini");
}

import_plan openxr_ui::global_library::plan_import(const std::filesystem::path &library, const manifest &files, const game_folders &folders)
{
	import_plan plan;
	const std::vector<std::filesystem::path> effect_folders = expand_folders(folders.effects);
	const std::vector<std::filesystem::path> texture_folders = expand_folders(folders.textures);
	const std::unordered_map<std::wstring, std::filesystem::path> effects = index_files(effect_folders);
	// Where each file is, or will be, in the game; the files it refers to are looked up from there.
	std::vector<std::filesystem::path> locations(files.files.size());
	std::unordered_set<std::wstring> planned;
	const auto present = [&planned](const std::filesystem::path &candidate) {
		return is_file(candidate) || planned.find(path_key(candidate)) != planned.end();
	};
	const auto note = [](std::vector<std::string> &names, const std::string &name) {
		add_unique(names, name);
	};

	for (size_t index = 0; index < files.files.size(); ++index)
	{
		const file_reference &file = files.files[index];
		const std::filesystem::path reference = std::filesystem::u8path(file.reference);
		std::filesystem::path existing;
		std::filesystem::path destination;
		if (file.kind == file_kind::effect)
		{
			// By file name anywhere in the effect folders: a second copy would load the effect twice.
			const auto effect = effects.find(lower(std::filesystem::u8path(file.file_name).native()));
			if (effect != effects.end())
				existing = effect->second;
			else if (!folders.effects.empty())
				destination = folders.effects.front().path / std::filesystem::u8path(file.file_name);
		}
		else if (file.parent < 0 || locations[static_cast<size_t>(file.parent)].empty() || reference.is_absolute())
		{
			continue;
		}
		else if (file.kind == file_kind::include)
		{
			const std::filesystem::path next_to_parent = locations[static_cast<size_t>(file.parent)].parent_path() / reference;
			if (present(next_to_parent))
				existing = next_to_parent;
			else
				for (const std::filesystem::path &folder : effect_folders)
					if (present(folder / reference))
					{
						existing = folder / reference;
						break;
					}
			const bool inside_effect_folder = std::any_of(folders.effects.begin(), folders.effects.end(), [&next_to_parent](const search_folder &folder) {
				return is_inside(next_to_parent, folder.path);
			});
			if (existing.empty() && inside_effect_folder)
				destination = next_to_parent;
		}
		else
		{
			for (const std::filesystem::path &folder : texture_folders)
				if (present(folder / reference))
				{
					existing = folder / reference;
					break;
				}
			if (existing.empty() && !folders.textures.empty() && is_inside(folders.textures.front().path / reference, folders.textures.front().path))
				destination = folders.textures.front().path / reference;
		}

		if (!existing.empty())
		{
			locations[index] = existing.lexically_normal();
			// A file this import adds itself is not one the game already has.
			if (planned.find(path_key(existing)) != planned.end())
				continue;
			++plan.existing_file_count;
			if (hash_file(existing) != file.hash)
				note(plan.different_files, file.file_name);
			continue;
		}
		if (destination.empty())
		{
			plan.error = "this game's ReShade has no folder where " + file.file_name + " can be added";
			return plan;
		}
		const std::filesystem::path source = library / L"Files" / std::filesystem::u8path(file.hash) / std::filesystem::u8path(file.file_name);
		if (!is_file(source))
		{
			plan.error = file.file_name + " is missing from the shared library";
			return plan;
		}
		destination = destination.lexically_normal();
		locations[index] = destination;
		planned.insert(path_key(destination));
		plan.copies.push_back({ source, destination });
		note(file.kind == file_kind::texture ? plan.added_textures : plan.added_effect_files, file.file_name);
	}
	return plan;
}

bool openxr_ui::global_library::copy_files(const import_plan &plan, copy_result &done, std::string &error)
{
	for (const planned_copy &copy : plan.copies)
	{
		std::error_code copy_error;
		// copy_options::none refuses to replace a file that appeared in the meantime.
		if (!create_folder(copy.destination.parent_path(), done.folders) ||
			!std::filesystem::copy_file(copy.source, copy.destination, std::filesystem::copy_options::none, copy_error))
		{
			error = "could not copy " + copy.destination.filename().u8string() + " to " + copy.destination.parent_path().u8string() +
				(copy_error ? " (" + copy_error.message() + ")" : std::string());
			undo_copy(done);
			done = {};
			return false;
		}
		done.files.push_back(copy.destination);
	}
	return true;
}

void openxr_ui::global_library::undo_copy(const copy_result &done)
{
	std::error_code error;
	for (const std::filesystem::path &file : done.files)
		std::filesystem::remove(file, error);
	// Removes only empty folders, innermost first.
	for (auto folder = done.folders.rbegin(); folder != done.folders.rend(); ++folder)
		std::filesystem::remove(*folder, error);
}

bool openxr_ui::global_library::copy_preset(const std::filesystem::path &library, const std::string &name,
	const std::filesystem::path &destination, bool replace, std::string &error)
{
	std::error_code copy_error;
	std::filesystem::create_directories(destination.parent_path(), copy_error);
	if (copy_error || !std::filesystem::copy_file(preset_path(library, name), destination,
		replace ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none, copy_error))
	{
		error = "could not write the preset " + destination.filename().u8string() + (copy_error ? " (" + copy_error.message() + ")" : std::string());
		return false;
	}
	return true;
}

std::vector<import_record> openxr_ui::global_library::load_records(const std::filesystem::path &records_file)
{
	std::vector<import_record> records;
	std::string text;
	if (!read_text(records_file, text))
		return records;
	for (const std::string &line : text_lines(text))
	{
		const std::vector<std::string> parts = split(line, '|', 4);
		if (parts.size() != 4)
			continue;
		import_record record;
		record.preset_file = std::filesystem::u8path(parts[0]);
		record.name = parts[1];
		record.version = static_cast<uint32_t>(std::strtoul(parts[2].c_str(), nullptr, 10));
		record.hash = parts[3];
		records.push_back(std::move(record));
	}
	return records;
}

bool openxr_ui::global_library::save_record(const std::filesystem::path &records_file, const import_record &record)
{
	std::vector<import_record> records = load_records(records_file);
	// Presets deleted since they were imported drop out here.
	records.erase(std::remove_if(records.begin(), records.end(), [&record](const import_record &existing) {
		return path_key(existing.preset_file) == path_key(record.preset_file) || !is_file(existing.preset_file);
	}), records.end());
	records.push_back(record);
	std::string text;
	for (const import_record &entry : records)
		text += entry.preset_file.u8string() + '|' + entry.name + '|' + std::to_string(entry.version) + '|' + entry.hash + '\n';
	return write_text(records_file, text);
}
