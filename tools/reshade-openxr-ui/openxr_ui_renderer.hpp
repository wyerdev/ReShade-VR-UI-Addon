#pragma once

#include "openxr_ui_model.hpp"
#include "vr_frame_statistics.hpp"
#include "vr_menu_shortcut.hpp"
#include "vr_preset_gesture.hpp"
#include "vr_ui_themes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D12CommandQueue;
struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct D3D12_CPU_DESCRIPTOR_HANDLE;

namespace openxr_ui
{
	inline constexpr uint32_t panel_width = 1024;
	inline constexpr uint32_t compact_panel_height = 650;
	inline constexpr uint32_t expanded_panel_height = 906;
	inline constexpr uint32_t statistics_width = 512;
	inline constexpr uint32_t statistics_height = 128;

	struct render_input
	{
		bool pointer_visible = false;
		float pointer_x = 0.0f;
		float pointer_y = 0.0f;
		bool select_clicked = false;
		bool expanded = false;
		bool select_down = false;
		bool select_released = false;
		float scroll_y = 0.0f;
		// Choices from vr_menu_shortcut.hpp.
		uint32_t menu_button = menu_button_both_sticks;
		bool ui_starts_open = true;
		bool performance_mode = false;
		bool shaders_temporarily_disabled = false;
		bool preset_dirty = false;
		std::string preset_name;
		bool effect_runtime_available = true;
		bool always_show_statistics = false;
		uint32_t frame_rate_divisor = 1;
		bool panel_locked = false;
		// Per-eye resolution the headset asks for; zero when unknown.
		uint32_t eye_width = 0;
		uint32_t eye_height = 0;
		// ReShade's Generic Depth add-on is turned off in the saved settings.
		bool depth_detection_disabled = false;
		bool depth_detection_restart_pending = false;
		bool performance_mode_restart_pending = false;
		// Effects ReShade skips because their subfolders are not in EffectSearchPaths.
		effect_subfolder_status effect_subfolders;
		// False only when no ReShade is loaded, so effects can never start.
		bool reshade_loaded = true;
		// How long the panel has waited for ReShade's effects to start.
		uint32_t effect_runtime_wait_seconds = 0;
		// The game started with performance mode on, so ReShade baked the effect values into the shaders.
		bool performance_mode_active = false;
		uint32_t menu_key = menu_key_insert;
		// Index into vr_theme::theme_names; also used by the FPS counter.
		uint32_t theme = vr_theme::default_theme;
		// The add-on runs its own effect runtime (UEVR games), which gets no depth buffer.
		bool depth_effects_unavailable = false;
		// Choice from vr_preset_gesture.hpp.
		uint32_t preset_gesture = preset_gesture_off;
	};

	struct render_result
	{
		enum class command
		{
			none,
			toggle_panel_size,
			hide_panel,
			move_active_to_top,
			move_technique,
			toggle_all_techniques,
			save_preset,
			previous_preset,
			next_preset,
			save_preset_as,
			delete_preset,
			toggle_technique,
			set_technique_hidden,
			set_technique_favourite,
			revert_technique,
			restore_technique,
			set_parameter,
			reset_parameter,
			reset_all_parameters,
			set_menu_button,
			set_menu_key,
			set_ui_starts_open,
			set_performance_mode,
			set_always_show_statistics,
			set_frame_rate_divisor,
			set_depth_detection_disabled,
			toggle_panel_lock,
			include_effect_subfolders,
			dismiss_effect_subfolder_notice,
			set_theme,
			set_preset_gesture,
		};

		command action = command::none;
		uint64_t target_id = 0;
		uint32_t component = 0;
		double value = 0.0;
		bool state = false;
		std::string text;
	};

	class panel_renderer
	{
	public:
		panel_renderer();
		~panel_renderer();

		panel_renderer(const panel_renderer &) = delete;
		panel_renderer &operator=(const panel_renderer &) = delete;

		bool initialize(ID3D11Device *device, ID3D11DeviceContext *context, uint32_t width, uint32_t height);
		bool initialize(ID3D12Device *device, ID3D12CommandQueue *queue, uint32_t frame_count, int64_t format, uint32_t width, uint32_t height);
		// OpenGL calls use the calling thread's current context, which must be the same context for every call and for destruction.
		bool initialize_opengl(uint32_t width, uint32_t height);
		render_result render(ID3D11RenderTargetView *render_target, const technique_snapshot &snapshot, const render_input &input);
		render_result render(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target,
			const technique_snapshot &snapshot, const render_input &input);
		render_result render_opengl(uint32_t texture, const technique_snapshot &snapshot, const render_input &input);
		void render_statistics(ID3D11RenderTargetView *render_target, const frame_statistics &statistics);
		void render_statistics(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target, const frame_statistics &statistics);
		void render_statistics_opengl(uint32_t texture, const frame_statistics &statistics);
		void notify_save_result(bool succeeded);
		void notify_save_as_started(bool started);
		void notify_save_as_result(bool succeeded);
		// True when the render target converts written colors to sRGB, as the usual R8G8B8A8_UNORM_SRGB or
		// GL_SRGB8_ALPHA8 swapchains do; the renderer then compensates so theme colors appear exactly.
		void set_srgb_target(bool srgb);
		// Selects the theme for every panel in the process, including the FPS counter.
		static void select_theme(uint32_t theme);

		enum class page
		{
			effects,
			effect_detail,
			save_as,
			settings,
		};
		// Opens a page directly, for the preview tool and tests. `technique_id` selects the effect for effect_detail.
		void show_page(page target, uint64_t technique_id = 0, const std::string &save_as_name = {});

		// A control drawn in the most recent frame, in panel pixels (the space of render_input::pointer_x/pointer_y).
		struct control_rect
		{
			std::string id;
			float min_x = 0.0f;
			float min_y = 0.0f;
			float max_x = 0.0f;
			float max_y = 0.0f;
		};
		// For the preview tool and tests. Off by default; the game never turns it on. When on, every frame replaces the recorded controls.
		void set_control_recording(bool enabled);
		// Centre of the control with that id in the most recently rendered frame; false when there is none.
		bool find_control(std::string_view id, float &center_x, float &center_y) const;
		const std::vector<control_rect> &recorded_controls() const;

	private:
		bool initialize_context(uint32_t width, uint32_t height);
		render_result render_ui(const technique_snapshot &snapshot, const render_input &input);
		void render_statistics_ui(const frame_statistics &statistics);
		void submit(ID3D11RenderTargetView *render_target);
		void submit(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target);
		void submit_opengl(uint32_t texture);

		struct implementation;
		std::unique_ptr<implementation> _implementation;
	};
}