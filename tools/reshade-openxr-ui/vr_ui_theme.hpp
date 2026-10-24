#pragma once

// Include after imgui.h. Every color of the headset panel comes from the active palette here; drawing code never uses
// raw color values. See .github/skills/vr-ui-design/SKILL.md for the rules behind these choices.
//
// The themes are this project's own palettes, designed for this panel; never copy another project's palette or theme.
// Each theme_source picks one hue for its surfaces and steps it in clear lightness levels. make_palette turns those
// colors into the panel's roles with one shared rule, so every theme gets the same structure: an opaque background,
// slightly lighter rows, lighter controls, one accent for selection and the main action, and success/warning/danger
// colors that appear only for a real state. To add a theme, add its name to vr_ui_themes.hpp and its colors below.

#include "vr_ui_themes.hpp"

#include <atomic>

namespace vr_theme
{
	struct theme_source
	{
		ImU32 background;    // Panel background.
		ImU32 inset;         // Text field and open drop-down list, darker than the background.
		ImU32 row;           // List rows and notices.
		ImU32 control;       // Buttons, slider tracks, unchecked boxes.
		ImU32 control_hover; // Hovered buttons; also drop-down borders.
		ImU32 muted;         // Disabled text, idle scroll bar and box outlines.
		ImU32 subtext;       // Labels and secondary information.
		ImU32 text;
		ImU32 accent;
		ImU32 warning;       // Also marks values changed from their default.
		ImU32 success;
		ImU32 danger;
		ImU32 on_accent;     // Text on accent, success, warning and danger fills.
	};

	struct palette
	{
		ImU32 background;
		ImU32 inset;
		ImU32 surface;
		ImU32 surface_raised;
		ImU32 surface_hover;
		ImU32 border;
		ImU32 control_idle;
		ImU32 shadow;
		ImU32 text;
		ImU32 text_secondary;
		ImU32 text_disabled;
		ImU32 text_on_color;
		ImU32 accent;
		ImU32 accent_hover;
		ImU32 accent_muted;
		ImU32 changed_value;
		ImU32 warning;
		ImU32 success;
		ImU32 danger;
		ImU32 danger_hover;
	};

	constexpr ImU32 rgb(uint32_t hex)
	{
		return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, 0xFF);
	}

	constexpr ImU32 mix(ImU32 from, ImU32 to, float amount)
	{
		ImU32 result = 0;
		for (int shift = 0; shift < 32; shift += 8)
		{
			const float a = static_cast<float>((from >> shift) & 0xFF);
			const float b = static_cast<float>((to >> shift) & 0xFF);
			result |= static_cast<ImU32>(a + (b - a) * amount + 0.5f) << shift;
		}
		return result;
	}

	constexpr ImU32 with_alpha(ImU32 color, uint32_t alpha)
	{
		return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
	}

	constexpr palette make_palette(const theme_source &source)
	{
		palette result = {};
		result.background = source.background;
		result.inset = source.inset;
		result.surface = source.row;
		result.surface_raised = source.control;
		result.surface_hover = source.control_hover;
		result.border = source.control_hover;
		result.control_idle = source.muted;
		result.shadow = with_alpha(mix(source.inset, IM_COL32_BLACK, 0.5f), 170);
		result.text = source.text;
		result.text_secondary = source.subtext;
		result.text_disabled = source.muted;
		result.text_on_color = source.on_accent;
		result.accent = source.accent;
		result.accent_hover = mix(source.accent, source.text, 0.35f);
		result.accent_muted = mix(source.control, source.accent, 0.35f);
		result.changed_value = source.warning;
		result.warning = source.warning;
		result.success = source.success;
		result.danger = source.danger;
		result.danger_hover = mix(source.danger, source.text, 0.3f);
		return result;
	}

	// Order per theme: background, inset, row, control, hovered control, muted, subtext, text, accent, warning, success,
	// danger, text on accent.
	inline constexpr palette palettes[] = {
		// Violet Night: violet-black surfaces (hue about 265) with a soft violet accent.
		make_palette({ rgb(0x17131F), rgb(0x110E18), rgb(0x221C2E), rgb(0x2F2740), rgb(0x3F3555), rgb(0x6E6385), rgb(0xADA3C2),
			rgb(0xECE7F5), rgb(0xB48CFF), rgb(0xF2C46D), rgb(0x7FD4A0), rgb(0xF07A8A), rgb(0x17131F) }),
		// Plum: warmer red-purple surfaces (hue about 315) with an orchid accent.
		make_palette({ rgb(0x1C1219), rgb(0x150D13), rgb(0x281A24), rgb(0x362331), rgb(0x4A3044), rgb(0x7A6274), rgb(0xC2A9BA),
			rgb(0xF4E9F0), rgb(0xE08AD0), rgb(0xF0C070), rgb(0x86D19C), rgb(0xF27A7A), rgb(0x1C1219) }),
		// Slate: calm blue-grey surfaces (hue about 225) with a violet accent, for less purple overall.
		make_palette({ rgb(0x14161C), rgb(0x0F1115), rgb(0x1E212A), rgb(0x2A2E3A), rgb(0x3A3F4E), rgb(0x646B7E), rgb(0xA6ADBF),
			rgb(0xE8EBF2), rgb(0x9D8CFF), rgb(0xEBC46E), rgb(0x78CFA0), rgb(0xEE7C86), rgb(0x14161C) }),
	};
	static_assert(sizeof(palettes) / sizeof(palettes[0]) == theme_count, "Every name in vr_ui_themes.hpp needs a palette.");

	// One theme for every panel in the process; the panel and the FPS counter switch together.
	inline std::atomic<uint32_t> active_theme { default_theme };

	inline void select_theme(uint32_t index)
	{
		active_theme.store(index < theme_count ? index : default_theme, std::memory_order_relaxed);
	}

	inline uint32_t selected_theme()
	{
		return active_theme.load(std::memory_order_relaxed);
	}

	inline const palette &current()
	{
		return palettes[selected_theme()];
	}

	// Shape and type do not change with the theme.
	inline constexpr float radius_sm = 6.0f;
	inline constexpr float radius_md = 8.0f;
	inline constexpr float radius_panel = 14.0f;
	inline constexpr float stroke = 2.0f;
	inline constexpr float stroke_thin = 1.5f;

	inline constexpr float font_body = 22.0f;
	inline constexpr float font_control = 20.0f;
	inline constexpr float font_small = 18.0f;
	inline constexpr float font_title = 24.0f;

	inline ImVec4 to_float4(ImU32 color)
	{
		return ImGui::ColorConvertU32ToFloat4(color);
	}
}
