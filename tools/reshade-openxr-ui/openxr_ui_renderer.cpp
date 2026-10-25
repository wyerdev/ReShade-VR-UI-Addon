#include "openxr_ui_renderer.hpp"

#include <d3d11.h>
#include <d3d12.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_opengl3.h>

#include "vr_frame_graph.hpp"
#include "vr_ui_theme.hpp"
#include "vr_ui_version.hpp"
#include "vr_ui_fonts.inc"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <type_traits>
#include <vector>

namespace
{
	std::mutex s_imgui_mutex;

	constexpr unsigned int gl_texture_2d = 0x0DE1;
	constexpr unsigned int gl_scissor_test = 0x0C11;
	constexpr unsigned int gl_color_clear_value = 0x0C22;
	constexpr unsigned int gl_color_writemask = 0x0C23;
	constexpr unsigned int gl_color_buffer_bit = 0x4000;
	constexpr unsigned int gl_draw_framebuffer = 0x8CA9;
	constexpr unsigned int gl_draw_framebuffer_binding = 0x8CA6;
	constexpr unsigned int gl_color_attachment0 = 0x8CE0;
	constexpr unsigned int gl_framebuffer_complete = 0x8CD5;
	constexpr unsigned int gl_framebuffer_srgb = 0x8DB9;

	// Loaded at run time so Direct3D processes never depend on OpenGL.
	struct opengl_functions
	{
		HGLRC(WINAPI *GetCurrentContext)() = nullptr;
		void(APIENTRY *GetIntegerv)(unsigned int, int *) = nullptr;
		void(APIENTRY *GetFloatv)(unsigned int, float *) = nullptr;
		void(APIENTRY *GetBooleanv)(unsigned int, unsigned char *) = nullptr;
		unsigned char(APIENTRY *IsEnabled)(unsigned int) = nullptr;
		void(APIENTRY *Enable)(unsigned int) = nullptr;
		void(APIENTRY *Disable)(unsigned int) = nullptr;
		void(APIENTRY *ColorMask)(unsigned char, unsigned char, unsigned char, unsigned char) = nullptr;
		void(APIENTRY *ClearColor)(float, float, float, float) = nullptr;
		void(APIENTRY *Clear)(unsigned int) = nullptr;
		void(APIENTRY *GenFramebuffers)(int, unsigned int *) = nullptr;
		void(APIENTRY *DeleteFramebuffers)(int, const unsigned int *) = nullptr;
		void(APIENTRY *BindFramebuffer)(unsigned int, unsigned int) = nullptr;
		void(APIENTRY *FramebufferTexture2D)(unsigned int, unsigned int, unsigned int, unsigned int, int) = nullptr;
		unsigned int(APIENTRY *CheckFramebufferStatus)(unsigned int) = nullptr;

		bool load()
		{
			const HMODULE module = GetModuleHandleW(L"opengl32.dll");
			if (module == nullptr)
				return false;
			const auto get_extension = reinterpret_cast<PROC(WINAPI *)(LPCSTR)>(GetProcAddress(module, "wglGetProcAddress"));
			if (get_extension == nullptr)
				return false;
			const auto core = [module](auto &function, const char *name) {
				function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(GetProcAddress(module, name));
				return function != nullptr;
			};
			const auto extension = [get_extension](auto &function, const char *name) {
				function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(get_extension(name));
				return function != nullptr;
			};
			return core(GetCurrentContext, "wglGetCurrentContext") && core(GetIntegerv, "glGetIntegerv") &&
				core(GetFloatv, "glGetFloatv") && core(GetBooleanv, "glGetBooleanv") && core(IsEnabled, "glIsEnabled") &&
				core(Enable, "glEnable") && core(Disable, "glDisable") && core(ColorMask, "glColorMask") &&
				core(ClearColor, "glClearColor") && core(Clear, "glClear") &&
				extension(GenFramebuffers, "glGenFramebuffers") && extension(DeleteFramebuffers, "glDeleteFramebuffers") &&
				extension(BindFramebuffer, "glBindFramebuffer") && extension(FramebufferTexture2D, "glFramebufferTexture2D") &&
				extension(CheckFramebufferStatus, "glCheckFramebufferStatus");
		}
	};

	// Layout grid: every gap and size is a multiple of 4, mostly of 8, so all pages share one rhythm.
	constexpr float outer_padding = 24.0f;
	constexpr float space_xs = 4.0f;
	constexpr float space_sm = 8.0f;
	constexpr float space_md = 16.0f;
	constexpr float control_height = 32.0f;
	constexpr float header_row_step = control_height + space_sm;
	constexpr float list_row_height = 40.0f;
	constexpr float list_row_gap = 6.0f;
	constexpr float note_height = 20.0f;
	constexpr float scrollbar_width = 32.0f;
	constexpr float main_footer_height = control_height * 3.0f + space_sm * 3.0f + note_height;
	constexpr float detail_footer_height = control_height;
	constexpr float row_spacing = 3.0f;
	constexpr uint32_t drag_release_frames = 3;
	constexpr float joystick_scroll_dead_zone = 0.5f;
	constexpr uint32_t joystick_scroll_repeat_frames = 8;
	constexpr uint32_t save_feedback_frames = 180;
	constexpr uint32_t no_theme_applied = ~0u;

	const vr_theme::palette &colors()
	{
		return vr_theme::current();
	}

	// The panel swapchain is usually sRGB, so the GPU converts every written color from linear light to sRGB, which lifts
	// dark colors (a #1E1E2E background would show as a pale grey). Converting the vertex colors to linear first makes the
	// written result match the theme exactly. Runs on the CPU over the frame's few thousand vertices and never waits on the GPU.
	void convert_draw_data_to_linear(ImDrawData *draw_data)
	{
		static const auto table = [] {
			std::array<uint8_t, 256> values = {};
			for (size_t index = 0; index < values.size(); ++index)
			{
				const double encoded = static_cast<double>(index) / 255.0;
				const double linear = encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
				values[index] = static_cast<uint8_t>(std::lround(linear * 255.0));
			}
			return values;
		}();
		if (draw_data == nullptr)
			return;
		for (ImDrawList *const draw_list : draw_data->CmdLists)
			for (ImDrawVert &vertex : draw_list->VtxBuffer)
			{
				const ImU32 color = vertex.col;
				vertex.col = (color & IM_COL32_A_MASK) |
					(static_cast<ImU32>(table[(color >> IM_COL32_R_SHIFT) & 0xFF]) << IM_COL32_R_SHIFT) |
					(static_cast<ImU32>(table[(color >> IM_COL32_G_SHIFT) & 0xFF]) << IM_COL32_G_SHIFT) |
					(static_cast<ImU32>(table[(color >> IM_COL32_B_SHIFT) & 0xFF]) << IM_COL32_B_SHIFT);
			}
	}

	// How a button is filled. `toggled` marks an option that is switched on; `primary` is the one main action on a screen.
	enum class button_tone
	{
		neutral,
		toggled,
		primary,
		warning,
		success,
		danger,
		destructive,
	};

	enum class button_icon
	{
		close,
		previous,
		next,
		up,
		down,
		expand,
		collapse,
		pin,
	};

	ImFont *bold_font()
	{
		const ImFontAtlas *const atlas = ImGui::GetIO().Fonts;
		return atlas->Fonts.Size > 1 ? atlas->Fonts[1] : nullptr;
	}

	constexpr float statistics_font_scale = 1.25f;

	// ImGui 1.92 draws glyphs on first use. When its font image then runs out of room it drops sizes unused for two frames
	// and repacks into a new texture, which in D3D12 can overwrite glyphs still in flight (missing pixel rows) and makes
	// the backend wait for the GPU mid-game. Drawing every printable glyph in every size the panels use before the first
	// frame means the image is uploaded once, at its final size, and never changes while the game runs.
	void prebake_fonts()
	{
		ImFontAtlas *const atlas = ImGui::GetIO().Fonts;
		if (atlas->Fonts.Size < 2)
			return;
		const struct
		{
			int font;
			float size;
		} uses[] = {
			{ 0, vr_theme::font_body },
			{ 0, vr_theme::font_control },
			{ 0, vr_theme::font_small },
			{ 0, vr_theme::font_body * statistics_font_scale },
			{ 1, vr_theme::font_body },
			{ 1, vr_theme::font_small },
			{ 1, vr_theme::font_title },
		};
		for (const auto &use : uses)
		{
			ImFontBaked *const baked = atlas->Fonts[use.font]->GetFontBaked(use.size, 1.0f);
			if (baked == nullptr)
				continue;
			for (ImWchar character = 0x20; character <= 0x7E; ++character)
				baked->FindGlyph(character);
			baked->FindGlyph(0xB7);
		}
	}

	void apply_style()
	{
		ImGuiStyle &style = ImGui::GetStyle();
		style.FontSizeBase = vr_theme::font_body;
		style.WindowPadding = { outer_padding, outer_padding };
		style.WindowRounding = vr_theme::radius_panel;
		style.WindowBorderSize = vr_theme::stroke_thin;
		style.PopupRounding = vr_theme::radius_md;
		style.PopupBorderSize = vr_theme::stroke_thin;
		style.FrameRounding = vr_theme::radius_sm;
		style.FrameBorderSize = 0.0f;
		style.ItemSpacing = { 8.0f, row_spacing };
		style.Colors[ImGuiCol_WindowBg] = vr_theme::to_float4(colors().background);
		style.Colors[ImGuiCol_PopupBg] = vr_theme::to_float4(colors().surface_raised);
		style.Colors[ImGuiCol_Border] = vr_theme::to_float4(colors().border);
		style.Colors[ImGuiCol_Separator] = vr_theme::to_float4(colors().border);
		style.Colors[ImGuiCol_Text] = vr_theme::to_float4(colors().text);
		style.Colors[ImGuiCol_TextDisabled] = vr_theme::to_float4(colors().text_secondary);
	}

	void draw_crosshair(ImDrawList *draw_list, const ImVec2 position)
	{
		const ImU32 outline = colors().shadow | IM_COL32_A_MASK;
		draw_list->AddLine({ position.x - 11.0f, position.y }, { position.x + 11.0f, position.y }, outline, 7.0f);
		draw_list->AddLine({ position.x, position.y - 11.0f }, { position.x, position.y + 11.0f }, outline, 7.0f);
		draw_list->AddLine({ position.x - 9.0f, position.y }, { position.x + 9.0f, position.y }, colors().text, 3.0f);
		draw_list->AddLine({ position.x, position.y - 9.0f }, { position.x, position.y + 9.0f }, colors().text, 3.0f);
		draw_list->AddCircleFilled(position, 3.0f, colors().accent);
	}

	struct button_colors
	{
		ImU32 fill;
		ImU32 text;
		ImU32 outline;
	};

	button_colors button_colors_for(button_tone tone, bool hovered, bool enabled)
	{
		// Status tones stay visible while a button is busy and therefore disabled, for example "Creating...".
		const bool status = tone == button_tone::warning || tone == button_tone::success || tone == button_tone::danger;
		if (!enabled && !status)
		{
			// Keeps the normal fill so a disabled button does not change background; only the text/icon is greyed.
			button_colors look = button_colors_for(tone, false, true);
			look.text = colors().text_disabled;
			look.outline = 0;
			return look;
		}
		hovered = hovered && enabled;
		switch (tone)
		{
		case button_tone::toggled:
			return { hovered ? colors().surface_hover : colors().accent_muted, colors().text, colors().accent };
		case button_tone::primary:
			return { hovered ? colors().accent_hover : colors().accent, colors().text_on_color, 0 };
		case button_tone::warning:
			return { colors().warning, colors().text_on_color, hovered ? colors().text : 0 };
		case button_tone::success:
			return { colors().success, colors().text_on_color, hovered ? colors().text : 0 };
		case button_tone::danger:
			return { hovered ? colors().danger_hover : colors().danger, colors().text_on_color, 0 };
		case button_tone::destructive:
			return { hovered ? colors().danger : colors().surface_raised, hovered ? colors().text_on_color : colors().danger, 0 };
		default:
			return { hovered ? colors().surface_hover : colors().surface_raised, colors().text, 0 };
		}
	}

	void draw_button_frame(ImDrawList *draw_list, ImVec2 minimum, ImVec2 maximum, const button_colors &look)
	{
		draw_list->AddRectFilled(minimum, maximum, look.fill, vr_theme::radius_sm);
		if (look.outline != 0)
			draw_list->AddRect(minimum, maximum, look.outline, vr_theme::radius_sm, 0, vr_theme::stroke_thin);
	}

	// Controls drawn by the add-on's own panel can be recorded for the preview tool and tests. The pointer is
	// non-null only while recording is on, so the game pays a single pointer check per control and nothing else.
	// A recorded id is the id the helper gives ImGui (never the visible label), with `prefix` in front.
	// Rows and drop-down items are named by position, not content: effect/<row>/..., param/<row>/..., <dropdown>/item/<index>.
	struct control_recorder
	{
		std::vector<openxr_ui::panel_renderer::control_rect> controls;
		std::string prefix;
	};
	control_recorder *s_recorder = nullptr;

	// Points s_recorder at nothing again on every return path of render_ui, so it never outlives its renderer.
	struct recorder_scope
	{
		~recorder_scope() { s_recorder = nullptr; }
	};

	void record_control(std::string_view id, ImVec2 minimum, ImVec2 maximum)
	{
		if (s_recorder == nullptr)
			return;
		s_recorder->controls.push_back({ s_recorder->prefix + std::string(id), minimum.x, minimum.y, maximum.x, maximum.y });
	}

	// Records the item just submitted.
	void record_last_item(const char *id)
	{
		if (s_recorder == nullptr)
			return;
		record_control(id, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	}

	void set_control_prefix(const char *prefix)
	{
		if (s_recorder != nullptr)
			s_recorder->prefix = prefix;
	}

	void set_control_row_prefix(const char *kind, size_t row)
	{
		if (s_recorder != nullptr)
			s_recorder->prefix = std::string(kind) + "/" + std::to_string(row) + "/";
	}

	void clear_control_prefix()
	{
		if (s_recorder != nullptr)
			s_recorder->prefix.clear();
	}

	bool draw_text_button(const char *id, const char *label, const ImVec2 size, bool enabled = true, button_tone tone = button_tone::neutral)
	{
		ImGui::BeginDisabled(!enabled);
		ImGui::InvisibleButton(id, size);
		record_last_item(id);
		const bool hovered = enabled && ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const button_colors look = button_colors_for(tone, hovered, enabled);
		draw_button_frame(draw_list, minimum, maximum, look);
		ImGui::PushFont(nullptr, vr_theme::font_control);
		const ImVec2 text_size = ImGui::CalcTextSize(label);
		draw_list->AddText({ std::floor(minimum.x + (size.x - text_size.x) * 0.5f), std::floor(minimum.y + (size.y - text_size.y) * 0.5f) },
			look.text, label);
		ImGui::PopFont();
		ImGui::EndDisabled();
		return hovered;
	}

	// Simple line icons instead of "<", "^" or "X" characters, which read poorly at VR resolution.
	bool draw_icon_button(const char *id, button_icon icon, const ImVec2 size, bool enabled = true, bool toggled = false)
	{
		ImGui::BeginDisabled(!enabled);
		ImGui::InvisibleButton(id, size);
		record_last_item(id);
		const bool hovered = enabled && ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const button_colors look = button_colors_for(toggled ? button_tone::toggled : button_tone::neutral, hovered, enabled);
		draw_button_frame(draw_list, minimum, maximum, look);

		const ImVec2 center = { (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f };
		const float half = std::clamp((std::min)(size.x, size.y) * 0.18f, 3.5f, 7.0f);
		const float thickness = 2.5f;
		switch (icon)
		{
		case button_icon::close:
			draw_list->AddLine({ center.x - half, center.y - half }, { center.x + half, center.y + half }, look.text, thickness);
			draw_list->AddLine({ center.x - half, center.y + half }, { center.x + half, center.y - half }, look.text, thickness);
			break;
		case button_icon::previous:
		case button_icon::next:
		{
			const float direction = icon == button_icon::next ? 1.0f : -1.0f;
			const ImVec2 points[3] = { { center.x - half * 0.5f * direction, center.y - half },
				{ center.x + half * 0.5f * direction, center.y }, { center.x - half * 0.5f * direction, center.y + half } };
			draw_list->AddPolyline(points, 3, look.text, ImDrawFlags_None, thickness);
			break;
		}
		case button_icon::pin:
		{
			// A round head with a short needle below it.
			const ImVec2 head = { center.x, center.y - half * 0.45f };
			draw_list->AddCircle(head, half * 0.65f, look.text, 0, thickness);
			draw_list->AddLine({ head.x, head.y + half * 0.65f }, { head.x, center.y + half * 1.3f }, look.text, thickness);
			break;
		}
		case button_icon::expand:
		case button_icon::collapse:
		{
			// Four corner brackets: pointing out to grow the panel, pointing in to shrink it.
			const float outer = half * 1.25f;
			const float arm = half * 0.75f;
			const bool outward = icon == button_icon::expand;
			for (const float sx : { -1.0f, 1.0f })
				for (const float sy : { -1.0f, 1.0f })
				{
					const float corner = outward ? outer : half * 0.55f;
					const float reach = outward ? -arm : outer - corner;
					const ImVec2 points[3] = { { center.x + sx * (corner + reach), center.y + sy * corner },
						{ center.x + sx * corner, center.y + sy * corner }, { center.x + sx * corner, center.y + sy * (corner + reach) } };
					draw_list->AddPolyline(points, 3, look.text, ImDrawFlags_None, thickness);
				}
			break;
		}
		default:
		{
			const float direction = icon == button_icon::down ? 1.0f : -1.0f;
			const ImVec2 points[3] = { { center.x - half, center.y - half * 0.5f * direction },
				{ center.x, center.y + half * 0.5f * direction }, { center.x + half, center.y - half * 0.5f * direction } };
			draw_list->AddPolyline(points, 3, look.text, ImDrawFlags_None, thickness);
			break;
		}
		}
		ImGui::EndDisabled();
		return hovered;
	}

	// Returns hover even when disabled so the tooltip can explain why nothing will happen.
	bool draw_reset_button(const char *id, const ImVec2 size, bool enabled)
	{
		ImGui::InvisibleButton(id, size);
		record_last_item(id);
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const button_colors look = button_colors_for(button_tone::neutral, enabled && hovered, enabled);
		draw_button_frame(draw_list, minimum, maximum, look);

		const ImVec2 center = { (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f };
		const float radius = std::clamp((std::min)(size.x, size.y) * 0.5f - 6.0f, 4.0f, 9.0f);
		const ImU32 color = look.text;
		draw_list->PathArcTo(center, radius, -1.5707963268f, 3.8f, 24);
		draw_list->PathStroke(color, ImDrawFlags_None, 2.5f);
		const float head = radius * 0.6f;
		draw_list->AddTriangleFilled({ center.x - head, center.y - radius }, { center.x + head * 0.4f, center.y - radius - head },
			{ center.x + head * 0.4f, center.y - radius + head }, color);
		return hovered;
	}

	bool draw_star_button(const char *id, const ImVec2 size, bool active)
	{
		ImGui::InvisibleButton(id, size);
		record_last_item(id);
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		draw_button_frame(draw_list, minimum, maximum, button_colors_for(button_tone::neutral, hovered, true));

		const ImVec2 center = { (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f };
		ImVec2 points[10];
		for (size_t index = 0; index < 10; ++index)
		{
			const float angle = -1.5707963268f + static_cast<float>(index) * 0.6283185307f;
			const float radius = index % 2 == 0 ? 10.0f : 4.5f;
			points[index] = { center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius };
		}
		const ImU32 color = active ? colors().warning : colors().text_secondary;
		if (active)
			for (size_t index = 0; index < 10; ++index)
				draw_list->AddTriangleFilled(center, points[index], points[(index + 1) % 10], color);
		else
			draw_list->AddPolyline(points, 10, color, ImDrawFlags_Closed, 2.0f);
		return hovered;
	}

	constexpr float switch_track_width = 44.0f;
	constexpr float switch_track_height = 24.0f;

	// Pill-shaped on/off switch with a sliding knob drawn at its on or off end.
	[[maybe_unused]] void draw_switch_indicator(ImDrawList *draw_list, ImVec2 min, bool on, bool hovered)
	{
		const ImU32 track = on ? (hovered ? colors().accent_hover : colors().accent) : colors().control_idle;
		draw_list->AddRectFilled(min, { min.x + switch_track_width, min.y + switch_track_height }, track, switch_track_height * 0.5f);
		const float knob_x = on ? min.x + switch_track_width - 3.0f - 9.0f : min.x + 3.0f + 9.0f;
		draw_list->AddCircleFilled({ knob_x, min.y + switch_track_height * 0.5f }, 9.0f, on ? colors().text_on_color : colors().text);
	}

	// Hit area of `size` with the switch vertically centered at its left edge.
	[[maybe_unused]] bool draw_switch(const char *id, bool on, ImVec2 size)
	{
		ImGui::InvisibleButton(id, size);
		record_last_item(id);
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		draw_switch_indicator(ImGui::GetWindowDrawList(), { minimum.x, std::floor(minimum.y + (size.y - switch_track_height) * 0.5f) }, on, hovered);
		return hovered;
	}

	constexpr float switch_pill_height = 40.0f;

	[[maybe_unused]] float switch_pill_width(const char *label)
	{
		ImGui::PushFont(nullptr, vr_theme::font_control);
		const float text_width = ImGui::CalcTextSize(label).x;
		ImGui::PopFont();
		return std::ceil(4.0f + switch_track_width + 8.0f + text_width + 12.0f);
	}

	// Inset pill holding a switch and its label; the whole pill is the hit area.
	[[maybe_unused]] bool draw_switch_pill(const char *id, const char *label, bool on)
	{
		ImGui::InvisibleButton(id, { switch_pill_width(label), switch_pill_height });
		record_last_item(id);
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		draw_list->AddRectFilled(minimum, maximum, colors().inset, switch_pill_height * 0.5f);
		draw_switch_indicator(draw_list, { minimum.x + 4.0f, minimum.y + (switch_pill_height - switch_track_height) * 0.5f }, on, hovered);
		ImGui::PushFont(nullptr, vr_theme::font_control);
		draw_list->AddText({ minimum.x + 4.0f + switch_track_width + 8.0f, std::floor(minimum.y + (switch_pill_height - ImGui::GetFontSize()) * 0.5f) },
			colors().text, label);
		ImGui::PopFont();
		return hovered;
	}

	void push_segment_font(bool large)
	{
		if (large)
			ImGui::PushFont(bold_font(), vr_theme::font_body);
		else
			ImGui::PushFont(nullptr, vr_theme::font_control);
	}

	constexpr float segment_padding = 16.0f;
	constexpr float segment_count_gap = 8.0f;

	float segment_width_at(const char *const *labels, const char *const *counts, size_t index)
	{
		float width = ImGui::CalcTextSize(labels[index]).x;
		if (counts != nullptr && counts[index] != nullptr)
			width += segment_count_gap + ImGui::CalcTextSize(counts[index]).x;
		return std::ceil(width + segment_padding * 2.0f);
	}

	[[maybe_unused]] float segmented_width(const char *const *labels, const char *const *counts, size_t count, bool large)
	{
		push_segment_font(large);
		float width = 8.0f;
		for (size_t index = 0; index < count; ++index)
			width += segment_width_at(labels, counts, index);
		ImGui::PopFont();
		return width;
	}

	// Inset strip of segments (tabs or a filter). Returns the hovered segment, or -1; the caller reacts to clicks.
	[[maybe_unused]] int draw_segmented(const char *id, const char *const *labels, const char *const *counts, size_t count, size_t selected, bool large)
	{
		constexpr float strip_height = 40.0f;
		constexpr float segment_height = 32.0f;
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float total_width = segmented_width(labels, counts, count, large);
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		draw_list->AddRectFilled(start, { start.x + total_width, start.y + strip_height }, colors().inset, vr_theme::radius_md);

		int hovered_index = -1;
		push_segment_font(large);
		ImGui::PushID(id);
		float x = start.x + 4.0f;
		for (size_t index = 0; index < count; ++index)
		{
			const float width = segment_width_at(labels, counts, index);
			ImGui::PushID(static_cast<int>(index));
			ImGui::SetCursorScreenPos({ x, start.y + 4.0f });
			ImGui::InvisibleButton("segment", { width, segment_height });
			if (s_recorder != nullptr)
				record_control(std::string(id) + "/" + std::to_string(index) + "/segment", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			const bool hovered = ImGui::IsItemHovered();
			ImGui::PopID();
			if (hovered)
				hovered_index = static_cast<int>(index);
			const bool is_selected = index == selected;
			if (is_selected || hovered)
				draw_list->AddRectFilled({ x, start.y + 4.0f }, { x + width, start.y + 4.0f + segment_height },
					is_selected ? colors().surface_raised : colors().surface_hover, vr_theme::radius_sm);
			const float text_y = std::floor(start.y + 4.0f + (segment_height - ImGui::GetFontSize()) * 0.5f);
			float text_x = x + segment_padding;
			draw_list->AddText({ text_x, text_y }, is_selected ? colors().text : colors().text_secondary, labels[index]);
			if (counts != nullptr && counts[index] != nullptr)
			{
				text_x += ImGui::CalcTextSize(labels[index]).x + segment_count_gap;
				draw_list->AddText({ text_x, text_y }, colors().text_disabled, counts[index]);
			}
			x += width;
		}
		ImGui::PopID();
		ImGui::PopFont();
		ImGui::SetCursorScreenPos(start);
		ImGui::Dummy({ total_width, strip_height });
		return hovered_index;
	}

	[[maybe_unused]] void draw_group_title(const char *text, ImVec2 pos)
	{
		std::string upper = text;
		for (char &character : upper)
			if (character >= 'a' && character <= 'z')
				character = static_cast<char>(character - 'a' + 'A');
		ImGui::PushFont(bold_font(), vr_theme::font_small);
		ImGui::GetWindowDrawList()->AddText(pos, colors().text_secondary, upper.c_str());
		ImGui::PopFont();
	}

	[[maybe_unused]] void draw_card(ImVec2 min, ImVec2 max)
	{
		ImGui::GetWindowDrawList()->AddRectFilled(min, max, colors().surface, vr_theme::radius_md);
	}

	// Rows are divided by a 2 px band in the background color rather than a thin line.
	[[maybe_unused]] void draw_row_separator(float x0, float x1, float y)
	{
		ImGui::GetWindowDrawList()->AddRectFilled({ x0, y }, { x1, y + 2.0f }, colors().background);
	}

	[[maybe_unused]] void draw_settings_row_label(ImVec2 row_min, float row_height, const char *label)
	{
		ImGui::GetWindowDrawList()->AddText({ row_min.x, std::floor(row_min.y + (row_height - ImGui::GetFontSize()) * 0.5f) }, colors().text, label);
	}

	constexpr float settings_row_height = 44.0f;

	// Settings row whose whole width is the hit area of a switch at its right end. `note` is small warning text left of the switch.
	[[maybe_unused]] bool draw_switch_row(const char *id, const char *label, bool on, ImVec2 row_min, float width, const char *note = nullptr)
	{
		ImGui::SetCursorScreenPos(row_min);
		ImGui::InvisibleButton(id, { width, settings_row_height });
		record_last_item(id);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		draw_settings_row_label(row_min, settings_row_height, label);
		const float switch_x = row_min.x + width - switch_track_width;
		draw_switch_indicator(draw_list, { switch_x, std::floor(row_min.y + (settings_row_height - switch_track_height) * 0.5f) }, on, hovered);
		if (note != nullptr)
		{
			ImGui::PushFont(nullptr, vr_theme::font_small);
			draw_list->AddText({ switch_x - space_md - std::ceil(ImGui::CalcTextSize(note).x),
				std::floor(row_min.y + (settings_row_height - ImGui::GetFontSize()) * 0.5f) }, colors().warning, note);
			ImGui::PopFont();
		}
		return hovered;
	}

	// The font has no U+25CF glyph, so the dot is a drawn circle.
	[[maybe_unused]] void draw_status_dot_text(ImDrawList *draw_list, ImVec2 pos, ImU32 color, const char *text)
	{
		ImGui::PushFont(nullptr, vr_theme::font_small);
		draw_list->AddCircleFilled({ pos.x + 4.0f, pos.y + ImGui::GetFontSize() * 0.5f }, 4.0f, color);
		draw_list->AddText({ pos.x + 16.0f, pos.y }, color, text);
		ImGui::PopFont();
	}

	std::string fit_text(const char *text, float maximum_width)
	{
		if (ImGui::CalcTextSize(text).x <= maximum_width)
			return text;

		std::string fitted = text;
		while (!fitted.empty() && ImGui::CalcTextSize((fitted + "...").c_str()).x > maximum_width)
		{
			fitted.pop_back();
			while (!fitted.empty() && (static_cast<unsigned char>(fitted.back()) & 0xC0) == 0x80)
				fitted.pop_back();
		}
		return fitted + "...";
	}

	// Bottom line of the effect page: `left_text`, then "<graphics> 0xB7 <version>" on the right. The version is always
	// complete; only the resolution part of the graphics label gives way when room runs out.
	void draw_api_version_line(ImDrawList *draw_list, const char *left_text, const char *graphics_api_label, float display_height, float right_edge)
	{
		char version_label[16];
		openxr_ui::format_version_label(version_label);
		char version_suffix[32];
		std::snprintf(version_suffix, sizeof(version_suffix), " \xC2\xB7 %s", version_label);
		ImGui::PushFont(nullptr, vr_theme::font_small);
		const float bottom_y = display_height - outer_padding - note_height + std::floor((note_height - ImGui::GetFontSize()) * 0.5f);
		const float left_width = std::ceil(ImGui::CalcTextSize(left_text).x);
		const float suffix_width = std::ceil(ImGui::CalcTextSize(version_suffix).x);
		const std::string fitted_api = fit_text(graphics_api_label, (std::max)(0.0f, right_edge - outer_padding - left_width - space_md - suffix_width));
		const std::string right_text = fitted_api + version_suffix;
		const float right_x = right_edge - std::ceil(ImGui::CalcTextSize(right_text.c_str()).x);
		draw_list->AddText({ outer_padding, bottom_y }, colors().text_secondary, left_text);
		draw_list->AddText({ right_x, bottom_y }, colors().text_secondary, right_text.c_str());
		ImGui::PopFont();
	}

	void draw_hover_details(const ImVec2 minimum, const ImVec2 maximum, const char *label, const char *tooltip = nullptr)
	{
		if (!ImGui::IsMouseHoveringRect(minimum, maximum) || ((tooltip == nullptr || *tooltip == '\0') && ImGui::CalcTextSize(label).x <= maximum.x - minimum.x))
			return;

		ImGui::SetNextWindowSizeConstraints({ 280.0f, 0.0f }, { (std::min)(700.0f, ImGui::GetIO().DisplaySize.x - outer_padding * 2.0f), ImGui::GetIO().DisplaySize.y - outer_padding * 2.0f });
		if (ImGui::BeginTooltip())
		{
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (std::min)(640.0f, ImGui::GetIO().DisplaySize.x - outer_padding * 4.0f));
			ImGui::TextUnformatted(label);
			if (tooltip != nullptr && *tooltip != '\0')
			{
				ImGui::Separator();
				ImGui::TextUnformatted(tooltip);
			}
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	// Tooltip for the last item; works for disabled items too because it tests the pointer against the rectangle.
	[[maybe_unused]] void draw_hover_help(const char *label, const char *help)
	{
		draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), label, help);
	}

	// Small chevron drawn before a text button's label: pointing left (previous) or up.
	void draw_label_chevron(ImVec2 center, bool up, ImU32 color)
	{
		const float half = 5.0f;
		const ImVec2 left_points[3] = { { center.x + half * 0.5f, center.y - half }, { center.x - half * 0.5f, center.y },
			{ center.x + half * 0.5f, center.y + half } };
		const ImVec2 up_points[3] = { { center.x - half, center.y + half * 0.5f }, { center.x, center.y - half * 0.5f },
			{ center.x + half, center.y + half * 0.5f } };
		ImGui::GetWindowDrawList()->AddPolyline(up ? up_points : left_points, 3, color, ImDrawFlags_None, 2.5f);
	}

	// Width of a text button that has a chevron before its label.
	float chevron_button_width(const char *label)
	{
		ImGui::PushFont(nullptr, vr_theme::font_control);
		const float width = std::ceil(ImGui::CalcTextSize(label).x) + 48.0f;
		ImGui::PopFont();
		return width;
	}

	// Width of a text button with a 12 px chevron and an 8 px gap next to its label, 16 px padding each side.
	float chevron_label_button_width(const char *label)
	{
		ImGui::PushFont(nullptr, vr_theme::font_control);
		const float width = std::ceil(ImGui::CalcTextSize(label).x) + 12.0f + space_sm + 32.0f;
		ImGui::PopFont();
		return width;
	}

	// Text button whose chevron and label are centered together: an up chevron before the label, or a next chevron after it.
	bool draw_chevron_label_button(const char *id, const char *label, const ImVec2 size, bool enabled, bool up_before_label)
	{
		ImGui::BeginDisabled(!enabled);
		ImGui::InvisibleButton(id, size);
		record_last_item(id);
		const bool hovered = enabled && ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const button_colors look = button_colors_for(button_tone::neutral, hovered, enabled);
		draw_button_frame(draw_list, minimum, ImGui::GetItemRectMax(), look);
		ImGui::PushFont(nullptr, vr_theme::font_control);
		const ImVec2 text_size = ImGui::CalcTextSize(label);
		const float text_width = std::ceil(text_size.x);
		const float group_x = std::floor(minimum.x + (size.x - (12.0f + space_sm + text_width)) * 0.5f);
		const float center_y = minimum.y + size.y * 0.5f;
		const float text_x = up_before_label ? group_x + 12.0f + space_sm : group_x;
		draw_list->AddText({ text_x, std::floor(minimum.y + (size.y - text_size.y) * 0.5f) }, look.text, label);
		ImGui::PopFont();
		if (up_before_label)
			draw_label_chevron({ group_x + 6.0f, center_y }, true, look.text);
		else
		{
			const float cx = group_x + text_width + space_sm + 6.0f;
			const ImVec2 points[3] = { { cx - 2.5f, center_y - 5.0f }, { cx + 2.5f, center_y }, { cx - 2.5f, center_y + 5.0f } };
			draw_list->AddPolyline(points, 3, look.text, ImDrawFlags_None, 2.5f);
		}
		ImGui::EndDisabled();
		return hovered;
	}

	// Status strip above the effect list. `text_max_x` leaves room for buttons on the right.
	void draw_notice(const ImVec2 notice_min, const ImVec2 notice_max, float text_max_x, ImU32 accent, bool busy,
		const char *message, const char *details)
	{
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		draw_list->AddRectFilled(notice_min, notice_max, colors().surface_raised, vr_theme::radius_md);
		draw_list->AddRectFilled(notice_min, { notice_min.x + 6.0f, notice_max.y }, accent, vr_theme::radius_md, ImDrawFlags_RoundCornersLeft);
		if (busy)
		{
			// A segment sliding along the bottom edge shows that loading is still in progress.
			const float track_min = notice_min.x + 5.0f;
			const float track_width = notice_max.x - track_min;
			const float segment_width = track_width * 0.2f;
			const float phase = static_cast<float>(std::fmod(ImGui::GetTime() * 0.5, 1.0));
			const float segment_min = track_min + (track_width + segment_width) * phase - segment_width;
			draw_list->AddRectFilled({ (std::max)(segment_min, track_min), notice_max.y - 4.0f },
				{ (std::min)(segment_min + segment_width, notice_max.x), notice_max.y }, accent);
		}

		const ImVec2 text_min = { notice_min.x + 16.0f, notice_min.y };
		const ImVec2 text_max = { text_max_x, notice_max.y };
		const std::string fitted = fit_text(message, text_max.x - text_min.x);
		draw_list->AddText({ text_min.x, std::floor(notice_min.y + (notice_max.y - notice_min.y - ImGui::GetFontSize()) * 0.5f) },
			colors().text, fitted.c_str());
		draw_hover_details(text_min, text_max, message, details);
	}

	struct slider_interaction
	{
		bool hovered = false;
		float fraction = 0.0f;
	};

	slider_interaction draw_slider(const char *id, const ImVec2 size, float fraction, bool enabled = true, float default_fraction = -1.0f)
	{
		fraction = std::clamp(fraction, 0.0f, 1.0f);
		ImGui::BeginDisabled(!enabled);
		ImGui::InvisibleButton(id, size);
		record_last_item(id);
		const bool hovered = enabled && ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const float center_y = (minimum.y + maximum.y) * 0.5f;
		const float track_min_x = minimum.x + 8.0f;
		const float track_max_x = maximum.x - 8.0f;
		const float knob_x = track_min_x + (track_max_x - track_min_x) * fraction;
		// A value without a range (or an On/Off value) has nothing to slide, so no empty track is drawn.
		if (enabled)
		{
			draw_list->AddRectFilled({ track_min_x, center_y - 3.0f }, { track_max_x, center_y + 3.0f }, colors().surface_hover, 3.0f);
			draw_list->AddRectFilled({ track_min_x, center_y - 3.0f }, { knob_x, center_y + 3.0f }, colors().accent, 3.0f);
		}
		if (enabled && default_fraction >= 0.0f && default_fraction <= 1.0f)
		{
			const float default_x = track_min_x + (track_max_x - track_min_x) * default_fraction;
			draw_list->AddLine({ default_x, center_y - 9.0f }, { default_x, center_y + 9.0f }, colors().text_secondary, vr_theme::stroke);
		}
		if (enabled)
		{
			draw_list->AddCircleFilled({ knob_x, center_y }, 9.0f, hovered ? colors().accent_hover : colors().accent);
			draw_list->AddCircleFilled({ knob_x, center_y }, 4.0f, colors().text);
		}
		ImGui::EndDisabled();
		return { hovered, std::clamp((ImGui::GetIO().MousePos.x - track_min_x) / (track_max_x - track_min_x), 0.0f, 1.0f) };
	}

	slider_interaction draw_vertical_slider(const char *id, const ImVec2 size, float fraction, bool enabled = true)
	{
		fraction = std::clamp(fraction, 0.0f, 1.0f);
		ImGui::BeginDisabled(!enabled);
		ImGui::InvisibleButton(id, size);
		record_last_item(id);
		const bool hovered = enabled && ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const float center_x = (minimum.x + maximum.x) * 0.5f;
		const float track_min_y = minimum.y + 8.0f;
		const float track_max_y = maximum.y - 8.0f;
		// The scroll bar appears only when the list is longer than the panel.
		if (enabled)
		{
			draw_list->AddRectFilled({ center_x - 4.0f, track_min_y }, { center_x + 4.0f, track_max_y }, colors().surface_raised, 4.0f);
			// A pill-shaped thumb reads as a scroll bar rather than a value slider.
			const float thumb_y = track_min_y + (track_max_y - track_min_y) * fraction;
			const float thumb_half = (std::min)(18.0f, (track_max_y - track_min_y) * 0.5f);
			const float thumb_center = std::clamp(thumb_y, track_min_y + thumb_half, track_max_y - thumb_half);
			draw_list->AddRectFilled({ center_x - 5.0f, thumb_center - thumb_half }, { center_x + 5.0f, thumb_center + thumb_half },
				hovered ? colors().accent_hover : colors().control_idle, 5.0f);
		}
		ImGui::EndDisabled();
		return { hovered, std::clamp((ImGui::GetIO().MousePos.y - track_min_y) / (track_max_y - track_min_y), 0.0f, 1.0f) };
	}

	// Only one drop-down list is open at a time. While it is open the pointer is hidden from the rest of the
	// panel, so the next click can only pick a choice or close the list.
	struct dropdown_state
	{
		ImGuiID open_id = 0;
		const char *open_name = nullptr;
		ImVec2 box_min;
		ImVec2 box_max;
		std::vector<std::string> options;
		std::vector<bool> disabled;
		size_t selected = 0;
		bool box_drawn = false;
		ImGuiID chosen_id = 0;
		size_t chosen_option = 0;
	};

	struct dropdown_interaction
	{
		bool hovered = false;
		bool chosen = false;
		size_t option = 0;
	};

	// Opens below the box, or above it when the panel has no room below.
	ImVec2 dropdown_list_min(const dropdown_state &state, float display_height)
	{
		const float list_height = (state.box_max.y - state.box_min.y) * static_cast<float>(state.options.size());
		const float below = state.box_max.y + 2.0f;
		return { state.box_min.x, below + list_height <= display_height ? below : state.box_min.y - 2.0f - list_height };
	}

	ptrdiff_t dropdown_option_at(const dropdown_state &state, ImVec2 pointer, float display_height)
	{
		const ImVec2 list_min = dropdown_list_min(state, display_height);
		const float row_height = state.box_max.y - state.box_min.y;
		if (row_height <= 0.0f || pointer.x < list_min.x || pointer.x >= state.box_max.x || pointer.y < list_min.y)
			return -1;
		const size_t row = static_cast<size_t>((pointer.y - list_min.y) / row_height);
		return row < state.options.size() ? static_cast<ptrdiff_t>(row) : -1;
	}

	// Call before ImGui::NewFrame. Returns true when the pointer must be hidden from the other controls this frame.
	bool update_dropdown(dropdown_state &state, bool pointer_visible, ImVec2 pointer, bool clicked, float display_height)
	{
		state.chosen_id = 0;
		if (state.open_id == 0)
			return false;
		if (clicked)
		{
			const ptrdiff_t option = pointer_visible ? dropdown_option_at(state, pointer, display_height) : -1;
			if (option >= 0 && !(static_cast<size_t>(option) < state.disabled.size() && state.disabled[static_cast<size_t>(option)]))
			{
				state.chosen_id = state.open_id;
				state.chosen_option = static_cast<size_t>(option);
			}
			state.open_id = 0;
		}
		return true;
	}

	// Width draw_dropdown gives a box: sized to its longest choice so the text is never cut short.
	[[maybe_unused]] float dropdown_width(const char *prefix, const char *const *options, size_t count, float height)
	{
		ImGui::PushFont(nullptr, vr_theme::font_control);
		float widest_option = 0.0f;
		for (size_t index = 0; index < count; ++index)
			widest_option = (std::max)(widest_option, ImGui::CalcTextSize(options[index]).x);
		const float width = std::ceil(10.0f + ImGui::CalcTextSize(prefix).x + widest_option + 12.0f + height);
		ImGui::PopFont();
		return width;
	}

	// A box showing `prefix` and the current choice with an arrow, so it reads as a list to pick from rather than a button.
	// It is sized to its longest choice, so the text is never cut short.
	dropdown_interaction draw_dropdown(dropdown_state &state, const char *id, const char *prefix, float height,
		const char *const *options, size_t option_count, size_t selected, bool clicked, const bool *disabled_options = nullptr)
	{
		const ImGuiID item_id = ImGui::GetID(id);
		const float box_width = dropdown_width(prefix, options, option_count, height);
		ImGui::PushFont(nullptr, vr_theme::font_control);
		constexpr float text_left = 10.0f;
		const float arrow_width = height;
		const ImVec2 size = { box_width, height };
		const std::string label = std::string(prefix) + (selected < option_count ? options[selected] : "");

		ImGui::InvisibleButton(id, size);
		record_last_item(id);
		dropdown_interaction interaction;
		interaction.hovered = ImGui::IsItemHovered();
		const bool open = state.open_id == item_id;
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const bool highlighted = interaction.hovered || open;
		draw_list->AddRectFilled(minimum, maximum, highlighted ? colors().surface_hover : colors().surface_raised, vr_theme::radius_sm);
		draw_list->AddRect(minimum, maximum, highlighted ? colors().accent : colors().border, vr_theme::radius_sm, 0, vr_theme::stroke_thin);

		const float arrow_left = maximum.x - arrow_width;
		const ImVec2 arrow_center = { arrow_left + arrow_width * 0.5f, (minimum.y + maximum.y) * 0.5f };
		const float half = (std::min)(arrow_width * 0.18f, 6.0f);
		const ImU32 text_color = colors().text;
		const ImU32 arrow_color = highlighted ? colors().accent_hover : colors().text_secondary;
		const float direction = open ? -1.0f : 1.0f;
		const ImVec2 chevron[3] = { { arrow_center.x - half, arrow_center.y - half * 0.5f * direction },
			{ arrow_center.x, arrow_center.y + half * 0.5f * direction }, { arrow_center.x + half, arrow_center.y - half * 0.5f * direction } };
		draw_list->AddPolyline(chevron, 3, arrow_color, ImDrawFlags_None, 2.5f);
		draw_list->AddText({ minimum.x + text_left, std::floor(minimum.y + (size.y - ImGui::GetFontSize()) * 0.5f) }, text_color, label.c_str());
		ImGui::PopFont();

		if (clicked && interaction.hovered && !open)
		{
			state.open_id = item_id;
			state.open_name = id;
			state.options.assign(options, options + option_count);
			state.disabled.assign(option_count, false);
			if (disabled_options != nullptr)
				for (size_t index = 0; index < option_count; ++index)
					state.disabled[index] = disabled_options[index];
			state.selected = selected;
		}
		if (state.open_id == item_id)
		{
			state.box_min = minimum;
			state.box_max = maximum;
			state.box_drawn = true;
		}
		if (state.chosen_id == item_id)
		{
			interaction.chosen = true;
			interaction.option = state.chosen_option;
		}
		return interaction;
	}

	// Call after every control is drawn. A list whose box was not drawn this frame (another page is shown) closes.
	void draw_open_dropdown(dropdown_state &state, bool pointer_visible, ImVec2 pointer, float display_height)
	{
		if (state.open_id != 0 && !state.box_drawn)
			state.open_id = 0;
		state.box_drawn = false;
		if (state.open_id == 0)
			return;

		const ImVec2 list_min = dropdown_list_min(state, display_height);
		const float row_height = state.box_max.y - state.box_min.y;
		const ImVec2 list_max = { state.box_max.x, list_min.y + row_height * static_cast<float>(state.options.size()) };
		const ptrdiff_t hovered = pointer_visible ? dropdown_option_at(state, pointer, display_height) : -1;
		ImDrawList *const draw_list = ImGui::GetForegroundDrawList();
		ImGui::PushFont(nullptr, vr_theme::font_control);
		draw_list->AddRectFilled({ list_min.x + 4.0f, list_min.y + 6.0f }, { list_max.x + 4.0f, list_max.y + 6.0f }, colors().shadow, vr_theme::radius_md);
		draw_list->AddRectFilled(list_min, list_max, colors().surface_raised, vr_theme::radius_md);
		for (size_t index = 0; index < state.options.size(); ++index)
		{
			const ImVec2 row_min = { list_min.x, list_min.y + row_height * static_cast<float>(index) };
			const ImVec2 row_max = { list_max.x, row_min.y + row_height };
			const bool option_disabled = index < state.disabled.size() && state.disabled[index];
			if (s_recorder != nullptr)
				record_control(std::string(state.open_name) + "/item/" + std::to_string(index), row_min, row_max);
			if (static_cast<ptrdiff_t>(index) == hovered && !option_disabled)
				draw_list->AddRectFilled({ row_min.x + 3.0f, row_min.y + 2.0f }, { row_max.x - 3.0f, row_max.y - 2.0f }, colors().surface_hover, vr_theme::radius_sm);
			if (index == state.selected)
				draw_list->AddRectFilled({ row_min.x + 6.0f, row_min.y + 7.0f }, { row_min.x + 10.0f, row_max.y - 7.0f }, colors().accent, 2.0f);
			draw_list->AddText({ row_min.x + 18.0f, std::floor(row_min.y + (row_height - ImGui::GetFontSize()) * 0.5f) },
				option_disabled ? colors().text_disabled : index == state.selected ? colors().text : colors().text_secondary, state.options[index].c_str());
		}
		draw_list->AddRect(list_min, list_max, colors().accent, vr_theme::radius_md, 0, vr_theme::stroke_thin);
		ImGui::PopFont();
	}
}

namespace openxr_ui
{
	struct panel_renderer::implementation
	{
		enum class graphics_api
		{
			none,
			d3d11,
			d3d12,
			opengl,
		};

		enum class drag_target
		{
			none,
			technique_scroll,
			value_slider,
			parameter_scroll,
		};

		ImGuiContext *context = nullptr;
		graphics_api api = graphics_api::none;
		ID3D11DeviceContext *device_context = nullptr;
		ID3D12DescriptorHeap *srv_descriptor_heap = nullptr;
		uint32_t srv_descriptor_increment = 0;
		std::vector<bool> srv_descriptors_in_use;
		opengl_functions gl;
		HGLRC opengl_context = nullptr;
		unsigned int opengl_framebuffer = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		size_t technique_scroll = 0;
		size_t parameter_scroll = 0;
		uint64_t selected_technique = 0;
		enum class tab
		{
			effects,
			settings,
		};
		enum class list_filter
		{
			all,
			starred,
			hidden,
		};
		tab current_tab = tab::effects;
		list_filter filter = list_filter::all;
		// "Reset this effect" needs a second press; the flag clears when the effect or page changes, or after 4 s.
		bool reset_armed = false;
		uint32_t reset_armed_frames = 0;
		uint64_t previous_selected_technique = 0;
		tab previous_tab = tab::effects;
		drag_target dragging = drag_target::none;
		uint64_t dragged_parameter = 0;
		uint32_t dragged_component = 0;
		float drag_start_pointer = 0.0f;
		float drag_start_fraction = 0.0f;
		uint32_t release_frame_count = 0;
		int joystick_scroll_direction = 0;
		uint32_t joystick_scroll_frame_count = 0;
		int save_feedback = 0;
		uint32_t save_feedback_frame_count = 0;
		int save_as_feedback = 0;
		uint32_t save_as_feedback_frame_count = 0;
		bool save_as_dialog_open = false;
		bool save_as_uppercase = true;
		std::string save_as_name;
		dropdown_state dropdown;
		control_recorder recorder;
		bool control_recording = false;
		uint32_t applied_theme = no_theme_applied;
		bool srgb_target = false;

		// The thumbstick moves a list one row per push, then keeps moving while held.
		void update_joystick_scroll(float scroll_y, size_t &scroll, size_t maximum_scroll)
		{
			if (dragging != drag_target::none)
			{
				joystick_scroll_direction = 0;
				joystick_scroll_frame_count = 0;
				return;
			}
			const int scroll_direction = scroll_y > joystick_scroll_dead_zone ? -1 : scroll_y < -joystick_scroll_dead_zone ? 1 : 0;
			bool advance_scroll = false;
			if (scroll_direction != joystick_scroll_direction)
			{
				joystick_scroll_direction = scroll_direction;
				joystick_scroll_frame_count = 0;
				advance_scroll = scroll_direction != 0;
			}
			else if (scroll_direction != 0 && ++joystick_scroll_frame_count >= joystick_scroll_repeat_frames)
			{
				joystick_scroll_frame_count = 0;
				advance_scroll = true;
			}
			if (advance_scroll && scroll_direction < 0 && scroll > 0)
				--scroll;
			else if (advance_scroll && scroll_direction > 0 && scroll < maximum_scroll)
				++scroll;
		}

		// Each panel has its own ImGui context, so each applies a newly selected theme to its own style.
		void apply_selected_theme()
		{
			if (applied_theme == vr_theme::selected_theme())
				return;
			apply_style();
			applied_theme = vr_theme::selected_theme();
		}

		void finish_frame()
		{
			ImGui::Render();
			if (srgb_target)
				convert_draw_data_to_linear(ImGui::GetDrawData());
		}
	};

	panel_renderer::panel_renderer() :
		_implementation(std::make_unique<implementation>())
	{
	}

	panel_renderer::~panel_renderer()
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr)
			return;
		ImGui::SetCurrentContext(_implementation->context);
		if (_implementation->api == implementation::graphics_api::opengl)
		{
			// Deleting OpenGL objects in another context would delete that context's objects instead; the owning context frees them when it is destroyed.
			if (_implementation->gl.GetCurrentContext() != _implementation->opengl_context)
				return;
			if (_implementation->opengl_framebuffer != 0)
				_implementation->gl.DeleteFramebuffers(1, &_implementation->opengl_framebuffer);
			ImGui_ImplOpenGL3_Shutdown();
		}
		else if (_implementation->api == implementation::graphics_api::d3d11)
			ImGui_ImplDX11_Shutdown();
		else if (_implementation->api == implementation::graphics_api::d3d12)
			ImGui_ImplDX12_Shutdown();
		ImGui::DestroyContext(_implementation->context);
		if (_implementation->device_context != nullptr)
			_implementation->device_context->Release();
		if (_implementation->srv_descriptor_heap != nullptr)
			_implementation->srv_descriptor_heap->Release();
	}

	bool panel_renderer::initialize_context(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0 || _implementation->context != nullptr)
			return false;

		_implementation->context = ImGui::CreateContext();
		ImGui::SetCurrentContext(_implementation->context);
		ImGuiIO &io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		io.DisplaySize = { static_cast<float>(width), static_cast<float>(height) };
		// The atlas only reads the embedded font bytes; it must not free them.
		ImFontConfig font_config;
		font_config.FontDataOwnedByAtlas = false;
		io.Fonts->Clear();
		io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(vr_ui_fonts::regular), static_cast<int>(sizeof(vr_ui_fonts::regular)),
			vr_theme::font_body, &font_config);
		io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(vr_ui_fonts::bold), static_cast<int>(sizeof(vr_ui_fonts::bold)),
			vr_theme::font_body, &font_config);
		_implementation->apply_selected_theme();
		prebake_fonts();

		_implementation->width = width;
		_implementation->height = height;
		return true;
	}

	bool panel_renderer::initialize(ID3D11Device *device, ID3D11DeviceContext *context, uint32_t width, uint32_t height)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (device == nullptr || context == nullptr || !initialize_context(width, height))
			return false;

		if (!ImGui_ImplDX11_Init(device, context))
		{
			ImGui::DestroyContext(_implementation->context);
			_implementation->context = nullptr;
			return false;
		}

		_implementation->api = implementation::graphics_api::d3d11;
		_implementation->device_context = context;
		_implementation->device_context->AddRef();
		return true;
	}

	bool panel_renderer::initialize(ID3D12Device *device, ID3D12CommandQueue *queue, uint32_t frame_count, int64_t format, uint32_t width, uint32_t height)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (device == nullptr || queue == nullptr || frame_count == 0 || !initialize_context(width, height))
			return false;

		D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap_desc.NumDescriptors = 16;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&_implementation->srv_descriptor_heap))))
		{
			ImGui::DestroyContext(_implementation->context);
			_implementation->context = nullptr;
			return false;
		}
		_implementation->srv_descriptor_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		_implementation->srv_descriptors_in_use.resize(heap_desc.NumDescriptors, false);

		ImGui_ImplDX12_InitInfo init_info = {};
		init_info.Device = device;
		init_info.CommandQueue = queue;
		init_info.NumFramesInFlight = static_cast<int>(frame_count);
		init_info.RTVFormat = static_cast<DXGI_FORMAT>(format);
		init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
		init_info.UserData = _implementation.get();
		init_info.SrvDescriptorHeap = _implementation->srv_descriptor_heap;
		init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE *cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE *gpu_handle) {
			auto &renderer = *static_cast<implementation *>(info->UserData);
			for (size_t index = 0; index < renderer.srv_descriptors_in_use.size(); ++index)
			{
				if (renderer.srv_descriptors_in_use[index])
					continue;
				renderer.srv_descriptors_in_use[index] = true;
				*cpu_handle = renderer.srv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
				*gpu_handle = renderer.srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart();
				cpu_handle->ptr += index * renderer.srv_descriptor_increment;
				gpu_handle->ptr += index * renderer.srv_descriptor_increment;
				return;
			}
			*cpu_handle = {};
			*gpu_handle = {};
		};
		init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE) {
			auto &renderer = *static_cast<implementation *>(info->UserData);
			const D3D12_CPU_DESCRIPTOR_HANDLE start = renderer.srv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
			const size_t index = (cpu_handle.ptr - start.ptr) / renderer.srv_descriptor_increment;
			if (index < renderer.srv_descriptors_in_use.size())
				renderer.srv_descriptors_in_use[index] = false;
		};
		if (!ImGui_ImplDX12_Init(&init_info))
		{
			_implementation->srv_descriptor_heap->Release();
			_implementation->srv_descriptor_heap = nullptr;
			ImGui::DestroyContext(_implementation->context);
			_implementation->context = nullptr;
			return false;
		}

		_implementation->api = implementation::graphics_api::d3d12;
		return true;
	}

	bool panel_renderer::initialize_opengl(uint32_t width, uint32_t height)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		implementation &renderer = *_implementation;
		if (!renderer.gl.load() || (renderer.opengl_context = renderer.gl.GetCurrentContext()) == nullptr ||
			!initialize_context(width, height))
			return false;

		if (!ImGui_ImplOpenGL3_Init(nullptr))
		{
			ImGui::DestroyContext(renderer.context);
			renderer.context = nullptr;
			return false;
		}
		renderer.api = implementation::graphics_api::opengl;
		renderer.gl.GenFramebuffers(1, &renderer.opengl_framebuffer);
		return renderer.opengl_framebuffer != 0;
	}

	void panel_renderer::select_theme(uint32_t theme)
	{
		vr_theme::select_theme(theme);
	}

	void panel_renderer::show_page(page target, uint64_t technique_id, const std::string &save_as_name)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		_implementation->save_as_dialog_open = target == page::save_as;
		_implementation->save_as_name = save_as_name;
		_implementation->selected_technique = target == page::effect_detail ? technique_id : 0;
		if (target == page::settings)
			_implementation->current_tab = implementation::tab::settings;
		else if (target == page::effects)
			_implementation->current_tab = implementation::tab::effects;
		_implementation->reset_armed = false;
		_implementation->reset_armed_frames = 0;
		_implementation->technique_scroll = 0;
		_implementation->parameter_scroll = 0;
	}

	void panel_renderer::set_control_recording(bool enabled)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		_implementation->control_recording = enabled;
		if (!enabled)
			_implementation->recorder = {};
	}

	bool panel_renderer::find_control(std::string_view id, float &center_x, float &center_y) const
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		for (const control_rect &control : _implementation->recorder.controls)
		{
			if (control.id != id)
				continue;
			center_x = (control.min_x + control.max_x) * 0.5f;
			center_y = (control.min_y + control.max_y) * 0.5f;
			return true;
		}
		return false;
	}

	const std::vector<panel_renderer::control_rect> &panel_renderer::recorded_controls() const
	{
		return _implementation->recorder.controls;
	}

	void panel_renderer::set_srgb_target(bool srgb)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		_implementation->srgb_target = srgb;
	}

	void panel_renderer::notify_save_result(bool succeeded)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		_implementation->save_feedback = succeeded ? 1 : -1;
		_implementation->save_feedback_frame_count = save_feedback_frames;
	}

	void panel_renderer::notify_save_as_started(bool started)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		_implementation->save_as_feedback = started ? 2 : -1;
		_implementation->save_as_feedback_frame_count = started ? 0 : save_feedback_frames;
		if (started)
		{
			_implementation->save_as_dialog_open = false;
			_implementation->save_as_name.clear();
		}
	}

	void panel_renderer::notify_save_as_result(bool succeeded)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		_implementation->save_as_feedback = succeeded ? 1 : -1;
		_implementation->save_as_feedback_frame_count = save_feedback_frames;
	}

	render_result panel_renderer::render_ui(const technique_snapshot &snapshot, const render_input &input)
	{
		render_result result;
		const recorder_scope recorder_reset;
		s_recorder = _implementation->control_recording ? &_implementation->recorder : nullptr;
		if (s_recorder != nullptr)
		{
			s_recorder->controls.clear();
			s_recorder->prefix.clear();
		}
		const char *const graphics_api_name = _implementation->api == implementation::graphics_api::d3d11 ? "DX11" :
			_implementation->api == implementation::graphics_api::d3d12 ? "DX12" :
			_implementation->api == implementation::graphics_api::opengl ? "OpenGL" : "Unknown";
		char graphics_api_label[48];
		if (input.eye_width != 0 && input.eye_height != 0)
			std::snprintf(graphics_api_label, sizeof(graphics_api_label), "%s, %ux%u per eye", graphics_api_name, input.eye_width, input.eye_height);
		else
			std::snprintf(graphics_api_label, sizeof(graphics_api_label), "%s", graphics_api_name);
		ImGuiIO &io = ImGui::GetIO();
		const float display_height = input.expanded ? static_cast<float>(expanded_panel_height) : static_cast<float>(compact_panel_height);
		io.DisplaySize = { static_cast<float>(_implementation->width), display_height };
		io.DeltaTime = 1.0f / 90.0f;
		const ImVec2 pointer = { input.pointer_x, input.pointer_y };
		const bool dropdown_takes_pointer = update_dropdown(_implementation->dropdown, input.pointer_visible, pointer,
			input.select_clicked && _implementation->dragging == implementation::drag_target::none, display_height);
		const bool pointer_reaches_controls = input.pointer_visible && !dropdown_takes_pointer;
		io.AddMousePosEvent(pointer_reaches_controls ? input.pointer_x : -FLT_MAX, pointer_reaches_controls ? input.pointer_y : -FLT_MAX);
		if (_implementation->dragging != implementation::drag_target::none)
		{
			if (input.select_down)
				_implementation->release_frame_count = 0;
			else if (input.select_released || _implementation->release_frame_count != 0)
			{
				if (++_implementation->release_frame_count >= drag_release_frames)
				{
					_implementation->dragging = implementation::drag_target::none;
					_implementation->release_frame_count = 0;
				}
			}
		}

		vr_theme::select_theme(input.theme);
		_implementation->apply_selected_theme();
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 0.0f, 0.0f });
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("ReShade VR UI Add-on", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

		const auto selected_it = std::find_if(snapshot.techniques.begin(), snapshot.techniques.end(), [this](const technique &entry) {
			return entry.id == _implementation->selected_technique;
		});
		if (_implementation->selected_technique != 0 && selected_it == snapshot.techniques.end())
			_implementation->selected_technique = 0;
		if (_implementation->selected_technique != _implementation->previous_selected_technique ||
			_implementation->current_tab != _implementation->previous_tab || _implementation->save_as_dialog_open)
		{
			_implementation->previous_selected_technique = _implementation->selected_technique;
			_implementation->previous_tab = _implementation->current_tab;
			_implementation->reset_armed = false;
		}
		if (!_implementation->reset_armed)
			_implementation->reset_armed_frames = 0;
		else if (++_implementation->reset_armed_frames > 360)
			_implementation->reset_armed = false;

		// Header shared by the effects/settings pages (title + tabs) and the effect page (back + name); y 24..64.
		// It handles its own clicks. The "Effects on" / "This effect on" switch sits left of the pin/expand/close icons.
		const auto draw_header = [&](const technique *detail) {
			const float right_edge = io.DisplaySize.x - outer_padding;
			const float top = outer_padding;
			const float icon_y = top + space_xs;
			ImDrawList *const draw_list = ImGui::GetWindowDrawList();
			ImGui::SetCursorScreenPos({ right_edge - control_height, icon_y });
			const bool close_hovered = draw_icon_button("close", button_icon::close, { control_height, control_height });
			draw_hover_help("Close menu", nullptr);
			ImGui::SetCursorScreenPos({ right_edge - control_height * 2.0f - space_sm, icon_y });
			const bool expand_hovered = draw_icon_button("expand", input.expanded ? button_icon::collapse : button_icon::expand, { control_height, control_height });
			draw_hover_help(input.expanded ? "Make panel shorter" : "Make panel taller", nullptr);
			const float pin_x = right_edge - control_height * 3.0f - space_sm * 2.0f;
			ImGui::SetCursorScreenPos({ pin_x, icon_y });
			const bool pin_hovered = draw_icon_button("pin", button_icon::pin, { control_height, control_height }, true, input.panel_locked);
			draw_hover_help(input.panel_locked ? "Let the panel follow your head again" : "Keep panel in place (stops it following your head)", nullptr);

			const char *const pill_label = detail != nullptr ? "This effect on" : "Effects on";
			const float pill_x = pin_x - space_md - switch_pill_width(pill_label);
			ImGui::SetCursorScreenPos({ pill_x, top });
			const bool pill_hovered = draw_switch_pill(detail != nullptr ? "technique_on" : "effects_on", pill_label,
				detail != nullptr ? detail->enabled : !input.shaders_temporarily_disabled);
			draw_hover_help(detail != nullptr ? "Turn this effect off or on" :
				"Turn every effect off or on, to compare with and without. Your choices are kept.", nullptr);

			bool back_hovered = false;
			int hovered_tab = -1;
			if (detail == nullptr)
			{
				ImGui::PushFont(bold_font(), vr_theme::font_title);
				const char *const title = "ReShade";
				draw_list->AddText({ outer_padding, std::floor(top + (switch_pill_height - ImGui::GetFontSize()) * 0.5f) }, colors().text, title);
				const float tabs_x = outer_padding + std::ceil(ImGui::CalcTextSize(title).x) + space_md;
				ImGui::PopFont();
				const char *const tab_labels[] = { "Effects", "Settings / Gestures" };
				ImGui::SetCursorScreenPos({ tabs_x, top });
				hovered_tab = draw_segmented("tab", tab_labels, nullptr, 2, _implementation->current_tab == implementation::tab::settings ? 1 : 0, true);
			}
			else
			{
				const float back_width = chevron_button_width("Effects");
				ImGui::SetCursorScreenPos({ outer_padding, icon_y });
				back_hovered = draw_text_button("back", "Effects", { back_width, control_height });
				const ImVec2 back_min = ImGui::GetItemRectMin();
				draw_label_chevron({ back_min.x + 16.0f, back_min.y + control_height * 0.5f }, false, colors().text);

				const float name_x = outer_padding + back_width + space_md;
				const float text_right = pill_x - space_md;
				ImGui::PushFont(bold_font(), vr_theme::font_title);
				const std::string fitted_name = fit_text(detail->name.c_str(), text_right - name_x);
				draw_list->AddText({ name_x, std::floor(top + (switch_pill_height - ImGui::GetFontSize()) * 0.5f) }, colors().text, fitted_name.c_str());
				const float file_x = name_x + std::ceil(ImGui::CalcTextSize(fitted_name.c_str()).x) + space_sm;
				ImGui::PopFont();
				if (file_x + 40.0f < text_right)
				{
					ImGui::PushFont(nullptr, vr_theme::font_small);
					const std::string fitted_file = fit_text(detail->effect_name.c_str(), text_right - file_x);
					draw_list->AddText({ file_x, std::floor(top + (switch_pill_height - ImGui::GetFontSize()) * 0.5f) + 1.0f },
						colors().text_secondary, fitted_file.c_str());
					ImGui::PopFont();
				}
				char detail_label[320];
				std::snprintf(detail_label, sizeof(detail_label), "%s  [%s]", detail->name.c_str(), detail->effect_name.c_str());
				draw_hover_details({ name_x, top }, { text_right, top + switch_pill_height }, detail_label, detail->tooltip.c_str());
			}

			if (input.select_clicked && _implementation->dragging == implementation::drag_target::none)
			{
				if (close_hovered)
					result.action = render_result::command::hide_panel;
				else if (expand_hovered)
					result.action = render_result::command::toggle_panel_size;
				else if (pin_hovered)
					result.action = render_result::command::toggle_panel_lock;
				else if (pill_hovered && detail != nullptr)
				{
					result.action = render_result::command::toggle_technique;
					result.target_id = detail->id;
				}
				else if (pill_hovered)
					result.action = render_result::command::toggle_all_techniques;
				else if (back_hovered)
				{
					_implementation->selected_technique = 0;
					_implementation->parameter_scroll = 0;
				}
				else if (hovered_tab >= 0)
					_implementation->current_tab = hovered_tab == 1 ? implementation::tab::settings : implementation::tab::effects;
			}
		};

		if (_implementation->save_as_dialog_open)
		{
			ImGui::PushFont(bold_font(), vr_theme::font_title);
			ImGui::TextUnformatted("Save preset as");
			ImGui::PopFont();
			ImGui::SameLine();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
			ImGui::TextDisabled("Graphics: %s", graphics_api_label);
			ImGui::SetCursorScreenPos({ io.DisplaySize.x - outer_padding - 34.0f, 18.0f });
			const bool close_dialog_hovered = draw_icon_button("close_save_as", button_icon::close, { 34.0f, 30.0f });

			ImGui::SetCursorScreenPos({ outer_padding, 58.0f });
			ImGui::TextDisabled("Preset name");
			const ImVec2 name_min = { outer_padding, 88.0f };
			const ImVec2 name_max = { io.DisplaySize.x - outer_padding, 136.0f };
			ImGui::GetWindowDrawList()->AddRectFilled(name_min, name_max, colors().surface, vr_theme::radius_md);
			ImGui::GetWindowDrawList()->AddRect(name_min, name_max, colors().accent, vr_theme::radius_md, 0, vr_theme::stroke);
			const std::string displayed_name = fit_text(_implementation->save_as_name.c_str(), name_max.x - name_min.x - 32.0f);
			const ImVec2 name_text_position = { name_min.x + 16.0f, std::floor(name_min.y + (name_max.y - name_min.y - ImGui::GetFontSize()) * 0.5f) };
			ImGui::GetWindowDrawList()->AddText(name_text_position, colors().text, displayed_name.c_str());
			// A caret after the name shows where typed letters go.
			const float caret_x = name_text_position.x + ImGui::CalcTextSize(displayed_name.c_str()).x + 3.0f;
			ImGui::GetWindowDrawList()->AddRectFilled({ caret_x, name_text_position.y }, { caret_x + 2.5f, name_text_position.y + ImGui::GetFontSize() },
				colors().accent);

			auto draw_keyboard_row = [&](const char *keys, float start_x, float y) {
				for (size_t index = 0; keys[index] != '\0'; ++index)
				{
					ImGui::PushID(static_cast<int>(y * 100.0f) + static_cast<int>(index));
					ImGui::SetCursorScreenPos({ start_x + static_cast<float>(index) * 92.0f, y });
					char label[2] = { keys[index], '\0' };
					if (!_implementation->save_as_uppercase && label[0] >= 'A' && label[0] <= 'Z')
						label[0] = static_cast<char>(label[0] - 'A' + 'a');
					const bool hovered = draw_text_button(label, label, { 84.0f, 42.0f });
					if (input.select_clicked && hovered && _implementation->save_as_name.size() < 120)
						_implementation->save_as_name.push_back(label[0]);
					ImGui::PopID();
				}
			};
			set_control_prefix("key/");
			draw_keyboard_row("1234567890", 56.0f, 158.0f);
			draw_keyboard_row("QWERTYUIOP", 56.0f, 208.0f);
			draw_keyboard_row("ASDFGHJKL", 102.0f, 258.0f);
			draw_keyboard_row("ZXCVBNM", 194.0f, 308.0f);

			ImGui::SetCursorScreenPos({ 56.0f, 366.0f });
			const bool case_hovered = draw_text_button("case", _implementation->save_as_uppercase ? "ABC" : "abc", { 104.0f, 42.0f }, true,
				_implementation->save_as_uppercase ? button_tone::toggled : button_tone::neutral);
			ImGui::SetCursorScreenPos({ 168.0f, 366.0f });
			const bool space_hovered = draw_text_button("space", "Space", { 320.0f, 42.0f });
			ImGui::SetCursorScreenPos({ 496.0f, 366.0f });
			const bool backspace_hovered = draw_text_button("backspace", "Backspace", { 160.0f, 42.0f });
			ImGui::SetCursorScreenPos({ 664.0f, 366.0f });
			const bool clear_hovered = draw_text_button("clear", "Clear", { 112.0f, 42.0f });

			ImGui::SetCursorScreenPos({ 56.0f, 430.0f });
			const bool cancel_hovered = draw_text_button("cancel", "Cancel", { 160.0f, 46.0f });
			const bool valid_name = !_implementation->save_as_name.empty() &&
				_implementation->save_as_name.back() != ' ' && _implementation->save_as_name.back() != '.';
			const char *const create_label = _implementation->save_as_feedback < 0 ? "Failed" : "Create";
			ImGui::SetCursorScreenPos({ io.DisplaySize.x - outer_padding - 160.0f, 430.0f });
			const bool create_hovered = draw_text_button("create", create_label, { 160.0f, 46.0f }, valid_name,
				_implementation->save_as_feedback < 0 ? button_tone::danger : button_tone::primary);
			clear_control_prefix();

			if (input.select_clicked && (close_dialog_hovered || cancel_hovered))
			{
				_implementation->save_as_dialog_open = false;
				_implementation->save_as_name.clear();
			}
			else if (input.select_clicked && case_hovered)
				_implementation->save_as_uppercase = !_implementation->save_as_uppercase;
			else if (input.select_clicked && space_hovered && !_implementation->save_as_name.empty() &&
				_implementation->save_as_name.back() != ' ' && _implementation->save_as_name.size() < 120)
				_implementation->save_as_name.push_back(' ');
			else if (input.select_clicked && backspace_hovered && !_implementation->save_as_name.empty())
				_implementation->save_as_name.pop_back();
			else if (input.select_clicked && clear_hovered)
				_implementation->save_as_name.clear();
			else if (input.select_clicked && create_hovered)
			{
				result.action = render_result::command::save_preset_as;
				result.text = _implementation->save_as_name;
			}
		}
		else if (_implementation->selected_technique == 0)
		{
			const bool settings_tab = _implementation->current_tab == implementation::tab::settings;
			std::vector<const technique *> displayed_techniques;
			displayed_techniques.reserve(snapshot.techniques.size());
			size_t count_all = 0, count_starred = 0, count_hidden = 0, count_enabled = 0;
			for (const technique &entry : snapshot.techniques)
			{
				count_all += entry.hidden ? 0 : 1;
				count_starred += entry.favourite && !entry.hidden ? 1 : 0;
				count_hidden += entry.hidden ? 1 : 0;
				count_enabled += entry.enabled ? 1 : 0;
				const bool shown = _implementation->filter == implementation::list_filter::hidden ? entry.hidden :
					_implementation->filter == implementation::list_filter::starred ? entry.favourite && !entry.hidden : !entry.hidden;
				if (shown)
					displayed_techniques.push_back(&entry);
			}

			const float right_edge = ImGui::GetWindowWidth() - outer_padding;
			ImDrawList *const header_draw_list = ImGui::GetWindowDrawList();
			draw_header(nullptr);
			const bool can_click = input.select_clicked && _implementation->dragging == implementation::drag_target::none;

			bool previous_preset_hovered = false, next_preset_hovered = false, save_hovered = false, save_as_hovered = false, delete_preset_hovered = false;
			if (!settings_tab)
			{
			// Preset bar, y 80..112.
			constexpr float preset_y = 80.0f;
			float preset_group_x = right_edge - (88.0f + 128.0f + 96.0f + space_sm * 2.0f);
			{
				const float text_y = std::floor(preset_y + (control_height - ImGui::GetFontSize()) * 0.5f);
				header_draw_list->AddText({ outer_padding, text_y }, colors().text_secondary, "Preset");
				const float previous_x = outer_padding + std::ceil(ImGui::CalcTextSize("Preset").x) + space_sm;
				ImGui::SetCursorScreenPos({ previous_x, preset_y });
				previous_preset_hovered = draw_icon_button("previous_preset", button_icon::previous, { control_height, control_height }, !input.shaders_temporarily_disabled);
				const float name_x = previous_x + control_height + space_sm;
				const char *const preset_name = input.preset_name.empty() ? "No preset" : input.preset_name.c_str();
				ImGui::PushFont(nullptr, vr_theme::font_small);
				const float dirty_width = input.preset_dirty ? 16.0f + std::ceil(ImGui::CalcTextSize("Unsaved changes").x) : 0.0f;
				const float dirty_text_y = std::floor(preset_y + (control_height - ImGui::GetFontSize()) * 0.5f);
				ImGui::PopFont();
				const float reserved = control_height + space_sm + (input.preset_dirty ? space_md + dirty_width : 0.0f) + space_md;
				ImGui::PushFont(bold_font(), vr_theme::font_body);
				const float box_limit = (std::max)(96.0f, preset_group_x - reserved - name_x);
				const float box_width = (std::min)((std::max)(220.0f, std::ceil(ImGui::CalcTextSize(preset_name).x) + space_md * 2.0f), box_limit);
				const std::string preset_label = fit_text(preset_name, box_width - space_md * 2.0f);
				header_draw_list->AddRectFilled({ name_x, preset_y }, { name_x + box_width, preset_y + control_height }, colors().inset, vr_theme::radius_sm);
				header_draw_list->AddText({ name_x + space_md, std::floor(preset_y + (control_height - ImGui::GetFontSize()) * 0.5f) }, colors().text, preset_label.c_str());
				ImGui::PopFont();
				draw_hover_details({ name_x, preset_y }, { name_x + box_width, preset_y + control_height }, preset_name);
				const float next_x = name_x + box_width + space_sm;
				ImGui::SetCursorScreenPos({ next_x, preset_y });
				next_preset_hovered = draw_icon_button("next_preset", button_icon::next, { control_height, control_height }, !input.shaders_temporarily_disabled);
				if (input.preset_dirty)
					draw_status_dot_text(header_draw_list, { next_x + control_height + space_md, dirty_text_y }, colors().warning, "Unsaved changes");
			}
			const char *const save_label = input.preset_dirty ? "Save" : _implementation->save_feedback > 0 ? "Saved" : _implementation->save_feedback < 0 ? "Failed" : "Save";
			const button_tone save_tone = input.preset_dirty ? button_tone::primary :
				_implementation->save_feedback > 0 ? button_tone::success :
				_implementation->save_feedback < 0 ? button_tone::danger : button_tone::neutral;
			ImGui::SetCursorScreenPos({ preset_group_x, preset_y });
			save_hovered = draw_text_button("save", save_label, { 88.0f, control_height }, !input.shaders_temporarily_disabled, save_tone);
			const char *const save_as_label = _implementation->save_as_feedback == 2 ? "Creating..." :
				_implementation->save_as_feedback > 0 ? "Created" : _implementation->save_as_feedback < 0 ? "Failed" : "Save as...";
			const button_tone save_as_tone = _implementation->save_as_feedback == 2 ? button_tone::warning :
				_implementation->save_as_feedback > 0 ? button_tone::success :
				_implementation->save_as_feedback < 0 ? button_tone::danger : button_tone::neutral;
			ImGui::SetCursorScreenPos({ preset_group_x + 88.0f + space_sm, preset_y });
			save_as_hovered = draw_text_button("save_as", save_as_label, { 128.0f, control_height },
				!input.shaders_temporarily_disabled && _implementation->save_as_feedback != 2, save_as_tone);
			ImGui::SetCursorScreenPos({ preset_group_x + 88.0f + 128.0f + space_sm * 2.0f, preset_y });
			delete_preset_hovered = draw_text_button("delete_preset", "Delete", { 96.0f, control_height },
				!input.shaders_temporarily_disabled && _implementation->save_as_feedback != 2, button_tone::destructive);

			// List bar, y 120..160.
			char count_labels[3][16];
			std::snprintf(count_labels[0], sizeof(count_labels[0]), "%zu", count_all);
			std::snprintf(count_labels[1], sizeof(count_labels[1]), "%zu", count_starred);
			std::snprintf(count_labels[2], sizeof(count_labels[2]), "%zu", count_hidden);
			const char *const filter_labels[] = { "All", "Starred", "Hidden" };
			const char *const filter_counts[] = { count_labels[0], count_labels[1], count_labels[2] };
			constexpr float list_bar_y = 120.0f;
			ImGui::SetCursorScreenPos({ outer_padding, list_bar_y });
			const int hovered_filter = draw_segmented("filter", filter_labels, filter_counts, 3, static_cast<size_t>(_implementation->filter), false);
			const float active_x = outer_padding + segmented_width(filter_labels, filter_counts, 3, false) + space_md;
			const float active_width = chevron_label_button_width("Active on top");
			ImGui::SetCursorScreenPos({ active_x, list_bar_y + space_xs });
			const bool active_first_hovered = draw_chevron_label_button("active_first", "Active on top", { active_width, control_height }, true, true);
			draw_hover_help("Moves every effect that is on to the top of the list. Effects keep their order among themselves; nothing else is rearranged.", nullptr);
			ImGui::SetCursorScreenPos({ outer_padding, list_bar_y + 40.0f + space_md });

			if (can_click && previous_preset_hovered)
				result.action = render_result::command::previous_preset;
			else if (can_click && next_preset_hovered)
				result.action = render_result::command::next_preset;
			else if (can_click && save_hovered)
				result.action = render_result::command::save_preset;
			else if (can_click && save_as_hovered)
			{
				_implementation->save_as_dialog_open = true;
				_implementation->save_as_name = input.preset_name.empty() ? "New Preset" : input.preset_name + " Copy";
				_implementation->save_as_feedback = 0;
				_implementation->save_as_feedback_frame_count = 0;
			}
			else if (can_click && delete_preset_hovered)
				result.action = render_result::command::delete_preset;
			else if (can_click && active_first_hovered)
			{
				result.action = render_result::command::move_active_to_top;
				_implementation->technique_scroll = 0;
			}
			else if (can_click && hovered_filter >= 0 && static_cast<size_t>(hovered_filter) != static_cast<size_t>(_implementation->filter))
			{
				_implementation->filter = static_cast<implementation::list_filter>(hovered_filter);
				_implementation->technique_scroll = 0;
			}
			}
			else
				ImGui::SetCursorScreenPos({ outer_padding, 80.0f });
			if (!settings_tab && input.effect_subfolders.state != effect_subfolder_state::none)
			{
				const effect_subfolder_status &subfolders = input.effect_subfolders;
				const bool offered = subfolders.state == effect_subfolder_state::offered;
				const bool busy = subfolders.state == effect_subfolder_state::saving || subfolders.state == effect_subfolder_state::loading;
				const char *blocked_reason = offered && input.shaders_temporarily_disabled ? "Turn shaders back on first." : nullptr;
				const bool drops_unsaved_changes = offered && blocked_reason == nullptr && input.preset_dirty;
				char message[160];
				ImU32 accent = colors().warning;
				const char *details = nullptr;
				switch (subfolders.state)
				{
				case effect_subfolder_state::offered:
					if (drops_unsaved_changes)
						std::snprintf(message, sizeof(message), "Loading %u %s from %u %s drops unsaved changes.", subfolders.effect_count,
							subfolders.effect_count == 1 ? "effect" : "effects", subfolders.folder_count,
							subfolders.folder_count == 1 ? "subfolder" : "subfolders");
					else
						std::snprintf(message, sizeof(message), "%u %s in %u %s not loaded.%s%s", subfolders.effect_count,
							subfolders.effect_count == 1 ? "effect" : "effects", subfolders.folder_count,
							subfolders.folder_count == 1 ? "subfolder is" : "subfolders are",
							blocked_reason != nullptr ? " " : "", blocked_reason != nullptr ? blocked_reason : "");
					details = "ReShade only looks for effects directly inside the folders listed under EffectSearchPaths in ReShade.ini, "
						"not in the folders inside them.\n\n"
						"Load subfolders adds \\** to those folders in ReShade.ini, which tells ReShade to look inside every subfolder, "
						"and then reloads all effects. Loading can take a moment and the game may stutter while it happens.\n\n"
						"Reloading re-reads the preset from disk, so unsaved shader changes are lost. Press Save first to keep them.";
					break;
				case effect_subfolder_state::saving:
				case effect_subfolder_state::loading:
					std::snprintf(message, sizeof(message), "Loading effects... %u s. Effects are off until it finishes.", subfolders.loading_seconds);
					accent = colors().accent;
					details = "ReShade is building every effect again, including the ones from the subfolders. "
						"All effects are switched off until it is done, and the list is empty meanwhile.\n\n"
						"The first time this can take up to a minute, because the new effects have never been built before. "
						"The game keeps running; you do not need to do anything.";
					break;
				case effect_subfolder_state::loaded:
					std::snprintf(message, sizeof(message), "Effects from subfolders loaded. No restart needed.");
					accent = colors().success;
					details = "ReShade.ini now includes the subfolders, so they also load at the next game start.";
					break;
				case effect_subfolder_state::restart_needed:
					std::snprintf(message, sizeof(message), "Subfolders saved. Restart the game to load their effects.");
					details = "ReShade.ini now includes the subfolders, but this ReShade did not load them yet. "
						"They load the next time the game starts.";
					break;
				default:
					std::snprintf(message, sizeof(message), "Could not change ReShade.ini.");
					accent = colors().danger;
					details = "ReShade did not save the setting, for example because ReShade.ini is read-only.\n\n"
						"To fix it by hand, open ReShade.ini, find EffectSearchPaths and add \\** to the end of each folder, "
						"for example .\\reshade-shaders\\Shaders\\**. Then restart the game.";
					break;
				}

				const float notice_height = 40.0f;
				const ImVec2 notice_min = ImGui::GetCursorScreenPos();
				const ImVec2 notice_max = { ImGui::GetWindowWidth() - outer_padding, notice_min.y + notice_height };
				const float buttons_width = offered ? 110.0f + 196.0f : busy ? 0.0f : 80.0f;
				float buttons_x = notice_max.x - 5.0f - buttons_width;
				draw_notice(notice_min, notice_max, buttons_x - 10.0f, accent, busy, message, details);

				const float button_y = notice_min.y + 5.0f;
				const ImVec2 button_size = { 0.0f, notice_height - 10.0f };
				bool primary_hovered = false;
				bool dismiss_hovered = false;
				if (offered)
				{
					ImGui::SetCursorScreenPos({ buttons_x, button_y });
					primary_hovered = draw_text_button("subfolders_load", drops_unsaved_changes ? "Load anyway" : "Load subfolders",
						{ 188.0f, button_size.y }, blocked_reason == nullptr,
						drops_unsaved_changes ? button_tone::warning : button_tone::primary);
					buttons_x += 196.0f;
					ImGui::SetCursorScreenPos({ buttons_x, button_y });
					dismiss_hovered = draw_text_button("subfolders_not_now", "Not now", { 110.0f, button_size.y });
				}
				else if (!busy)
				{
					ImGui::SetCursorScreenPos({ buttons_x, button_y });
					dismiss_hovered = draw_text_button("subfolders_ok", "OK", { 80.0f, button_size.y });
				}

				const bool notice_can_click = input.select_clicked && _implementation->dragging == implementation::drag_target::none;
				if (notice_can_click && primary_hovered)
					result.action = render_result::command::include_effect_subfolders;
				else if (notice_can_click && dismiss_hovered)
					result.action = render_result::command::dismiss_effect_subfolder_notice;
				ImGui::SetCursorScreenPos({ notice_min.x, notice_max.y + space_sm });
			}
			if (!settings_tab)
			{
			if (!input.effect_runtime_available)
			{
				char message[160];
				const char *details = nullptr;
				if (input.reshade_loaded)
				{
					std::snprintf(message, sizeof(message), "Waiting for ReShade... %u s. Effects start when the game shows its 3D view.",
						input.effect_runtime_wait_seconds);
					details = "ReShade starts its effects when the game first draws the picture for the headset. "
						"A flat title screen or a loading screen can keep this waiting; that is normal.\n\n"
						"If this keeps counting after the game shows its 3D world in the headset, ReShade could not attach to the "
						"game's graphics. ReShade.log next to the game explains why.";
				}
				else
				{
					std::snprintf(message, sizeof(message), "ReShade is not loaded in this game. Effects cannot start.");
					details = "This panel found no ReShade in the game, so there are no effects to control.\n\n"
						"Install ReShade for this game with OpenXR support turned on, then restart the game.";
				}
				const ImVec2 notice_min = ImGui::GetCursorScreenPos();
				const ImVec2 notice_max = { ImGui::GetWindowWidth() - outer_padding, notice_min.y + 40.0f };
				draw_notice(notice_min, notice_max, notice_max.x - 10.0f,
					input.reshade_loaded ? colors().accent : colors().danger, input.reshade_loaded, message, details);
				ImGui::SetCursorScreenPos({ notice_min.x, notice_max.y + space_sm });
			}
			else if (input.depth_effects_unavailable)
			{
				const ImVec2 notice_min = ImGui::GetCursorScreenPos();
				const ImVec2 notice_max = { ImGui::GetWindowWidth() - outer_padding, notice_min.y + 40.0f };
				draw_notice(notice_min, notice_max, notice_max.x - 10.0f, colors().warning, false,
					"UEVR game: depth effects (MXAO, depth of field, depth fog) do not work.",
					"Effects that need to know how far away things are get no depth information in UEVR games, "
					"so they show nothing or look wrong. Colour, sharpening and tone effects work normally.\n\n"
					"The Depth buffer tick box below does not change this.");
				ImGui::SetCursorScreenPos({ notice_min.x, notice_max.y + space_sm });
			}
			if (input.effect_runtime_available && snapshot.techniques.empty() && (input.effect_subfolders.state == effect_subfolder_state::saving ||
				input.effect_subfolders.state == effect_subfolder_state::loading))
			{
				ImGui::TextColored(vr_theme::to_float4(colors().accent), "ReShade is loading effects");
				ImGui::TextWrapped("The list and all effects come back when loading finishes.");
				ImGui::Separator();
			}
			else if (input.effect_runtime_available && snapshot.techniques.empty())
			{
				ImGui::TextColored(vr_theme::to_float4(colors().warning), "No ReShade techniques found");
				ImGui::TextWrapped("Check EffectSearchPaths in ReShadeVR.ini or ReShade.ini.");
				ImGui::Separator();
			}

			// Rows are list_row_height tall; as many as fit between the list bar and the bottom line (8 px above it).
			const ImVec2 list_min = ImGui::GetCursorScreenPos();
			const float list_height = display_height - outer_padding - note_height - space_sm - list_min.y;
			const size_t visible_techniques = std::max<size_t>(1, static_cast<size_t>((list_height + list_row_gap) / (list_row_height + list_row_gap)));
			const float row_height = list_row_height;
			const size_t maximum_scroll = displayed_techniques.size() > visible_techniques ? displayed_techniques.size() - visible_techniques : 0;
			_implementation->technique_scroll = (std::min)(_implementation->technique_scroll, maximum_scroll);
			_implementation->update_joystick_scroll(input.scroll_y, _implementation->technique_scroll, maximum_scroll);
			// The scroll bar column is only taken when the rows do not all fit.
			const float row_width = ImGui::GetContentRegionAvail().x - (maximum_scroll > 0 ? scrollbar_width + space_sm : 0.0f);
			const size_t first = _implementation->technique_scroll;
			const size_t last = (std::min)(first + visible_techniques, displayed_techniques.size());
			if (displayed_techniques.empty() && !snapshot.techniques.empty())
			{
				const char *const empty_text = _implementation->filter == implementation::list_filter::starred ?
					"No starred effects. Press the star on an effect to add it here." :
					_implementation->filter == implementation::list_filter::hidden ? "No hidden effects." : "No effects to show.";
				ImGui::PushFont(nullptr, vr_theme::font_body);
				const ImVec2 empty_size = ImGui::CalcTextSize(empty_text);
				ImGui::GetWindowDrawList()->AddText({ std::floor(list_min.x + (row_width - empty_size.x) * 0.5f),
					std::floor(list_min.y + (list_height - empty_size.y) * 0.5f) }, colors().text_secondary, empty_text);
				ImGui::PopFont();
			}
			for (size_t index = first; index < first + visible_techniques; ++index)
			{
				if (index >= last)
					break;
				ImGui::PushID(static_cast<int>(index));
				set_control_row_prefix("effect", index - first);
				const ImVec2 size = { row_width, row_height };
				ImGui::InvisibleButton("technique", size);
				record_last_item("technique");
				const bool row_hovered = ImGui::IsItemHovered();
				const ImVec2 minimum = ImGui::GetItemRectMin();
				const ImVec2 next_row = { minimum.x, minimum.y + row_height + list_row_gap };
				const ImVec2 maximum = ImGui::GetItemRectMax();
				ImDrawList *const draw_list = ImGui::GetWindowDrawList();
				// Empty slots below the last effect stay blank instead of showing a stack of empty bars.
				if (index < last)
					draw_list->AddRectFilled(minimum, maximum, row_hovered ? colors().surface_raised : colors().surface, vr_theme::radius_md);

				if (index < last)
				{
					const technique &entry = *displayed_techniques[index];
					ImGui::SetCursorScreenPos({ minimum.x + 4.0f, minimum.y + 2.0f });
					ImGui::InvisibleButton("toggle", { 56.0f, row_height - 4.0f });
					record_last_item("toggle");
					const bool toggle_hovered = ImGui::IsItemHovered();
					draw_switch_indicator(draw_list, { minimum.x + 12.0f, std::floor(minimum.y + (row_height - switch_track_height) * 0.5f) }, entry.enabled, toggle_hovered);
					// Right columns, right to left: Open 96, Hide 80, star 32, down 32, up 32; 4 px inside the row.
					const float open_x = maximum.x - 4.0f - 96.0f;
					const float hide_x = open_x - space_sm - 80.0f;
					const float star_x = hide_x - space_sm - 32.0f;
					const float down_x = star_x - space_md - 32.0f;
					const float up_x = down_x - space_sm - 32.0f;
					const float name_x = minimum.x + 12.0f + switch_track_width + 12.0f;
					const float text_limit = up_x - space_md;
					char label[320];
					std::snprintf(label, sizeof(label), "%s  [%s]", entry.name.c_str(), entry.effect_name.c_str());
					// The name leads; the file name follows in smaller, quieter text and is shortened first.
					ImGui::PushFont(entry.enabled ? bold_font() : nullptr, vr_theme::font_body);
					const float name_y = std::floor(minimum.y + (row_height - ImGui::GetFontSize()) * 0.5f);
					// A dot after the name keeps the "has changed values" state visible without relying on colour.
					const float dot_room = entry.has_non_default_values ? 16.0f : 0.0f;
					const std::string fitted_name = fit_text(entry.name.c_str(), text_limit - name_x - dot_room);
					const float name_width = std::ceil(ImGui::CalcTextSize(fitted_name.c_str()).x);
					draw_list->AddText({ name_x, name_y }, entry.enabled ? colors().text : colors().text_secondary, fitted_name.c_str());
					ImGui::PopFont();
					float text_end = name_x + name_width;
					if (entry.has_non_default_values)
					{
						draw_list->AddCircleFilled({ text_end + 8.0f, minimum.y + row_height * 0.5f }, 4.0f, colors().changed_value);
						text_end += dot_room;
					}
					const float effect_x = text_end + space_sm;
					if (effect_x + 40.0f < text_limit)
					{
						ImGui::PushFont(nullptr, vr_theme::font_small);
						const std::string fitted_effect = fit_text(entry.effect_name.c_str(), text_limit - effect_x);
						draw_list->AddText({ effect_x, std::floor(minimum.y + (row_height - ImGui::GetFontSize()) * 0.5f) },
							colors().text_disabled, fitted_effect.c_str());
						ImGui::PopFont();
					}
					draw_hover_details({ name_x, minimum.y }, { text_limit, maximum.y }, label, entry.tooltip.c_str());
					const auto order_it = std::find_if(snapshot.techniques.begin(), snapshot.techniques.end(), [&entry](const technique &candidate) {
						return candidate.id == entry.id;
					});
					const bool can_move_up = order_it != snapshot.techniques.end() && order_it != snapshot.techniques.begin();
					const bool can_move_down = order_it != snapshot.techniques.end() && order_it + 1 != snapshot.techniques.end();
					const float button_y = minimum.y + space_xs;
					const float button_height = row_height - space_xs * 2.0f;
					ImGui::SetCursorScreenPos({ up_x, button_y });
					const bool move_up_hovered = draw_icon_button("up", button_icon::up, { 32.0f, button_height }, !input.shaders_temporarily_disabled && can_move_up);
					draw_hover_help(can_move_up ? "Move up (earlier in the effect order)" : "Already at the top", nullptr);
					ImGui::SetCursorScreenPos({ down_x, button_y });
					const bool move_down_hovered = draw_icon_button("down", button_icon::down, { 32.0f, button_height }, !input.shaders_temporarily_disabled && can_move_down);
					draw_hover_help(can_move_down ? "Move down (later in the effect order)" : "Already at the bottom", nullptr);
					const bool move_controls_hovered = ImGui::IsMouseHoveringRect({ up_x, button_y }, { down_x + 32.0f, button_y + button_height });
					ImGui::SetCursorScreenPos({ star_x, button_y });
					const bool favourite_hovered = draw_star_button("star", { 32.0f, button_height }, entry.favourite);
					draw_hover_help(entry.favourite ? "Starred: shows under Starred" : "Star this effect", nullptr);
					ImGui::SetCursorScreenPos({ hide_x, button_y });
					const bool hide_hovered = draw_text_button("hide", entry.hidden ? "Show" : "Hide", { 80.0f, button_height });
					draw_hover_help(entry.hidden ? "Show in the list again" : "Hide from list. Find it again under Hidden.", nullptr);
					ImGui::SetCursorScreenPos({ open_x, button_y });
					const bool open_hovered = draw_chevron_label_button("open", "Open", { 96.0f, button_height }, !input.shaders_temporarily_disabled, false);
					draw_hover_help("Change this effect's values", nullptr);
					if (input.select_clicked && open_hovered && _implementation->dragging == implementation::drag_target::none)
					{
						_implementation->selected_technique = entry.id;
						_implementation->parameter_scroll = 0;
					}
					else if (input.select_clicked && hide_hovered && _implementation->dragging == implementation::drag_target::none)
					{
						result.action = render_result::command::set_technique_hidden;
						result.target_id = entry.id;
						result.state = !entry.hidden;
					}
					else if (input.select_clicked && favourite_hovered && _implementation->dragging == implementation::drag_target::none)
					{
						result.action = render_result::command::set_technique_favourite;
						result.target_id = entry.id;
						result.state = !entry.favourite;
					}
					else if (input.select_clicked && move_controls_hovered && _implementation->dragging == implementation::drag_target::none)
					{
						if (move_up_hovered || move_down_hovered)
						{
							result.action = render_result::command::move_technique;
							result.target_id = entry.id;
							result.state = move_up_hovered;
						}
					}
					else if (input.select_clicked && !input.shaders_temporarily_disabled && (row_hovered || toggle_hovered) && _implementation->dragging == implementation::drag_target::none)
					{
						result.action = render_result::command::toggle_technique;
						result.target_id = entry.id;
					}
				}
				ImGui::SetCursorScreenPos(next_row);
				ImGui::PopID();
				clear_control_prefix();
			}

			ImGui::SetCursorScreenPos({ list_min.x + row_width + space_sm, list_min.y });
			const float scroll_fraction = maximum_scroll > 0 ? static_cast<float>(_implementation->technique_scroll) / static_cast<float>(maximum_scroll) : 0.0f;
			const slider_interaction scroll_slider = draw_vertical_slider("technique_scroll", { scrollbar_width, list_height }, scroll_fraction, maximum_scroll > 0);
			if (input.select_clicked && scroll_slider.hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->dragging = implementation::drag_target::technique_scroll;
				_implementation->release_frame_count = 0;
				_implementation->drag_start_pointer = io.MousePos.y;
				_implementation->drag_start_fraction = scroll_fraction;
			}
			if (_implementation->dragging == implementation::drag_target::technique_scroll && (input.select_clicked || input.select_down))
			{
				const float drag_fraction = std::clamp(_implementation->drag_start_fraction +
					(io.MousePos.y - _implementation->drag_start_pointer) / list_height * 0.9f, 0.0f, 1.0f);
				_implementation->technique_scroll = static_cast<size_t>(std::round(drag_fraction * static_cast<float>(maximum_scroll)));
			}

			// Bottom line: effect counts on the left, graphics and version on the right. The version is always complete;
			// only the resolution part of the graphics label gives way when room runs out.
			{
				char left_text[64];
				std::snprintf(left_text, sizeof(left_text), "%zu effects, %zu on", snapshot.techniques.size(), count_enabled);
				draw_api_version_line(header_draw_list, left_text, graphics_api_label, display_height, right_edge);
			}
			}

			if (settings_tab)
			{
			// Two columns of group cards: a 20 px title, 8 px gap, then a card with 4 px top and bottom padding and 44 px rows.
			constexpr float column_width = 476.0f;
			constexpr float card_padding = 12.0f;
			constexpr float group_gap = 16.0f;
			constexpr float title_block = 28.0f;
			const float left_x = outer_padding;
			const float right_x = right_edge - column_width;
			const auto card_height = [](size_t rows) { return 8.0f + settings_row_height * static_cast<float>(rows) + 2.0f * static_cast<float>(rows - 1); };
			const auto row_top = [](float card_top, size_t row) { return card_top + 4.0f + (settings_row_height + 2.0f) * static_cast<float>(row); };
			const auto separator_after = [&](float x, float row_y) { draw_row_separator(x, x + column_width, row_y + settings_row_height); };
			const auto begin_card = [&](float x, float title_y, float height)
			{
				draw_card({ x, title_y + title_block }, { x + column_width, title_y + title_block + height });
				return title_y + title_block;
			};
			const auto dropdown_row = [&](float x, float row_y, const char *label, const char *id, const char *const *options, size_t count,
				size_t selected, const bool *disabled = nullptr)
			{
				draw_settings_row_label({ x + card_padding, row_y }, settings_row_height, label);
				ImGui::SetCursorScreenPos({ x + column_width - card_padding - dropdown_width("", options, count, control_height),
					row_y + (settings_row_height - control_height) * 0.5f });
				return draw_dropdown(_implementation->dropdown, id, "", control_height, options, count, selected, can_click, disabled);
			};
			const auto switch_row = [&](float x, float row_y, const char *id, const char *label, bool on, const char *note = nullptr)
			{
				return draw_switch_row(id, label, on, { x + card_padding, row_y }, column_width - card_padding * 2.0f, note);
			};

			// The game still sees every button and key press, so each choice may also do something in the game or in a mod such as UEVR.
			const char *const shortcut_details =
				"Shows or hides this menu. The game also receives the press.\n\n"
				"Controller: L3+R3 presses both thumbsticks in. B+Y presses the two upper face buttons together. "
				"The Hold choices, including Hold Menu (the left controller's menu button), need 1.5 seconds.\n"
				"Key: works while the game window is in front.\n\n"
				"UEVR opens its own menu with L3+R3 and Insert, so in UEVR games pick a hold or B+Y, and End or Page Up. "
				"Controller and key cannot both be Off.";

			// Left column: opening this menu.
			float title_y = 80.0f;
			float card_top = begin_card(left_x, title_y, card_height(3));
			draw_group_title("Opening this menu", { left_x, title_y });
			float row_y = row_top(card_top, 0);
			const dropdown_interaction button_choice = dropdown_row(left_x, row_y, "Controller buttons", "menu_button",
				menu_button_names, menu_button_count, input.menu_button);
			draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "Controller buttons", shortcut_details);
			separator_after(left_x, row_y);
			row_y = row_top(card_top, 1);
			const dropdown_interaction key_choice = dropdown_row(left_x, row_y, "Keyboard key", "menu_key",
				menu_key_names, menu_key_count, input.menu_key);
			draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "Keyboard key", shortcut_details);
			separator_after(left_x, row_y);
			const bool starts_open_hovered = switch_row(left_x, row_top(card_top, 2), "starts_open", "Open when the game starts", input.ui_starts_open);

			// Left column: gestures. Only these display strings differ from the saved choices, which keep their order.
			const char *gesture_options[preset_gesture_count] = { "Off", "Head Gesture: Left Trigger", "Head Gesture: Left Grip",
				"Head Gesture: Left X", "Head Gesture: Right Trigger", "Head Gesture: Right Grip", "Head Gesture: Right A", "L3+R3" };
			bool gesture_disabled[preset_gesture_count] = {};
			const bool stick_gesture_enabled = stick_gesture_allowed(input.menu_button, false);
			if (!stick_gesture_enabled)
			{
				gesture_disabled[preset_gesture_both_sticks] = true;
				gesture_options[preset_gesture_both_sticks] = "L3+R3 (already opens this menu)";
			}
			title_y = card_top + card_height(3) + group_gap;
			const char *const gesture_text = "Switch to the next preset without opening the menu: hand to the side of your head + button, "
				"or click both thumbsticks (L3+R3).";
			ImGui::PushFont(nullptr, vr_theme::font_small);
			const float gesture_text_height = std::ceil(ImGui::CalcTextSize(gesture_text, nullptr, false, column_width - card_padding * 2.0f).y);
			ImGui::PopFont();
			card_top = begin_card(left_x, title_y, 8.0f + gesture_text_height + 8.0f + settings_row_height);
			draw_group_title("Gestures", { left_x, title_y });
			ImGui::PushFont(nullptr, vr_theme::font_small);
			ImGui::GetWindowDrawList()->AddText(nullptr, 0.0f, { left_x + card_padding, card_top + 8.0f }, colors().text_secondary, gesture_text,
				nullptr, column_width - card_padding * 2.0f);
			ImGui::PopFont();
			const dropdown_interaction gesture_choice = dropdown_row(left_x, card_top + 8.0f + gesture_text_height + 8.0f, "Next preset", "preset_gesture",
				gesture_options, preset_gesture_count, input.preset_gesture, gesture_disabled);
			draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "Next preset",
				"Bring the chosen hand up to the side of your head, like switching on a head torch, and press the chosen button. "
				"That switches to the next preset. With only one preset, it turns all effects off, and on again the next time.\n\n"
				"L3+R3 needs no hand movement: click both thumbsticks and let go. It is not available while the menu shortcut is "
				"also L3+R3; with Hold L3+R3, a quick click switches preset and a long hold shows the menu.\n\n"
				"Works only while this menu is closed. The game also receives the button press.");

			// Right column: FPS counter.
			title_y = 80.0f;
			card_top = begin_card(right_x, title_y, card_height(2));
			draw_group_title("FPS counter", { right_x, title_y });
			const bool statistics_hovered = switch_row(right_x, row_top(card_top, 0), "always_show_statistics", "Show when menu is closed",
				input.always_show_statistics);
			separator_after(right_x, row_top(card_top, 0));
			static_assert(max_frame_rate_divisor == 3, "frame_rate_options lists one name per divisor");
			const char *const frame_rate_options[max_frame_rate_divisor] = { "Full headset rate", "Half headset rate", "One-third headset rate" };
			const dropdown_interaction frame_rate = dropdown_row(right_x, row_top(card_top, 1), "FPS graph target", "frame_rate_divisor",
				frame_rate_options, max_frame_rate_divisor, input.frame_rate_divisor - 1);
			draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "FPS graph target",
				"Frames at this rate count as on time in the graph. It does not change the game's speed.");

			// Right column: performance. Both settings are read by ReShade only at game start.
			title_y = card_top + card_height(2) + group_gap;
			card_top = begin_card(right_x, title_y, card_height(2));
			draw_group_title("Performance", { right_x, title_y });
			{
				ImGui::PushFont(bold_font(), vr_theme::font_small);
				const float title_width = std::ceil(ImGui::CalcTextSize("PERFORMANCE").x);
				ImGui::GetWindowDrawList()->AddText({ right_x + title_width, title_y }, colors().warning, " \xC2\xB7 needs game restart");
				ImGui::PopFont();
			}
			const std::string restart_first = "Restart the game to apply your change.\n\n";
			const char *const restart_note = "Restart to apply";
			row_y = row_top(card_top, 0);
			const bool performance_hovered = switch_row(right_x, row_y, "performance_mode", "Faster shaders", input.performance_mode,
				input.performance_mode_restart_pending ? restart_note : nullptr);
			draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "Faster shaders",
				((input.performance_mode_restart_pending ? restart_first : std::string()) +
				"ReShade performance mode: effects run faster, but their values can't be changed until you turn it off.").c_str());
			separator_after(right_x, row_y);
			const bool depth_hovered = switch_row(right_x, row_top(card_top, 1), "depth_buffer", "Enable depth-based effects",
				!input.depth_detection_disabled, input.depth_detection_restart_pending ? restart_note : nullptr);
			draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "Enable depth-based effects",
				((input.depth_detection_restart_pending ? restart_first : std::string()) +
				"Needed for effects like MXAO and depth of field. Off is faster in OpenGL games.").c_str());

			// Right column: look.
			title_y = card_top + card_height(2) + group_gap;
			card_top = begin_card(right_x, title_y, card_height(1));
			draw_group_title("Look", { right_x, title_y });
			const dropdown_interaction theme_choice = dropdown_row(right_x, row_top(card_top, 0), "Colour theme", "theme",
				vr_theme::theme_names, vr_theme::theme_count, input.theme);

			// Bottom line: graphics on the left, add-on version on the right.
			{
				char version_label[16];
				openxr_ui::format_version_label(version_label);
				const std::string right_text = std::string("ReShade VR UI Add-on ") + version_label;
				ImGui::PushFont(nullptr, vr_theme::font_small);
				const float bottom_y = display_height - outer_padding - note_height + std::floor((note_height - ImGui::GetFontSize()) * 0.5f);
				const float right_width = std::ceil(ImGui::CalcTextSize(right_text.c_str()).x);
				const std::string left_text = fit_text((std::string("Graphics: ") + graphics_api_label).c_str(),
					(std::max)(0.0f, right_edge - outer_padding - right_width - space_md));
				header_draw_list->AddText({ outer_padding, bottom_y }, colors().text_secondary, left_text.c_str());
				header_draw_list->AddText({ right_edge - right_width, bottom_y }, colors().text_secondary, right_text.c_str());
				ImGui::PopFont();
			}

			if (button_choice.chosen && button_choice.option != input.menu_button &&
				!(button_choice.option == menu_button_off && input.menu_key == menu_key_off))
			{
				result.action = render_result::command::set_menu_button;
				result.value = static_cast<double>(button_choice.option);
			}
			else if (key_choice.chosen && key_choice.option != input.menu_key &&
				!(key_choice.option == menu_key_off && input.menu_button == menu_button_off))
			{
				result.action = render_result::command::set_menu_key;
				result.value = static_cast<double>(key_choice.option);
			}
			else if (gesture_choice.chosen && gesture_choice.option != input.preset_gesture &&
				!(gesture_choice.option == preset_gesture_both_sticks && !stick_gesture_enabled))
			{
				result.action = render_result::command::set_preset_gesture;
				result.value = static_cast<double>(gesture_choice.option);
			}
			else if (input.select_clicked && starts_open_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::set_ui_starts_open;
				result.state = !input.ui_starts_open;
			}
			else if (input.select_clicked && performance_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::set_performance_mode;
				result.state = !input.performance_mode;
			}
			else if (input.select_clicked && statistics_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::set_always_show_statistics;
				result.state = !input.always_show_statistics;
			}
			else if (frame_rate.chosen && frame_rate.option + 1 != input.frame_rate_divisor)
			{
				result.action = render_result::command::set_frame_rate_divisor;
				result.value = static_cast<double>(frame_rate.option + 1);
			}
			else if (theme_choice.chosen && theme_choice.option != input.theme)
			{
				result.action = render_result::command::set_theme;
				result.value = static_cast<double>(theme_choice.option);
			}
			else if (input.select_clicked && depth_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::set_depth_detection_disabled;
				result.state = !input.depth_detection_disabled;
			}
			}
		}
		else
		{
			const technique &selected = *selected_it;
			draw_header(&selected);
			const ImVec2 detail_header = { outer_padding, 80.0f };
			const float right_edge = ImGui::GetWindowWidth() - outer_padding;
			// Top bar: the description on the left, "Restore saved values" and "Reset this effect" on the right.
			const auto top_button_width = [](const char *text) {
				ImGui::PushFont(nullptr, vr_theme::font_control);
				const float width = std::ceil(ImGui::CalcTextSize(text).x) + 32.0f;
				ImGui::PopFont();
				return width;
			};
			// The reset button keeps the width of its longer, armed label so it does not jump when pressed once.
			const float reset_width = (std::max)(top_button_width("Reset this effect"), top_button_width("Press again to reset"));
			const float restore_width = top_button_width("Restore saved values");
			const float reset_x = right_edge - reset_width;
			const float restore_x = reset_x - space_sm - restore_width;
			const bool reset_armed = _implementation->reset_armed;
			ImGui::SetCursorScreenPos({ restore_x, detail_header.y });
			const bool revert_restore_hovered = draw_text_button("revert_restore", "Restore saved values", { restore_width, control_height },
				(selected.has_restore_state || selected.differs_from_saved) && !input.shaders_temporarily_disabled);
			draw_hover_help("Restore saved values", "Go back to the values saved in the preset");
			ImGui::SetCursorScreenPos({ reset_x, detail_header.y });
			const bool reset_all_hovered = draw_text_button("reset_all", reset_armed ? "Press again to reset" : "Reset this effect",
				{ reset_width, control_height }, true, reset_armed ? button_tone::danger : button_tone::neutral);
			draw_hover_help("Reset this effect", "Set every value of this effect to its default. Press twice to confirm.");
			if (!selected.tooltip.empty())
			{
				ImGui::PushFont(nullptr, vr_theme::font_small);
				const float description_width = restore_x - space_md - detail_header.x;
				const std::string fitted_description = fit_text(selected.tooltip.c_str(), description_width);
				ImGui::GetWindowDrawList()->AddText({ detail_header.x, std::floor(detail_header.y + (control_height - ImGui::GetFontSize()) * 0.5f) },
					colors().text_secondary, fitted_description.c_str());
				ImGui::PopFont();
				draw_hover_details({ detail_header.x, detail_header.y }, { detail_header.x + description_width, detail_header.y + control_height },
					selected.tooltip.c_str());
			}
			if (input.select_clicked && reset_all_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				if (!_implementation->reset_armed)
				{
					_implementation->reset_armed = true;
					_implementation->reset_armed_frames = 0;
				}
				else
				{
					_implementation->reset_armed = false;
					result.action = render_result::command::reset_all_parameters;
					result.target_id = selected.id;
				}
			}
			else if (input.select_clicked && revert_restore_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = selected.has_restore_state ? render_result::command::restore_technique : render_result::command::revert_technique;
				result.target_id = selected.id;
			}

			// The performance-mode note, when it applies, takes one line above the list; the full text shows on hover.
			float content_y = detail_header.y + control_height + space_md;
			const auto draw_note_line = [&](const char *text, ImU32 color) {
				ImGui::PushFont(nullptr, vr_theme::font_small);
				const std::string fitted = fit_text(text, right_edge - detail_header.x);
				ImGui::GetWindowDrawList()->AddText({ detail_header.x, content_y - space_xs }, color, fitted.c_str());
				ImGui::PopFont();
				draw_hover_details({ detail_header.x, content_y - space_xs }, { right_edge, content_y - space_xs + note_height }, text);
				content_y += note_height + space_sm;
			};
			if (input.performance_mode_active)
				draw_note_line(input.performance_mode ?
					"Performance mode is on, so this effect's values are fixed and cannot be shown. "
					"Turn off Performance mode and restart the game to adjust them." :
					"Performance mode is still active until the game restarts. Restart the game to adjust this effect's values.",
					colors().changed_value);
			ImGui::SetCursorScreenPos({ detail_header.x, content_y });

			struct component_reference
			{
				const parameter *entry;
				uint32_t component;
			};
			std::vector<component_reference> components;
			for (const parameter &entry : snapshot.parameters)
				if (entry.effect_name == selected.effect_name)
					for (uint32_t component = 0; component < entry.values.size(); ++component)
						components.push_back({ &entry, component });

			const ImVec2 list_min = ImGui::GetCursorScreenPos();
			// Bottom bar: 48 tall, ending 8 px above the bottom line; the list stops 16 px above it.
			constexpr float bottom_bar_height = 48.0f;
			const float bottom_bar_top = display_height - outer_padding - note_height - space_sm - bottom_bar_height;
			const float list_height = bottom_bar_top - space_md - list_min.y;
			constexpr float parameter_row_height = 44.0f;
			constexpr float parameter_row_gap = 4.0f;
			const size_t visible_parameters = std::max<size_t>(1, static_cast<size_t>((list_height + parameter_row_gap) / (parameter_row_height + parameter_row_gap)));
			const float row_height = parameter_row_height;
			const size_t maximum_scroll = components.size() > visible_parameters ? components.size() - visible_parameters : 0;
			_implementation->parameter_scroll = (std::min)(_implementation->parameter_scroll, maximum_scroll);
			_implementation->update_joystick_scroll(input.scroll_y, _implementation->parameter_scroll, maximum_scroll);
			// The scroll bar only takes room when the rows do not fit.
			const float row_width = right_edge - list_min.x - (maximum_scroll > 0 ? scrollbar_width + space_sm : 0.0f);
			const size_t first = _implementation->parameter_scroll;
			const size_t last = (std::min)(first + visible_parameters, components.size());
			for (size_t index = first; index < last; ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				set_control_row_prefix("param", index - first);
				const ImVec2 row_min = ImGui::GetCursorScreenPos();
				const ImVec2 row_max = { row_min.x + row_width, row_min.y + row_height };
				ImGui::GetWindowDrawList()->AddRectFilled(row_min, row_max, colors().surface, vr_theme::radius_md);
				{
					const component_reference &reference = components[index];
					const parameter &entry = *reference.entry;
					char label[256];
					if (entry.values.size() == 1)
						std::snprintf(label, sizeof(label), "%s", entry.name.c_str());
					else
						std::snprintf(label, sizeof(label), "%s [%u]", entry.name.c_str(), reference.component + 1);
					// One line per value: name, slider, value, step buttons, reset. Columns are fixed so every row lines up.
					constexpr float label_width = 300.0f;
					constexpr float value_width = 64.0f;
					constexpr float steps_width = 44.0f + 36.0f + 36.0f + 44.0f + space_xs * 3.0f;
					const float control_y = row_min.y + (row_height - control_height) * 0.5f;
					const float control_size = control_height;
					const float reset_icon_x = row_max.x - space_md - control_height;
					const float steps_x = reset_icon_x - space_md - steps_width;
					const float value_x = steps_x - space_md - value_width;
					const ImVec2 label_min = { row_min.x + space_md, row_min.y };
					const ImVec2 label_max = { label_min.x + label_width, row_max.y };
					const bool changed_from_default = openxr_ui::component_differs_from_default(entry, reference.component);
					// A value that differs from the default gets a "Changed" badge after its name, so the name leaves room for it.
					float badge_width = 0.0f;
					if (changed_from_default)
					{
						ImGui::PushFont(nullptr, vr_theme::font_small);
						badge_width = space_sm + 16.0f + std::ceil(ImGui::CalcTextSize("Changed").x);
						ImGui::PopFont();
					}
					const std::string fitted_label = fit_text(label, label_width - badge_width);

					char value_text[64];
					if (entry.type == parameter_type::boolean)
						std::snprintf(value_text, sizeof(value_text), "%s", entry.values[reference.component] != 0.0 ? "On" : "Off");
					else if (entry.type == parameter_type::floating_point)
						std::snprintf(value_text, sizeof(value_text), "%.4g", entry.values[reference.component]);
					else
						std::snprintf(value_text, sizeof(value_text), "%.0f", entry.values[reference.component]);
					const bool has_slider = entry.type != parameter_type::boolean && std::isfinite(entry.minimum) && std::isfinite(entry.maximum) && entry.minimum < entry.maximum;
					const float slider_fraction = has_slider ? static_cast<float>((entry.values[reference.component] - entry.minimum) / (entry.maximum - entry.minimum)) : 0.0f;
					const bool has_default = reference.component < entry.default_values.size();
					const float default_fraction = has_slider && has_default ?
						static_cast<float>((entry.default_values[reference.component] - entry.minimum) / (entry.maximum - entry.minimum)) : -1.0f;
					const float slider_x = label_max.x + space_md;
					ImGui::SetCursorScreenPos({ slider_x, control_y });
					const slider_interaction value_slider = draw_slider("slider", { value_x - space_md - slider_x, control_size }, slider_fraction, has_slider, default_fraction);
					if (input.select_clicked && value_slider.hovered && _implementation->dragging == implementation::drag_target::none)
					{
						_implementation->dragging = implementation::drag_target::value_slider;
						_implementation->release_frame_count = 0;
						_implementation->dragged_parameter = entry.id;
						_implementation->dragged_component = reference.component;
					}

					const ImVec2 value_size = ImGui::CalcTextSize(value_text);
					ImGui::GetWindowDrawList()->AddText({ value_x + value_width - value_size.x, std::floor(row_min.y + (row_height - ImGui::GetFontSize()) * 0.5f) },
						changed_from_default ? colors().changed_value : colors().text_secondary, value_text);

					bool coarse_decrease_hovered = false;
					bool decrease_hovered = false;
					bool increase_hovered = false;
					bool coarse_increase_hovered = false;
					ImGui::SetCursorScreenPos({ steps_x, control_y });
					if (entry.type == parameter_type::boolean)
					{
						// The switch sits at the right end of the step-button column and sends the same command Toggle did.
						ImGui::SetCursorScreenPos({ steps_x + steps_width - switch_track_width, control_y });
						increase_hovered = draw_switch("switch", entry.values[reference.component] != 0.0, { switch_track_width, control_size });
					}
					else
					{
						coarse_decrease_hovered = draw_text_button("minus10", "-10x", { 44.0f, control_size });
						ImGui::SetCursorScreenPos({ steps_x + 48.0f, control_y });
						decrease_hovered = draw_text_button("minus", "-", { 36.0f, control_size });
						ImGui::SetCursorScreenPos({ steps_x + 88.0f, control_y });
						increase_hovered = draw_text_button("plus", "+", { 36.0f, control_size });
						ImGui::SetCursorScreenPos({ steps_x + 128.0f, control_y });
						coarse_increase_hovered = draw_text_button("plus10", "+10x", { 44.0f, control_size });
					}
					ImGui::SetCursorScreenPos({ reset_icon_x, control_y });
					const bool can_reset = entry.resettable && changed_from_default;
					const bool reset_hovered = draw_reset_button("reset", { control_height, control_size }, can_reset);
					if (reset_hovered && ImGui::BeginTooltip())
					{
						char default_text[64] = "";
						if (has_default && entry.type == parameter_type::boolean)
							std::snprintf(default_text, sizeof(default_text), "%s", entry.default_values[reference.component] != 0.0 ? "On" : "Off");
						else if (has_default && entry.type == parameter_type::floating_point)
							std::snprintf(default_text, sizeof(default_text), "%.4g", entry.default_values[reference.component]);
						else if (has_default)
							std::snprintf(default_text, sizeof(default_text), "%.0f", entry.default_values[reference.component]);
						if (!entry.resettable || !has_default)
							ImGui::TextUnformatted("This setting cannot be reset");
						else
							ImGui::Text(changed_from_default ? "Reset to default: %s" : "Already at default: %s", default_text);
						ImGui::EndTooltip();
					}
					const bool dragging_value = _implementation->dragging == implementation::drag_target::value_slider &&
						_implementation->dragged_parameter == entry.id && _implementation->dragged_component == reference.component;
					if (result.action == render_result::command::none &&
						((input.select_clicked && _implementation->dragging == implementation::drag_target::none) ||
						(dragging_value && (input.select_clicked || input.select_down))))
					{
						if (value_slider.hovered || dragging_value)
						{
							double value = entry.minimum + (entry.maximum - entry.minimum) * value_slider.fraction;
							if (entry.type != parameter_type::floating_point)
								value = std::round(value);
							result.action = render_result::command::set_parameter;
							result.target_id = entry.id;
							result.component = reference.component;
							result.value = value;
						}
						else if (coarse_decrease_hovered || decrease_hovered || increase_hovered || coarse_increase_hovered)
						{
							result.action = render_result::command::set_parameter;
							result.target_id = entry.id;
							result.component = reference.component;
							result.value = entry.type == parameter_type::boolean ? (entry.values[reference.component] == 0.0 ? 1.0 : 0.0) :
								entry.values[reference.component] + (coarse_increase_hovered ? 10.0 * entry.step :
									increase_hovered ? entry.step : coarse_decrease_hovered ? -10.0 * entry.step : -entry.step);
						}
						else if (reset_hovered && can_reset)
						{
							result.action = render_result::command::reset_parameter;
							result.target_id = entry.id;
						}
					}
					const float name_y = std::floor(row_min.y + (row_height - ImGui::GetFontSize()) * 0.5f);
					ImGui::GetWindowDrawList()->AddText({ label_min.x, name_y },
						changed_from_default ? colors().changed_value : colors().text, fitted_label.c_str());
					if (changed_from_default)
					{
						ImGui::PushFont(nullptr, vr_theme::font_small);
						const float small_height = ImGui::GetFontSize();
						ImGui::PopFont();
						draw_status_dot_text(ImGui::GetWindowDrawList(),
							{ label_min.x + std::ceil(ImGui::CalcTextSize(fitted_label.c_str()).x) + space_sm, std::floor(row_min.y + (row_height - small_height) * 0.5f) },
							colors().changed_value, "Changed");
					}
					draw_hover_details(label_min, label_max, label, entry.tooltip.c_str());
				}
				ImGui::SetCursorScreenPos({ row_min.x, row_max.y + parameter_row_gap });
				ImGui::PopID();
				clear_control_prefix();
			}

			if (maximum_scroll > 0)
			{
			ImGui::SetCursorScreenPos({ list_min.x + row_width + space_sm, list_min.y });
			const float scroll_fraction = static_cast<float>(_implementation->parameter_scroll) / static_cast<float>(maximum_scroll);
			const slider_interaction scroll_slider = draw_vertical_slider("parameter_scroll", { scrollbar_width, list_height }, scroll_fraction, true);
			if (input.select_clicked && scroll_slider.hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->dragging = implementation::drag_target::parameter_scroll;
				_implementation->release_frame_count = 0;
				_implementation->drag_start_pointer = io.MousePos.y;
				_implementation->drag_start_fraction = scroll_fraction;
			}
			if (_implementation->dragging == implementation::drag_target::parameter_scroll && (input.select_clicked || input.select_down))
			{
				const float drag_fraction = std::clamp(_implementation->drag_start_fraction +
					(io.MousePos.y - _implementation->drag_start_pointer) / list_height * 0.9f, 0.0f, 1.0f);
				_implementation->parameter_scroll = static_cast<size_t>(std::round(drag_fraction * static_cast<float>(maximum_scroll)));
			}
			}

			// Bottom bar: the preset name and its unsaved state on the left, "Save preset" on the right.
			{
				ImDrawList *const bar_list = ImGui::GetWindowDrawList();
				const ImVec2 bar_min = { detail_header.x, bottom_bar_top };
				const ImVec2 bar_max = { right_edge, bottom_bar_top + bottom_bar_height };
				bar_list->AddRectFilled(bar_min, bar_max, colors().inset, vr_theme::radius_md);
				const float bar_button_y = bar_min.y + space_sm;
				ImGui::PushFont(nullptr, vr_theme::font_control);
				const float save_width = std::ceil(ImGui::CalcTextSize("Save preset").x) + 32.0f;
				ImGui::PopFont();
				const float save_x = bar_max.x - space_sm - save_width;
				const char *const save_label = input.preset_dirty ? "Save preset" : _implementation->save_feedback > 0 ? "Saved" : _implementation->save_feedback < 0 ? "Failed" : "Save preset";
				const button_tone save_tone = input.preset_dirty ? button_tone::primary :
					_implementation->save_feedback > 0 ? button_tone::success :
					_implementation->save_feedback < 0 ? button_tone::danger : button_tone::neutral;
				ImGui::SetCursorScreenPos({ save_x, bar_button_y });
				const bool save_hovered = draw_text_button("save", save_label, { save_width, control_height }, !input.shaders_temporarily_disabled, save_tone);
				if (input.select_clicked && save_hovered && result.action == render_result::command::none && _implementation->dragging == implementation::drag_target::none)
					result.action = render_result::command::save_preset;

				const char *const preset_name = input.preset_name.empty() ? "No preset" : input.preset_name.c_str();
				float text_x = bar_min.x + space_sm;
				ImGui::PushFont(nullptr, vr_theme::font_body);
				const float body_height = ImGui::GetFontSize();
				bar_list->AddText({ text_x, std::floor(bar_min.y + (bottom_bar_height - body_height) * 0.5f) }, colors().text_secondary, "Preset");
				text_x += std::ceil(ImGui::CalcTextSize("Preset").x) + space_sm;
				ImGui::PopFont();
				float dirty_width = 0.0f;
				ImGui::PushFont(nullptr, vr_theme::font_small);
				const float small_height = ImGui::GetFontSize();
				if (input.preset_dirty)
					dirty_width = 16.0f + std::ceil(ImGui::CalcTextSize("Unsaved changes").x);
				ImGui::PopFont();
				ImGui::PushFont(bold_font(), vr_theme::font_body);
				const float name_limit = save_x - space_md - text_x - (input.preset_dirty ? dirty_width + space_md : 0.0f);
				const std::string fitted_name = fit_text(preset_name, (std::max)(0.0f, name_limit));
				bar_list->AddText({ text_x, std::floor(bar_min.y + (bottom_bar_height - ImGui::GetFontSize()) * 0.5f) }, colors().text, fitted_name.c_str());
				text_x += std::ceil(ImGui::CalcTextSize(fitted_name.c_str()).x) + space_md;
				ImGui::PopFont();
				if (input.preset_dirty)
					draw_status_dot_text(bar_list, { text_x, std::floor(bar_min.y + (bottom_bar_height - small_height) * 0.5f) }, colors().warning, "Unsaved changes");
			}

			{
				char values_text[32];
				std::snprintf(values_text, sizeof(values_text), "%zu values", components.size());
				draw_api_version_line(ImGui::GetWindowDrawList(), values_text, graphics_api_label, display_height, right_edge);
			}
		}

		if (_implementation->save_feedback_frame_count != 0)
		{
			const char *const message = _implementation->save_feedback > 0 ? "Preset saved" : "Preset save failed";
			ImGui::PushFont(bold_font(), 0.0f);
			const ImVec2 text_size = ImGui::CalcTextSize(message);
			const ImVec2 minimum = { std::floor((io.DisplaySize.x - text_size.x) * 0.5f) - 22.0f, 54.0f };
			const ImVec2 maximum = { minimum.x + text_size.x + 44.0f, minimum.y + text_size.y + 20.0f };
			ImDrawList *const foreground = ImGui::GetForegroundDrawList();
			foreground->AddRectFilled({ minimum.x + 3.0f, minimum.y + 5.0f }, { maximum.x + 3.0f, maximum.y + 5.0f }, colors().shadow, vr_theme::radius_md);
			foreground->AddRectFilled(minimum, maximum, _implementation->save_feedback > 0 ? colors().success : colors().danger, vr_theme::radius_md);
			foreground->AddText({ minimum.x + 22.0f, minimum.y + 10.0f }, colors().text_on_color, message);
			ImGui::PopFont();
			if (--_implementation->save_feedback_frame_count == 0)
				_implementation->save_feedback = 0;
		}
		if (_implementation->save_as_feedback_frame_count != 0 && --_implementation->save_as_feedback_frame_count == 0)
			_implementation->save_as_feedback = 0;

		draw_open_dropdown(_implementation->dropdown, input.pointer_visible, pointer, display_height);
		if (input.pointer_visible)
			draw_crosshair(ImGui::GetForegroundDrawList(), pointer);
		ImGui::End();
		_implementation->finish_frame();
		return result;
	}

	render_result panel_renderer::render(ID3D11RenderTargetView *render_target, const technique_snapshot &snapshot, const render_input &input)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr || _implementation->api != implementation::graphics_api::d3d11 || render_target == nullptr)
			return {};

		ImGui::SetCurrentContext(_implementation->context);
		ImGui_ImplDX11_NewFrame();
		const render_result result = render_ui(snapshot, input);
		submit(render_target);
		return result;
	}

	render_result panel_renderer::render(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target,
		const technique_snapshot &snapshot, const render_input &input)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr || _implementation->api != implementation::graphics_api::d3d12 ||
			command_list == nullptr || render_target.ptr == 0)
			return {};

		ImGui::SetCurrentContext(_implementation->context);
		ImGui_ImplDX12_NewFrame();
		const render_result result = render_ui(snapshot, input);
		submit(command_list, render_target);
		return result;
	}

	render_result panel_renderer::render_opengl(uint32_t texture, const technique_snapshot &snapshot, const render_input &input)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr || _implementation->api != implementation::graphics_api::opengl || texture == 0 ||
			_implementation->gl.GetCurrentContext() != _implementation->opengl_context)
			return {};

		ImGui::SetCurrentContext(_implementation->context);
		ImGui_ImplOpenGL3_NewFrame();
		const render_result result = render_ui(snapshot, input);
		submit_opengl(texture);
		return result;
	}

	void panel_renderer::render_statistics(ID3D11RenderTargetView *render_target, const frame_statistics &statistics)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr || _implementation->api != implementation::graphics_api::d3d11 || render_target == nullptr)
			return;

		ImGui::SetCurrentContext(_implementation->context);
		ImGui_ImplDX11_NewFrame();
		render_statistics_ui(statistics);
		submit(render_target);
	}

	void panel_renderer::render_statistics(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target, const frame_statistics &statistics)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr || _implementation->api != implementation::graphics_api::d3d12 ||
			command_list == nullptr || render_target.ptr == 0)
			return;

		ImGui::SetCurrentContext(_implementation->context);
		ImGui_ImplDX12_NewFrame();
		render_statistics_ui(statistics);
		submit(command_list, render_target);
	}

	void panel_renderer::render_statistics_opengl(uint32_t texture, const frame_statistics &statistics)
	{
		const std::lock_guard<std::mutex> lock(s_imgui_mutex);
		if (_implementation->context == nullptr || _implementation->api != implementation::graphics_api::opengl || texture == 0 ||
			_implementation->gl.GetCurrentContext() != _implementation->opengl_context)
			return;

		ImGui::SetCurrentContext(_implementation->context);
		ImGui_ImplOpenGL3_NewFrame();
		render_statistics_ui(statistics);
		submit_opengl(texture);
	}

	void panel_renderer::render_statistics_ui(const frame_statistics &statistics)
	{
		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = { static_cast<float>(_implementation->width), static_cast<float>(_implementation->height) };
		io.DeltaTime = 1.0f / 90.0f;
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		_implementation->apply_selected_theme();
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 0.0f, 0.0f });
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("Frame statistics", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
		ImGui::SetWindowFontScale(statistics_font_scale);
		if (statistics.revision == 0)
			ImGui::TextUnformatted("-- FPS");
		else
			ImGui::Text("%.0f FPS   %.1f ms", statistics.average_fps, statistics.frame_time_ms);
		char graph_label[32];
		openxr_ui::format_frame_graph_label(statistics, graph_label);
		ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(graph_label).x);
		ImGui::TextDisabled("%s", graph_label);
		char cpu_label[48];
		openxr_ui::format_cpu_label(statistics, cpu_label);
		ImGui::TextUnformatted(cpu_label);
		openxr_ui::draw_frame_graph(statistics, ImGui::GetContentRegionAvail());
		ImGui::End();
		_implementation->finish_frame();
	}

	void panel_renderer::submit(ID3D11RenderTargetView *render_target)
	{
		ID3D11RenderTargetView *previous_render_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView *previous_depth_stencil = nullptr;
		_implementation->device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous_render_targets, &previous_depth_stencil);
		const float transparent[4] = {};
		_implementation->device_context->ClearRenderTargetView(render_target, transparent);
		_implementation->device_context->OMSetRenderTargets(1, &render_target, nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		_implementation->device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous_render_targets, previous_depth_stencil);
		for (ID3D11RenderTargetView *previous_render_target : previous_render_targets)
			if (previous_render_target != nullptr)
				previous_render_target->Release();
		if (previous_depth_stencil != nullptr)
			previous_depth_stencil->Release();
	}

	void panel_renderer::submit(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target)
	{
		const float transparent[4] = {};
		command_list->ClearRenderTargetView(render_target, transparent, 0, nullptr);
		command_list->OMSetRenderTargets(1, &render_target, FALSE, nullptr);
		ID3D12DescriptorHeap *const heaps[] = { _implementation->srv_descriptor_heap };
		command_list->SetDescriptorHeaps(1, heaps);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
	}

	// The Dear ImGui backend restores the state it changes; this restores the rest and the game's framebuffer.
	void panel_renderer::submit_opengl(uint32_t texture)
	{
		const opengl_functions &gl = _implementation->gl;
		int previous_framebuffer = 0;
		gl.GetIntegerv(gl_draw_framebuffer_binding, &previous_framebuffer);
		float previous_clear_color[4] = {};
		gl.GetFloatv(gl_color_clear_value, previous_clear_color);
		unsigned char previous_color_mask[4] = {};
		gl.GetBooleanv(gl_color_writemask, previous_color_mask);
		const bool scissor_enabled = gl.IsEnabled(gl_scissor_test) != 0;
		const bool srgb_enabled = gl.IsEnabled(gl_framebuffer_srgb) != 0;

		gl.BindFramebuffer(gl_draw_framebuffer, _implementation->opengl_framebuffer);
		gl.FramebufferTexture2D(gl_draw_framebuffer, gl_color_attachment0, gl_texture_2d, texture, 0);
		if (gl.CheckFramebufferStatus(gl_draw_framebuffer) == gl_framebuffer_complete)
		{
			gl.ColorMask(1, 1, 1, 1);
			gl.Disable(gl_scissor_test);
			gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			gl.Clear(gl_color_buffer_bit);
			// Matches the sRGB render target views of the Direct3D backends.
			gl.Enable(gl_framebuffer_srgb);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		}
		gl.FramebufferTexture2D(gl_draw_framebuffer, gl_color_attachment0, gl_texture_2d, 0, 0);

		srgb_enabled ? gl.Enable(gl_framebuffer_srgb) : gl.Disable(gl_framebuffer_srgb);
		scissor_enabled ? gl.Enable(gl_scissor_test) : gl.Disable(gl_scissor_test);
		gl.ColorMask(previous_color_mask[0], previous_color_mask[1], previous_color_mask[2], previous_color_mask[3]);
		gl.ClearColor(previous_clear_color[0], previous_clear_color[1], previous_clear_color[2], previous_clear_color[3]);
		gl.BindFramebuffer(gl_draw_framebuffer, static_cast<unsigned int>(previous_framebuffer));
	}
}
