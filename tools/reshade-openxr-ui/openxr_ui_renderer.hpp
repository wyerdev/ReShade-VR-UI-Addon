#pragma once

#include "openxr_ui_model.hpp"
#include "vr_frame_statistics.hpp"

#include <cstdint>
#include <memory>
#include <string>

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
		bool long_press_enabled = false;
		bool ui_starts_open = true;
		bool performance_mode = false;
		bool shaders_temporarily_disabled = false;
		bool preset_dirty = false;
		std::string preset_name;
		bool effect_runtime_available = true;
		bool always_show_statistics = false;
		uint32_t frame_rate_divisor = 1;
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
			toggle_long_press,
			set_ui_starts_open,
			set_performance_mode,
			set_always_show_statistics,
			set_frame_rate_divisor,
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
		render_result render(ID3D11RenderTargetView *render_target, const technique_snapshot &snapshot, const render_input &input);
		render_result render(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target,
			const technique_snapshot &snapshot, const render_input &input);
		void render_statistics(ID3D11RenderTargetView *render_target, const frame_statistics &statistics);
		void render_statistics(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target, const frame_statistics &statistics);
		void notify_save_result(bool succeeded);
		void notify_save_as_started(bool started);
		void notify_save_as_result(bool succeeded);

	private:
		bool initialize_context(uint32_t width, uint32_t height);
		render_result render_ui(const technique_snapshot &snapshot, const render_input &input);
		void render_statistics_ui(const frame_statistics &statistics);
		void submit(ID3D11RenderTargetView *render_target);
		void submit(ID3D12GraphicsCommandList *command_list, D3D12_CPU_DESCRIPTOR_HANDLE render_target);

		struct implementation;
		std::unique_ptr<implementation> _implementation;
	};
}