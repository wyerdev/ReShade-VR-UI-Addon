#pragma once

#include <istream>
#include <string>
#include <string_view>

namespace openxr_ui
{
	// ReShade treats a .ini or .txt file as a preset only when its top section (before any [name] header) has a
	// Techniques key. This reads the file the same way ReShade's ini_file does, so the menu lists exactly the files
	// ReShade itself accepts as presets and skips game, mod and ReShade settings files in the same folder.
	inline bool text_is_reshade_preset(std::istream &stream)
	{
		const auto trim = [](std::string_view text, std::string_view characters) {
			const size_t first = text.find_first_not_of(characters);
			if (first == std::string_view::npos)
				return std::string_view();
			return text.substr(first, text.find_last_not_of(characters) - first + 1);
		};

		bool in_top_section = true;
		bool first_line = true;
		std::string line_data;
		while (std::getline(stream, line_data))
		{
			std::string_view line = line_data;
			if (first_line && line.substr(0, 3) == "\xEF\xBB\xBF")
				line.remove_prefix(3);
			first_line = false;
			line = trim(line, " \t\r\n");
			if (line.empty() || line[0] == ';' || line[0] == '/' || line[0] == '#')
				continue;
			if (line[0] == '[')
			{
				in_top_section = trim(line.substr(0, line.find(']')), " \t[]").empty();
				continue;
			}
			const size_t assign_index = line.find('=');
			if (in_top_section && assign_index != std::string_view::npos && trim(line.substr(0, assign_index), " \t") == "Techniques")
				return true;
		}
		return false;
	}
}
