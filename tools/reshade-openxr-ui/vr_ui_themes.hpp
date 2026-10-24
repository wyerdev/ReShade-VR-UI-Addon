#pragma once

#include <cstdint>
#include <cstring>

namespace vr_theme
{
	// Names shown in the menu's theme list and stored as Theme= in settings.ini. The order matches the palettes in
	// vr_ui_theme.hpp; the first entry is the default.
	inline constexpr const char *theme_names[] = {
		"Violet Night",
		"Plum",
		"Slate",
	};
	inline constexpr uint32_t theme_count = static_cast<uint32_t>(sizeof(theme_names) / sizeof(theme_names[0]));
	inline constexpr uint32_t default_theme = 0;

	// Unknown names fall back to the default, so a typo in settings.ini never leaves the panel without colors.
	inline uint32_t find_theme(const char *name)
	{
		for (uint32_t index = 0; name != nullptr && index < theme_count; ++index)
			if (std::strcmp(theme_names[index], name) == 0)
				return index;
		return default_theme;
	}
}
