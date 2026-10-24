#pragma once

#include <cstdio>

// The build passes the release number from RESHADE_VR_UI_VERSION; 0 means a local or unreleased build.
#ifndef RESHADE_VR_UI_RELEASE_NUMBER
#define RESHADE_VR_UI_RELEASE_NUMBER 0
#endif

namespace openxr_ui
{
	inline constexpr unsigned int release_number = RESHADE_VR_UI_RELEASE_NUMBER;

	template <size_t size>
	inline void format_version_label(char (&buffer)[size])
	{
		if constexpr (release_number == 0)
			std::snprintf(buffer, size, "dev");
		else
			std::snprintf(buffer, size, "v%u", release_number);
	}
}
