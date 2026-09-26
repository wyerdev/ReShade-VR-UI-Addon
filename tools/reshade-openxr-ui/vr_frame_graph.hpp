#pragma once

// Include after imgui.h (and reshade.hpp in the SteamVR add-on); the two add-ons draw with different ImGui builds.
#include "vr_frame_statistics.hpp"

#include <algorithm>
#include <cstdio>

namespace openxr_ui
{
	// For example "5 s, 90 Hz" or "5 s, 45 of 90 Hz"; a question mark means the headset has not reported its rate yet.
	inline void format_frame_graph_label(const frame_statistics &statistics, char (&label)[32])
	{
		const char *const unreported = statistics.refresh_hz_reported ? "" : "?";
		if (statistics.frame_rate_divisor > 1)
			std::snprintf(label, sizeof(label), "%.0f s, %.0f of %.0f Hz%s", frame_graph_seconds, statistics.target_hz(),
				statistics.refresh_hz, unreported);
		else
			std::snprintf(label, sizeof(label), "%.0f s, %.0f Hz%s", frame_graph_seconds, statistics.refresh_hz, unreported);
	}

	// Bars show frame times, newest on the right. Horizontal guides mark one, two and three target frame times;
	// vertical guides mark each second.
	inline void draw_frame_graph(const frame_statistics &statistics, ImVec2 size)
	{
		constexpr int refresh_guides = 3;
		const float refresh_ms = 1000.0f / statistics.target_hz();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::Dummy(size);
		const float right = origin.x + size.x;
		const float bottom = origin.y + size.y;
		const float pixels_per_ms = size.y / (refresh_ms * (refresh_guides + 1));
		draw_list->AddRectFilled(origin, { right, bottom }, IM_COL32(0, 0, 0, 120));
		for (int second = 1; second < static_cast<int>(frame_graph_seconds); ++second)
		{
			const float x = origin.x + size.x * static_cast<float>(second / frame_graph_seconds);
			draw_list->AddLine({ x, origin.y }, { x, bottom }, IM_COL32(255, 255, 255, 35));
		}

		const float column_width = size.x / static_cast<float>(frame_graph_columns);
		for (size_t index = 0; index < frame_graph_columns; ++index)
		{
			const float milliseconds = statistics.graph_ms[index];
			if (milliseconds <= 0.0f)
				continue;
			const ImU32 color = milliseconds <= refresh_ms * 1.5f ? IM_COL32(80, 200, 120, 255) :
				milliseconds <= refresh_ms * 2.5f ? IM_COL32(235, 200, 60, 255) : IM_COL32(235, 70, 60, 255);
			const float left = origin.x + column_width * static_cast<float>(index);
			const float height = (std::min)(milliseconds * pixels_per_ms, size.y);
			draw_list->AddRectFilled({ left, bottom - height }, { left + column_width, bottom }, color);
		}

		for (int guide = 1; guide <= refresh_guides; ++guide)
		{
			const float y = bottom - refresh_ms * static_cast<float>(guide) * pixels_per_ms;
			draw_list->AddLine({ origin.x, y }, { right, y }, IM_COL32(255, 255, 255, 70));
		}
	}
}
