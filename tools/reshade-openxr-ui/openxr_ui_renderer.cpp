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
			return { colors().surface, colors().text_disabled, 0 };
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

	bool draw_text_button(const char *id, const char *label, const ImVec2 size, bool enabled = true, button_tone tone = button_tone::neutral)
	{
		ImGui::BeginDisabled(!enabled);
		ImGui::InvisibleButton(id, size);
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
	bool draw_icon_button(const char *id, button_icon icon, const ImVec2 size, bool enabled = true)
	{
		ImGui::BeginDisabled(!enabled);
		ImGui::InvisibleButton(id, size);
		const bool hovered = enabled && ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		const button_colors look = button_colors_for(button_tone::neutral, hovered, enabled);
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

	// A rounded box that fills with the accent and shows a tick when on.
	void draw_check_indicator(ImDrawList *draw_list, ImVec2 box_min, float box_size, bool checked, bool hovered)
	{
		const ImVec2 box_max = { box_min.x + box_size, box_min.y + box_size };
		if (checked)
		{
			draw_list->AddRectFilled(box_min, box_max, hovered ? colors().accent_hover : colors().accent, vr_theme::radius_sm * 0.75f);
			const float unit = box_size / 20.0f;
			const ImVec2 tick[3] = { { box_min.x + 5.0f * unit, box_min.y + 10.5f * unit }, { box_min.x + 8.5f * unit, box_min.y + 14.0f * unit },
				{ box_min.x + 15.0f * unit, box_min.y + 6.0f * unit } };
			draw_list->AddPolyline(tick, 3, colors().text_on_color, ImDrawFlags_None, 2.5f);
		}
		else
		{
			draw_list->AddRectFilled(box_min, box_max, hovered ? colors().surface_hover : colors().surface_raised, vr_theme::radius_sm * 0.75f);
			draw_list->AddRect(box_min, box_max, colors().control_idle, vr_theme::radius_sm * 0.75f, 0, vr_theme::stroke);
		}
	}

	// Distance from a tick box's left edge to its label.
	constexpr float tick_box_label_x = 36.0f;

	bool draw_tick_box(const char *id, const char *label, bool checked, float width = 276.0f)
	{
		ImGui::InvisibleButton(id, { width, control_height });
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		ImDrawList *const draw_list = ImGui::GetWindowDrawList();
		draw_check_indicator(draw_list, { minimum.x + 6.0f, minimum.y + 6.0f }, 20.0f, checked, hovered);
		draw_list->AddText({ minimum.x + tick_box_label_x, std::floor(minimum.y + (control_height - ImGui::GetFontSize()) * 0.5f) },
			hovered ? colors().accent_hover : colors().text, label);
		return hovered;
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

	// Square icon button that switches between the compact and the taller panel; hovering names the action.
	bool draw_panel_size_button(bool expanded)
	{
		const bool hovered = draw_icon_button("expand", expanded ? button_icon::collapse : button_icon::expand, { control_height, control_height });
		draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), expanded ? "Collapse panel" : "Expand panel");
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
		ImVec2 box_min;
		ImVec2 box_max;
		std::vector<std::string> options;
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
			if (option >= 0)
			{
				state.chosen_id = state.open_id;
				state.chosen_option = static_cast<size_t>(option);
			}
			state.open_id = 0;
		}
		return true;
	}

	// A box showing `prefix` and the current choice with an arrow, so it reads as a list to pick from rather than a button.
	// It is sized to its longest choice, so the text is never cut short.
	dropdown_interaction draw_dropdown(dropdown_state &state, const char *id, const char *prefix, float height,
		const char *const *options, size_t option_count, size_t selected, bool clicked)
	{
		const ImGuiID item_id = ImGui::GetID(id);
		ImGui::PushFont(nullptr, vr_theme::font_control);
		float widest_option = 0.0f;
		for (size_t index = 0; index < option_count; ++index)
			widest_option = (std::max)(widest_option, ImGui::CalcTextSize(options[index]).x);
		constexpr float text_left = 10.0f;
		constexpr float text_gap = 12.0f;
		const float arrow_width = height;
		const ImVec2 size = { std::ceil(text_left + ImGui::CalcTextSize(prefix).x + widest_option + text_gap + arrow_width), height };
		const std::string label = std::string(prefix) + (selected < option_count ? options[selected] : "");

		ImGui::InvisibleButton(id, size);
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
			state.options.assign(options, options + option_count);
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
			if (static_cast<ptrdiff_t>(index) == hovered)
				draw_list->AddRectFilled({ row_min.x + 3.0f, row_min.y + 2.0f }, { row_max.x - 3.0f, row_max.y - 2.0f }, colors().surface_hover, vr_theme::radius_sm);
			if (index == state.selected)
				draw_list->AddRectFilled({ row_min.x + 6.0f, row_min.y + 7.0f }, { row_min.x + 10.0f, row_max.y - 7.0f }, colors().accent, 2.0f);
			draw_list->AddText({ row_min.x + 18.0f, std::floor(row_min.y + (row_height - ImGui::GetFontSize()) * 0.5f) },
				index == state.selected ? colors().text : colors().text_secondary, state.options[index].c_str());
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
		bool show_hidden = false;
		bool show_favourites_only = false;
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
		_implementation->technique_scroll = 0;
		_implementation->parameter_scroll = 0;
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
					const bool hovered = draw_text_button("key", label, { 84.0f, 42.0f });
					if (input.select_clicked && hovered && _implementation->save_as_name.size() < 120)
						_implementation->save_as_name.push_back(label[0]);
					ImGui::PopID();
				}
			};
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
			const bool cancel_hovered = draw_text_button("cancel_save_as", "Cancel", { 160.0f, 46.0f });
			const bool valid_name = !_implementation->save_as_name.empty() &&
				_implementation->save_as_name.back() != ' ' && _implementation->save_as_name.back() != '.';
			const char *const create_label = _implementation->save_as_feedback < 0 ? "Failed" : "Create";
			ImGui::SetCursorScreenPos({ io.DisplaySize.x - outer_padding - 160.0f, 430.0f });
			const bool create_hovered = draw_text_button("create_preset", create_label, { 160.0f, 46.0f }, valid_name,
				_implementation->save_as_feedback < 0 ? button_tone::danger : button_tone::primary);

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
			std::vector<const technique *> displayed_techniques;
			displayed_techniques.reserve(snapshot.techniques.size());
			for (const technique &entry : snapshot.techniques)
				if ((_implementation->show_hidden || !entry.hidden) && (!_implementation->show_favourites_only || entry.favourite))
					displayed_techniques.push_back(&entry);

			char page_text[128];
			char page_details[96];
			char version_suffix[24];
			{
				char version_label[16];
				openxr_ui::format_version_label(version_label);
				std::snprintf(version_suffix, sizeof(version_suffix), "  \xC2\xB7  %s", version_label);
			}
			std::snprintf(page_details, sizeof(page_details), "%zu shown  \xC2\xB7  %s", displayed_techniques.size(), graphics_api_label);
			std::snprintf(page_text, sizeof(page_text), "%s%s", page_details, version_suffix);
			const ImVec2 header_origin = ImGui::GetCursorScreenPos();
			const float right_edge = ImGui::GetWindowWidth() - outer_padding;
			ImDrawList *const header_draw_list = ImGui::GetWindowDrawList();
			ImGui::PushFont(bold_font(), vr_theme::font_title);
			const char *const title = "ReShade Techniques";
			header_draw_list->AddText({ header_origin.x, std::floor(header_origin.y + (control_height - ImGui::GetFontSize()) * 0.5f) },
				colors().text, title);
			const float subtitle_x = header_origin.x + ImGui::CalcTextSize(title).x + space_md;
			ImGui::PopFont();
			ImGui::SetCursorScreenPos({ right_edge - control_height, header_origin.y });
			const bool close_hovered = draw_icon_button("close", button_icon::close, { control_height, control_height });
			ImGui::SetCursorScreenPos({ right_edge - control_height * 2.0f - space_sm, header_origin.y });
			const bool expand_hovered = draw_panel_size_button(input.expanded);
			const float pin_x = right_edge - control_height * 2.0f - space_sm * 2.0f - 96.0f;
			ImGui::SetCursorScreenPos({ pin_x, header_origin.y });
			const bool pin_hovered = draw_text_button("pin", "Pin UI", { 96.0f, control_height }, true,
				input.panel_locked ? button_tone::toggled : button_tone::neutral);
			// The FPS counter is part of the panel's view, so its switch sits right next to Pin UI and Expand.
			// The box is as wide as its label so it groups tightly with them.
			const char *const statistics_label = "Always show FPS";
			const float statistics_box_width = std::ceil(tick_box_label_x + ImGui::CalcTextSize(statistics_label).x + space_sm);
			const float statistics_x = pin_x - space_sm - statistics_box_width;
			ImGui::SetCursorScreenPos({ statistics_x, header_origin.y });
			const bool statistics_hovered = draw_tick_box("always_show_statistics", statistics_label, input.always_show_statistics, statistics_box_width);
			{
				// The version is always shown in full; only the count and graphics details shorten when space runs out.
				ImGui::PushFont(nullptr, vr_theme::font_small);
				const float details_width = statistics_x - space_md - subtitle_x - ImGui::CalcTextSize(version_suffix).x;
				const std::string fitted_page_text = fit_text(page_details, details_width) + version_suffix;
				header_draw_list->AddText({ subtitle_x, std::floor(header_origin.y + (control_height - ImGui::GetFontSize()) * 0.5f) + 1.0f },
					colors().text_secondary, fitted_page_text.c_str());
				ImGui::PopFont();
				draw_hover_details({ subtitle_x, header_origin.y }, { statistics_x - space_md, header_origin.y + control_height }, page_text);
			}

			const float filter_y = header_origin.y + header_row_step;
			ImGui::SetCursorScreenPos({ header_origin.x, filter_y });
			const bool active_first_hovered = draw_text_button("active_first", "Active first", { 144.0f, control_height });
			ImGui::SetCursorScreenPos({ header_origin.x + 152.0f, filter_y });
			const bool favourites_only_hovered = draw_text_button("favourites_only", "Starred only", { 144.0f, control_height }, true,
				_implementation->show_favourites_only ? button_tone::toggled : button_tone::neutral);
			ImGui::SetCursorScreenPos({ header_origin.x + 304.0f, filter_y });
			const bool show_hidden_hovered = draw_text_button("show_hidden", _implementation->show_hidden ? "Hide hidden" : "Show hidden", { 144.0f, control_height }, true,
				_implementation->show_hidden ? button_tone::toggled : button_tone::neutral);
			ImGui::SetCursorScreenPos({ right_edge - 176.0f, filter_y });
			const bool disable_all_hovered = draw_text_button("disable_all",
				input.shaders_temporarily_disabled ? "Restore shaders" : "Disable all", { 176.0f, control_height }, true,
				input.shaders_temporarily_disabled ? button_tone::warning : button_tone::neutral);

			const float preset_y = header_origin.y + header_row_step * 2.0f;
			const float preset_controls_x = right_edge - 408.0f;
			{
				const float text_y = std::floor(preset_y + (control_height - ImGui::GetFontSize()) * 0.5f);
				header_draw_list->AddText({ header_origin.x, text_y }, colors().text_secondary, "Preset");
				const float name_x = header_origin.x + ImGui::CalcTextSize("Preset").x + space_sm + space_xs;
				ImGui::PushFont(bold_font(), 0.0f);
				const std::string preset_label = fit_text(input.preset_name.empty() ? "No preset" : input.preset_name.c_str(),
					preset_controls_x - space_md - name_x);
				header_draw_list->AddText({ name_x, text_y }, colors().text, preset_label.c_str());
				ImGui::PopFont();
			}
			ImGui::SetCursorScreenPos({ preset_controls_x, preset_y });
			const bool previous_preset_hovered = draw_icon_button("previous_preset", button_icon::previous, { control_height, control_height }, !input.shaders_temporarily_disabled);
			ImGui::SetCursorScreenPos({ preset_controls_x + 40.0f, preset_y });
			const bool next_preset_hovered = draw_icon_button("next_preset", button_icon::next, { control_height, control_height }, !input.shaders_temporarily_disabled);
			const char *const save_label = input.preset_dirty ? "Save" : _implementation->save_feedback > 0 ? "Saved" : _implementation->save_feedback < 0 ? "Failed" : "Save";
			const button_tone save_tone = input.preset_dirty ? button_tone::primary :
				_implementation->save_feedback > 0 ? button_tone::success :
				_implementation->save_feedback < 0 ? button_tone::danger : button_tone::neutral;
			ImGui::SetCursorScreenPos({ preset_controls_x + 80.0f, preset_y });
			const bool save_hovered = draw_text_button("save", save_label, { 88.0f, control_height }, !input.shaders_temporarily_disabled, save_tone);
			const char *const save_as_label = _implementation->save_as_feedback == 2 ? "Creating..." :
				_implementation->save_as_feedback > 0 ? "Created" : _implementation->save_as_feedback < 0 ? "Failed" : "Save As";
			const button_tone save_as_tone = _implementation->save_as_feedback == 2 ? button_tone::warning :
				_implementation->save_as_feedback > 0 ? button_tone::success :
				_implementation->save_as_feedback < 0 ? button_tone::danger : button_tone::neutral;
			ImGui::SetCursorScreenPos({ preset_controls_x + 176.0f, preset_y });
			const bool save_as_hovered = draw_text_button("save_as", save_as_label, { 128.0f, control_height },
				!input.shaders_temporarily_disabled && _implementation->save_as_feedback != 2, save_as_tone);
			ImGui::SetCursorScreenPos({ preset_controls_x + 312.0f, preset_y });
			const bool delete_preset_hovered = draw_text_button("delete_preset", "Delete", { 96.0f, control_height },
				!input.shaders_temporarily_disabled && _implementation->save_as_feedback != 2, button_tone::destructive);
			const float divider_y = preset_y + control_height + space_sm;
			header_draw_list->AddRectFilled({ header_origin.x, divider_y - 1.0f }, { right_edge, divider_y + 1.0f }, colors().surface);
			ImGui::SetCursorScreenPos({ header_origin.x, divider_y + space_sm });
			if (input.select_clicked && close_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::hide_panel;
			else if (input.select_clicked && expand_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::toggle_panel_size;
			else if (input.select_clicked && pin_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::toggle_panel_lock;
			else if (input.select_clicked && active_first_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::move_active_to_top;
				_implementation->technique_scroll = 0;
			}
			else if (input.select_clicked && favourites_only_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->show_favourites_only = !_implementation->show_favourites_only;
				_implementation->technique_scroll = 0;
			}
			else if (input.select_clicked && show_hidden_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->show_hidden = !_implementation->show_hidden;
				_implementation->technique_scroll = 0;
			}
			if (input.effect_subfolders.state != effect_subfolder_state::none)
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

				const bool can_click = input.select_clicked && _implementation->dragging == implementation::drag_target::none;
				if (can_click && primary_hovered)
					result.action = render_result::command::include_effect_subfolders;
				else if (can_click && dismiss_hovered)
					result.action = render_result::command::dismiss_effect_subfolder_notice;
				ImGui::SetCursorScreenPos({ notice_min.x, notice_max.y + space_sm });
			}
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
			else if (snapshot.techniques.empty() && (input.effect_subfolders.state == effect_subfolder_state::saving ||
				input.effect_subfolders.state == effect_subfolder_state::loading))
			{
				ImGui::TextColored(vr_theme::to_float4(colors().accent), "ReShade is loading effects");
				ImGui::TextWrapped("The list and all effects come back when loading finishes.");
				ImGui::Separator();
			}
			else if (snapshot.techniques.empty())
			{
				ImGui::TextColored(vr_theme::to_float4(colors().warning), "No ReShade techniques found");
				ImGui::TextWrapped("Check EffectSearchPaths in ReShadeVR.ini or ReShade.ini.");
				ImGui::Separator();
			}

			// As many rows of at least list_row_height as fit between the header and the settings below.
			const ImVec2 list_min = ImGui::GetCursorScreenPos();
			const float list_height = display_height - outer_padding - main_footer_height - space_md - list_min.y;
			const size_t visible_techniques = std::max<size_t>(1, static_cast<size_t>((list_height + list_row_gap) / (list_row_height + list_row_gap)));
			const float row_height = std::floor((list_height - list_row_gap * static_cast<float>(visible_techniques - 1)) / static_cast<float>(visible_techniques));
			const size_t maximum_scroll = displayed_techniques.size() > visible_techniques ? displayed_techniques.size() - visible_techniques : 0;
			_implementation->technique_scroll = (std::min)(_implementation->technique_scroll, maximum_scroll);
			_implementation->update_joystick_scroll(input.scroll_y, _implementation->technique_scroll, maximum_scroll);
			const float row_width = ImGui::GetContentRegionAvail().x - scrollbar_width - space_sm;
			const size_t first = _implementation->technique_scroll;
			const size_t last = (std::min)(first + visible_techniques, displayed_techniques.size());
			for (size_t index = first; index < first + visible_techniques; ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				const ImVec2 size = { row_width, row_height };
				ImGui::InvisibleButton("technique", size);
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
					ImGui::InvisibleButton("toggle", { 40.0f, row_height - 4.0f });
					const bool toggle_hovered = ImGui::IsItemHovered();
					draw_check_indicator(draw_list, { minimum.x + 12.0f, std::floor(minimum.y + (row_height - 18.0f) * 0.5f) }, 18.0f, entry.enabled, toggle_hovered);
					const ImVec2 text_min = { minimum.x + 50.0f, std::floor(minimum.y + (row_height - ImGui::GetFontSize()) * 0.5f) };
					char label[320];
					std::snprintf(label, sizeof(label), "%s  [%s]", entry.name.c_str(), entry.effect_name.c_str());
					const ImVec2 text_max = { maximum.x - 344.0f, maximum.y };
					// The technique name leads; the effect file follows in smaller, quieter text when there is room.
					const std::string fitted_name = fit_text(entry.name.c_str(), text_max.x - text_min.x);
					const ImU32 name_color = entry.has_non_default_values ? colors().changed_value : entry.hidden ? colors().text_secondary : colors().text;
					draw_list->AddText(text_min, name_color, fitted_name.c_str());
					const float effect_x = text_min.x + ImGui::CalcTextSize(fitted_name.c_str()).x + 12.0f;
					if (effect_x + 40.0f < text_max.x)
					{
						ImGui::PushFont(nullptr, vr_theme::font_small);
						const std::string fitted_effect = fit_text(entry.effect_name.c_str(), text_max.x - effect_x);
						draw_list->AddText({ effect_x, std::floor(minimum.y + (row_height - ImGui::GetFontSize()) * 0.5f) + 1.0f },
							colors().text_disabled, fitted_effect.c_str());
						ImGui::PopFont();
					}
					draw_hover_details(text_min, text_max, label, entry.tooltip.c_str());
					const auto order_it = std::find_if(snapshot.techniques.begin(), snapshot.techniques.end(), [&entry](const technique &candidate) {
						return candidate.id == entry.id;
					});
					const bool can_move_up = order_it != snapshot.techniques.end() && order_it != snapshot.techniques.begin();
					const bool can_move_down = order_it != snapshot.techniques.end() && order_it + 1 != snapshot.techniques.end();
					// Row buttons, right to left: Open, Hide, star, down, up; 8 px apart and 4 px inside the row.
					const float button_y = minimum.y + space_xs;
					const float button_height = row_height - space_xs * 2.0f;
					ImGui::SetCursorScreenPos({ maximum.x - 332.0f, button_y });
					const bool move_up_hovered = draw_icon_button("move_up", button_icon::up, { 32.0f, button_height }, !input.shaders_temporarily_disabled && can_move_up);
					ImGui::SetCursorScreenPos({ maximum.x - 292.0f, button_y });
					const bool move_down_hovered = draw_icon_button("move_down", button_icon::down, { 32.0f, button_height }, !input.shaders_temporarily_disabled && can_move_down);
					const bool move_controls_hovered = ImGui::IsMouseHoveringRect({ maximum.x - 332.0f, button_y }, { maximum.x - 260.0f, button_y + button_height });
					ImGui::SetCursorScreenPos({ maximum.x - 252.0f, button_y });
					const bool favourite_hovered = draw_star_button("favourite", { 40.0f, button_height }, entry.favourite);
					ImGui::SetCursorScreenPos({ maximum.x - 204.0f, button_y });
					const bool hide_hovered = draw_text_button("hide", entry.hidden ? "Unhide" : "Hide", { 96.0f, button_height });
					ImGui::SetCursorScreenPos({ maximum.x - 100.0f, button_y });
					const bool open_hovered = draw_text_button("open", "Open", { 96.0f, button_height }, !input.shaders_temporarily_disabled);
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
			}

			const ImVec2 footer_min = { list_min.x, list_min.y + list_height + space_md };
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

			// The left column holds UI behaviour, the right column game-start settings; the gap keeps the two apart.
			const float settings_column_x = footer_min.x + 488.0f;
			const float footer_row_2 = footer_min.y + header_row_step;
			const float footer_row_3 = footer_min.y + header_row_step * 2.0f;
			ImGui::SetCursorScreenPos(footer_min);
			const dropdown_interaction theme_choice = draw_dropdown(_implementation->dropdown, "theme", "Theme: ", control_height,
				vr_theme::theme_names, vr_theme::theme_count, input.theme,
				input.select_clicked && _implementation->dragging == implementation::drag_target::none);
			ImGui::SetCursorScreenPos({ settings_column_x, footer_min.y });
			const char *frame_rate_options[max_frame_rate_divisor];
			for (uint32_t divisor = 1; divisor <= max_frame_rate_divisor; ++divisor)
				frame_rate_options[divisor - 1] = frame_rate_divisor_name(divisor);
			const dropdown_interaction frame_rate = draw_dropdown(_implementation->dropdown, "frame_rate_divisor", "FPS target: ", control_height,
				frame_rate_options, max_frame_rate_divisor, input.frame_rate_divisor - 1,
				input.select_clicked && _implementation->dragging == implementation::drag_target::none);

			// Both settings are read by ReShade only at game start, so they share one restart note.
			const std::string restart_first = "Restart the game to apply your change.\n\n";
			ImGui::SetCursorScreenPos({ settings_column_x, footer_row_2 });
			const char *const performance_label = "Performance mode (on = faster shaders)";
			const bool performance_hovered = draw_tick_box("performance_mode", performance_label, input.performance_mode, 520.0f);
			draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), performance_label,
				((input.performance_mode_restart_pending ? restart_first : std::string()) +
				"On: ReShade builds the effects with their current settings fixed, so they run faster. "
				"How much this helps depends on how many and how heavy the shaders you use are. "
				"Shader settings can then not be changed; shaders can still be switched on and off. "
				"Applies after a game restart.").c_str());
			ImGui::SetCursorScreenPos({ settings_column_x, footer_row_3 });
			const char *const depth_label = "Depth buffer (off = faster in OpenGL)";
			const bool depth_hovered = draw_tick_box("depth_buffer", depth_label, !input.depth_detection_disabled, 520.0f);
			draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), depth_label,
				((input.depth_detection_restart_pending ? restart_first : std::string()) +
				"On: ReShade gives the game's depth buffer to shaders (ReShade's Generic Depth helper), so shaders that need depth, "
				"such as MXAO, depth of field and depth fog, work. Off: those shaders stop working; the game itself is not affected. "
				"In SOMA-VR (OpenGL) turning it off gave about 30 FPS more. In DirectX games ReShade costs only a few FPS in total, "
				"so turning this off makes little difference there. Applies after a game restart.").c_str());
			const bool restart_pending = input.performance_mode_restart_pending || input.depth_detection_restart_pending;
			ImGui::PushFont(nullptr, vr_theme::font_small);
			ImGui::GetWindowDrawList()->AddText({ settings_column_x + 36.0f, footer_row_3 + header_row_step },
				restart_pending ? colors().changed_value : colors().text_secondary,
				restart_pending ? "Restart the game to apply your change." : "These two apply after a game restart.");
			ImGui::PopFont();

			ImGui::SetCursorScreenPos({ footer_min.x, footer_row_2 });
			const bool starts_open_hovered = draw_tick_box("starts_open", "UI starts open", input.ui_starts_open, 220.0f);
			// The game still sees every button and key press, so each choice may also do something in the game or in a mod such as UEVR.
			const char *const shortcut_details =
				"Shows or hides this menu. The game also receives the press.\n\n"
				"Controller: L3+R3 presses both thumbsticks in. B+Y presses the two upper face buttons together. "
				"The Hold choices, including Hold Menu (the left controller's menu button), need 1.5 seconds.\n"
				"Key: works while the game window is in front.\n\n"
				"UEVR opens its own menu with L3+R3 and Insert, so in UEVR games pick a hold or B+Y, and End or Page Up. "
				"Controller and key cannot both be Off.";
			const bool interaction_free = input.select_clicked && _implementation->dragging == implementation::drag_target::none;
			const ImVec2 shortcut_row = { footer_min.x, footer_row_3 };
			const char *const shortcut_label = "Open UI";
			// Lines up with the tick boxes above; the two lists share their height and baseline with the FPS target list.
			ImGui::GetWindowDrawList()->AddText({ shortcut_row.x + 7.0f, std::floor(shortcut_row.y + (control_height - ImGui::GetFontSize()) * 0.5f) },
				colors().text, shortcut_label);
			ImGui::SetCursorScreenPos({ shortcut_row.x + 7.0f + ImGui::CalcTextSize(shortcut_label).x + space_md, shortcut_row.y });
			const dropdown_interaction button_choice = draw_dropdown(_implementation->dropdown, "menu_button", "", control_height,
				menu_button_names, menu_button_count, input.menu_button, interaction_free);
			draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "Open UI: controller buttons", shortcut_details);
			ImGui::SetCursorScreenPos({ ImGui::GetItemRectMax().x + space_sm, shortcut_row.y });
			const dropdown_interaction key_choice = draw_dropdown(_implementation->dropdown, "menu_key", "", control_height,
				menu_key_names, menu_key_count, input.menu_key, interaction_free);
			draw_hover_details(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), "Open UI: keyboard key", shortcut_details);

			if (input.select_clicked && previous_preset_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::previous_preset;
			else if (input.select_clicked && next_preset_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::next_preset;
			else if (input.select_clicked && save_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::save_preset;
			else if (input.select_clicked && save_as_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->save_as_dialog_open = true;
				_implementation->save_as_name = input.preset_name.empty() ? "New Preset" : input.preset_name + " Copy";
				_implementation->save_as_feedback = 0;
				_implementation->save_as_feedback_frame_count = 0;
			}
			else if (input.select_clicked && delete_preset_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::delete_preset;
			else if (input.select_clicked && disable_all_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::toggle_all_techniques;
			else if (button_choice.chosen && button_choice.option != input.menu_button &&
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
		else
		{
			const technique &selected = *selected_it;
			const ImVec2 detail_header = ImGui::GetCursorScreenPos();
			const float right_edge = ImGui::GetWindowWidth() - outer_padding;
			const bool back_hovered = draw_text_button("back", "Back", { 80.0f, control_height });
			char selected_label[320];
			std::snprintf(selected_label, sizeof(selected_label), "%s  [%s]", selected.name.c_str(), selected.effect_name.c_str());
			const float controls_x = right_edge - 440.0f;
			const float title_x = detail_header.x + 80.0f + space_md;
			const float selected_text_width = controls_x - title_x - 8.0f;
			{
				ImDrawList *const draw_list = ImGui::GetWindowDrawList();
				ImGui::PushFont(bold_font(), 0.0f);
				const std::string fitted_name = fit_text(selected.name.c_str(), selected_text_width);
				const float title_y = std::floor(detail_header.y + (control_height - ImGui::GetFontSize()) * 0.5f);
				draw_list->AddText({ title_x, title_y }, colors().text, fitted_name.c_str());
				const float effect_x = title_x + ImGui::CalcTextSize(fitted_name.c_str()).x + space_sm + space_xs;
				ImGui::PopFont();
				if (effect_x + 40.0f < title_x + selected_text_width)
				{
					ImGui::PushFont(nullptr, vr_theme::font_small);
					const std::string fitted_effect = fit_text(selected.effect_name.c_str(), title_x + selected_text_width - effect_x);
					draw_list->AddText({ effect_x, std::floor(detail_header.y + (control_height - ImGui::GetFontSize()) * 0.5f) + 1.0f },
						colors().text_disabled, fitted_effect.c_str());
					ImGui::PopFont();
				}
			}
			draw_hover_details({ title_x, detail_header.y }, { title_x + selected_text_width, detail_header.y + control_height }, selected_label, selected.tooltip.c_str());
			ImGui::SetCursorScreenPos({ controls_x, detail_header.y });
			const bool toggle_hovered = draw_text_button("toggle", selected.enabled ? "Disable" : "Enable", { 104.0f, control_height });
			ImGui::SetCursorScreenPos({ controls_x + 112.0f, detail_header.y });
			const bool reset_all_hovered = draw_text_button("reset_all", "Reset All", { 120.0f, control_height });
			ImGui::SetCursorScreenPos({ controls_x + 240.0f, detail_header.y });
			const bool revert_restore_hovered = draw_text_button("revert_restore", selected.has_restore_state ? "Restore" : "Revert", { 104.0f, control_height },
				(selected.has_restore_state || selected.differs_from_saved) && !input.shaders_temporarily_disabled);
			ImGui::SetCursorScreenPos({ controls_x + 352.0f, detail_header.y });
			const char *const save_label = input.preset_dirty ? "Save" : _implementation->save_feedback > 0 ? "Saved" : _implementation->save_feedback < 0 ? "Failed" : "Save";
			const button_tone save_tone = input.preset_dirty ? button_tone::primary :
				_implementation->save_feedback > 0 ? button_tone::success :
				_implementation->save_feedback < 0 ? button_tone::danger : button_tone::neutral;
			const bool save_hovered = draw_text_button("save", save_label, { 88.0f, control_height }, true, save_tone);
			if (input.select_clicked && back_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				_implementation->selected_technique = 0;
				_implementation->parameter_scroll = 0;
			}
			else if (input.select_clicked && toggle_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::toggle_technique;
				result.target_id = selected.id;
			}
			else if (input.select_clicked && reset_all_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = render_result::command::reset_all_parameters;
				result.target_id = selected.id;
			}
			else if (input.select_clicked && revert_restore_hovered && _implementation->dragging == implementation::drag_target::none)
			{
				result.action = selected.has_restore_state ? render_result::command::restore_technique : render_result::command::revert_technique;
				result.target_id = selected.id;
			}
			else if (input.select_clicked && save_hovered && _implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::save_preset;

			// One line each for the effect description and the performance-mode note; the full text shows on hover.
			float content_y = detail_header.y + control_height + space_md;
			const auto draw_note_line = [&](const char *text, ImU32 color) {
				ImGui::PushFont(nullptr, vr_theme::font_small);
				const std::string fitted = fit_text(text, right_edge - detail_header.x);
				ImGui::GetWindowDrawList()->AddText({ detail_header.x, content_y }, color, fitted.c_str());
				ImGui::PopFont();
				draw_hover_details({ detail_header.x, content_y }, { right_edge, content_y + note_height }, text);
				content_y += note_height + space_sm;
			};
			if (!selected.tooltip.empty())
				draw_note_line(selected.tooltip.c_str(), colors().text_secondary);
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
			const float list_height = display_height - outer_padding - detail_footer_height - space_md - list_min.y;
			const size_t visible_parameters = std::max<size_t>(1, static_cast<size_t>((list_height + list_row_gap) / (list_row_height + list_row_gap)));
			const float row_height = std::floor((list_height - list_row_gap * static_cast<float>(visible_parameters - 1)) / static_cast<float>(visible_parameters));
			const size_t maximum_scroll = components.size() > visible_parameters ? components.size() - visible_parameters : 0;
			_implementation->parameter_scroll = (std::min)(_implementation->parameter_scroll, maximum_scroll);
			_implementation->update_joystick_scroll(input.scroll_y, _implementation->parameter_scroll, maximum_scroll);
			const float row_width = ImGui::GetContentRegionAvail().x - scrollbar_width - space_sm;
			const size_t first = _implementation->parameter_scroll;
			const size_t last = (std::min)(first + visible_parameters, components.size());
			for (size_t index = first; index < first + visible_parameters; ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				const ImVec2 row_min = ImGui::GetCursorScreenPos();
				const ImVec2 row_max = { row_min.x + row_width, row_min.y + row_height };
				if (index < last)
					ImGui::GetWindowDrawList()->AddRectFilled(row_min, row_max, colors().surface, vr_theme::radius_md);
				if (index < last)
				{
					const component_reference &reference = components[index];
					const parameter &entry = *reference.entry;
					char label[256];
					if (entry.values.size() == 1)
						std::snprintf(label, sizeof(label), "%s", entry.name.c_str());
					else
						std::snprintf(label, sizeof(label), "%s [%u]", entry.name.c_str(), reference.component + 1);
					// One line per value: name, slider, value, step buttons, reset. Columns are fixed so every row lines up.
					constexpr float label_width = 232.0f;
					const float control_y = row_min.y + space_xs;
					const float control_size = row_height - space_xs * 2.0f;
					const ImVec2 label_min = { row_min.x + space_md, row_min.y };
					const ImVec2 label_max = { label_min.x + label_width, row_max.y };
					const std::string fitted_label = fit_text(label, label_max.x - label_min.x);
					const bool changed_from_default = openxr_ui::component_differs_from_default(entry, reference.component);

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
					const slider_interaction value_slider = draw_slider("value_slider", { row_max.x - 352.0f - slider_x, control_size }, slider_fraction, has_slider, default_fraction);
					if (input.select_clicked && value_slider.hovered && _implementation->dragging == implementation::drag_target::none)
					{
						_implementation->dragging = implementation::drag_target::value_slider;
						_implementation->release_frame_count = 0;
						_implementation->dragged_parameter = entry.id;
						_implementation->dragged_component = reference.component;
					}

					const ImVec2 value_size = ImGui::CalcTextSize(value_text);
					ImGui::GetWindowDrawList()->AddText({ row_max.x - 280.0f - value_size.x, std::floor(control_y + (control_size - ImGui::GetFontSize()) * 0.5f) },
						changed_from_default ? colors().changed_value : colors().text_secondary, value_text);

					bool coarse_decrease_hovered = false;
					bool decrease_hovered = false;
					bool increase_hovered = false;
					bool coarse_increase_hovered = false;
					ImGui::SetCursorScreenPos({ row_max.x - 268.0f, control_y });
					if (entry.type == parameter_type::boolean)
						increase_hovered = draw_text_button("change", "Toggle", { 104.0f, control_size });
					else
					{
						coarse_decrease_hovered = draw_text_button("coarse_decrease", "-10x", { 56.0f, control_size });
						ImGui::SetCursorScreenPos({ row_max.x - 204.0f, control_y });
						decrease_hovered = draw_text_button("decrease", "-", { 40.0f, control_size });
						ImGui::SetCursorScreenPos({ row_max.x - 156.0f, control_y });
						increase_hovered = draw_text_button("increase", "+", { 40.0f, control_size });
						ImGui::SetCursorScreenPos({ row_max.x - 108.0f, control_y });
						coarse_increase_hovered = draw_text_button("coarse_increase", "+10x", { 56.0f, control_size });
					}
					ImGui::SetCursorScreenPos({ row_max.x - 44.0f, control_y });
					const bool can_reset = entry.resettable && changed_from_default;
					const bool reset_hovered = draw_reset_button("reset", { 40.0f, control_size }, can_reset);
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
					ImGui::GetWindowDrawList()->AddText({ label_min.x, std::floor(row_min.y + (row_height - ImGui::GetFontSize()) * 0.5f) },
						changed_from_default ? colors().changed_value : colors().text, fitted_label.c_str());
					draw_hover_details(label_min, label_max, label, entry.tooltip.c_str());
				}
				ImGui::SetCursorScreenPos({ row_min.x, row_max.y + list_row_gap });
				ImGui::PopID();
			}

			const ImVec2 footer_min = { list_min.x, list_min.y + list_height + space_md };
			ImGui::SetCursorScreenPos({ list_min.x + row_width + space_sm, list_min.y });
			const float scroll_fraction = maximum_scroll > 0 ? static_cast<float>(_implementation->parameter_scroll) / static_cast<float>(maximum_scroll) : 0.0f;
			const slider_interaction scroll_slider = draw_vertical_slider("parameter_scroll", { scrollbar_width, list_height }, scroll_fraction, maximum_scroll > 0);
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

			{
				char footer_text[96];
				std::snprintf(footer_text, sizeof(footer_text), "%zu values  \xC2\xB7  %s", components.size(), graphics_api_label);
				ImGui::PushFont(nullptr, vr_theme::font_small);
				ImGui::GetWindowDrawList()->AddText({ footer_min.x, std::floor(footer_min.y + (control_height - ImGui::GetFontSize()) * 0.5f) },
					colors().text_secondary, footer_text);
				ImGui::PopFont();
			}
			ImGui::SetCursorScreenPos({ right_edge - control_height - space_sm - 96.0f, footer_min.y });
			const bool pin_hovered = draw_text_button("pin", "Pin UI", { 96.0f, control_height }, true,
				input.panel_locked ? button_tone::toggled : button_tone::neutral);
			ImGui::SetCursorScreenPos({ right_edge - control_height, footer_min.y });
			const bool expand_hovered = draw_panel_size_button(input.expanded);
			if (result.action == render_result::command::none && input.select_clicked && expand_hovered &&
				_implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::toggle_panel_size;
			else if (result.action == render_result::command::none && input.select_clicked && pin_hovered &&
				_implementation->dragging == implementation::drag_target::none)
				result.action = render_result::command::toggle_panel_lock;
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
