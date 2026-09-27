#include "openxr_ui_renderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdio>
#include <vector>

namespace
{
	using Microsoft::WRL::ComPtr;

	bool expect_command(openxr_ui::panel_renderer &renderer, ID3D11RenderTargetView *render_target,
		const openxr_ui::technique_snapshot &snapshot, float x, float y,
		openxr_ui::render_result::command expected_action, uint64_t expected_id, uint32_t expected_component = 0,
		double expected_value = 0.0, bool expanded = false)
	{
		const openxr_ui::render_input input = { true, x, y, true, expanded };
		const openxr_ui::render_result result = renderer.render(render_target, snapshot, input);
		if (result.action == expected_action && result.target_id == expected_id && result.component == expected_component &&
			(expected_action != openxr_ui::render_result::command::set_parameter || std::abs(result.value - expected_value) < 0.0001))
			return true;
		std::fprintf(stderr, "Expected action %u on %llu with value %.6f, got action %u on %llu with value %.6f.\n",
			static_cast<unsigned>(expected_action),
			static_cast<unsigned long long>(expected_id),
			expected_value,
			static_cast<unsigned>(result.action),
			static_cast<unsigned long long>(result.target_id),
			result.value);
		return false;
	}

}

int main()
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &device, nullptr, &context)))
	{
		std::fputs("Failed to create the D3D11 WARP device.\n", stderr);
		return 1;
	}

	D3D11_TEXTURE2D_DESC texture_desc = {};
	texture_desc.Width = openxr_ui::panel_width;
	texture_desc.Height = openxr_ui::expanded_panel_height;
	texture_desc.MipLevels = 1;
	texture_desc.ArraySize = 1;
	texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texture_desc.SampleDesc.Count = 1;
	texture_desc.Usage = D3D11_USAGE_DEFAULT;
	texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
	ComPtr<ID3D11Texture2D> texture;
	ComPtr<ID3D11RenderTargetView> render_target;
	if (FAILED(device->CreateTexture2D(&texture_desc, nullptr, &texture)) ||
		FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &render_target)))
	{
		std::fputs("Failed to create the renderer test target.\n", stderr);
		return 1;
	}

	openxr_ui::technique_snapshot snapshot;
	for (uint64_t id = 1; id <= 12; ++id)
	{
		openxr_ui::technique entry;
		entry.id = id;
		entry.name = "Technique " + std::to_string(id);
		entry.effect_name = "Test.fx";
		entry.enabled = id % 2 == 0;
		snapshot.techniques.push_back(std::move(entry));
	}
	snapshot.techniques.front().tooltip = "A persistent shader description shown in the detail view.";
	for (uint64_t id = 100; id < 108; ++id)
	{
		openxr_ui::parameter entry;
		entry.id = id;
		entry.name = "Parameter " + std::to_string(id - 99);
		entry.effect_name = "Test.fx";
		entry.type = id == 101 ? openxr_ui::parameter_type::boolean : openxr_ui::parameter_type::floating_point;
		entry.values = { id == 101 ? 0.0 : 0.5 };
		entry.default_values = { id == 101 ? 1.0 : 0.25 };
		entry.minimum = 0.0;
		entry.maximum = 1.0;
		entry.step = 0.1;
		snapshot.parameters.push_back(std::move(entry));
	}

	openxr_ui::technique_snapshot ordering_snapshot;
	ordering_snapshot.techniques = {
		{ 1, "Inactive 1", "", "Test.fx", false, false },
		{ 2, "Active 1", "", "Test.fx", true, false },
		{ 3, "Inactive 2", "", "Test.fx", false, false },
		{ 4, "Active 2", "", "Test.fx", true, false },
	};
	openxr_ui::move_enabled_techniques_to_top(ordering_snapshot);
	if (ordering_snapshot.techniques[0].id != 2 || ordering_snapshot.techniques[1].id != 4 ||
		ordering_snapshot.techniques[2].id != 1 || ordering_snapshot.techniques[3].id != 3)
	{
		std::fputs("Enabled techniques were not moved to the top with stable ordering.\n", stderr);
		return 1;
	}
	if (!openxr_ui::move_technique(ordering_snapshot, 1, true) ||
		ordering_snapshot.techniques[1].id != 1 || ordering_snapshot.techniques[2].id != 4 ||
		!openxr_ui::move_technique(ordering_snapshot, 1, false) ||
		ordering_snapshot.techniques[1].id != 4 || ordering_snapshot.techniques[2].id != 1 ||
		openxr_ui::move_technique(ordering_snapshot, 2, true) ||
		openxr_ui::move_technique(ordering_snapshot, 3, false))
	{
		std::fputs("Individual technique up/down ordering failed or crossed a list boundary.\n", stderr);
		return 1;
	}
	for (openxr_ui::technique &entry : ordering_snapshot.techniques)
		entry.persistence_id = std::to_string(entry.id);
	openxr_ui::technique_snapshot refreshed_ordering;
	refreshed_ordering.techniques = {
		{ 1, "Inactive 1", "", "Test.fx", false, false, false, false, "1" },
		{ 2, "Active 1", "", "Test.fx", true, false, false, false, "2" },
		{ 3, "Inactive 2", "", "Test.fx", true, false, false, false, "3" },
		{ 4, "Active 2", "", "Test.fx", true, false, false, false, "4" },
	};
	openxr_ui::preserve_technique_order(refreshed_ordering, ordering_snapshot);
	if (refreshed_ordering.techniques[0].id != 2 || refreshed_ordering.techniques[1].id != 4 ||
		refreshed_ordering.techniques[2].id != 1 || refreshed_ordering.techniques[3].id != 3)
	{
		std::fputs("Technique refresh forgot the user-established order after a later activation.\n", stderr);
		return 1;
	}
	openxr_ui::parameter comparison_parameter;
	comparison_parameter.type = openxr_ui::parameter_type::floating_point;
	comparison_parameter.values = { 0.25000001, 0.5 };
	comparison_parameter.default_values = { 0.25, 0.25 };
	if (openxr_ui::component_differs_from_default(comparison_parameter, 0) ||
		!openxr_ui::component_differs_from_default(comparison_parameter, 1))
	{
		std::fputs("Parameter default comparison did not tolerate float noise or detect a changed value.\n", stderr);
		return 1;
	}
	ordering_snapshot.parameters.push_back(comparison_parameter);
	ordering_snapshot.parameters.back().effect_name = "Test.fx";
	openxr_ui::update_technique_default_states(ordering_snapshot);
	if (!std::all_of(ordering_snapshot.techniques.begin(), ordering_snapshot.techniques.end(), [](const openxr_ui::technique &entry) {
		return entry.has_non_default_values;
	}))
	{
		std::fputs("Techniques did not inherit the changed-value state from their effect.\n", stderr);
		return 1;
	}
	openxr_ui::technique unchanged_effect = { 5, "Unchanged", "", "Other.fx", false, false };
	ordering_snapshot.techniques.push_back(unchanged_effect);
	openxr_ui::update_technique_default_states(ordering_snapshot);
	if (ordering_snapshot.techniques.back().has_non_default_values)
	{
		std::fputs("Changed-value state leaked to a different effect.\n", stderr);
		return 1;
	}
	openxr_ui::technique_snapshot preset_snapshot;
	preset_snapshot.techniques = {
		{ 1, "First", "", "First.fx", true, false },
		{ 2, "Second", "", "Second.fx", false, false },
	};
	openxr_ui::parameter preset_parameter;
	preset_parameter.id = 1000;
	preset_parameter.effect_name = "Second.fx";
	preset_parameter.values = { 0.25 };
	preset_parameter.default_values = { 0.25 };
	preset_snapshot.parameters.push_back(preset_parameter);
	const std::vector<openxr_ui::runtime_technique_state> loaded_techniques = { { 2, true }, { 1, false } };
	const std::vector<openxr_ui::runtime_parameter_state> loaded_parameters = { { 1000, { 0.75 } } };
	if (!openxr_ui::reconcile_runtime_state(preset_snapshot, loaded_techniques, loaded_parameters) ||
		preset_snapshot.techniques[0].id != 2 || !preset_snapshot.techniques[0].enabled ||
		preset_snapshot.techniques[1].enabled || !preset_snapshot.techniques[0].has_non_default_values)
	{
		std::fputs("Runtime reconciliation did not reactively apply preset technique and parameter state.\n", stderr);
		return 1;
	}
	if (openxr_ui::reconcile_runtime_state(preset_snapshot, loaded_techniques, loaded_parameters))
	{
		std::fputs("Unchanged runtime state caused a redundant view-model update.\n", stderr);
		return 1;
	}

	openxr_ui::panel_renderer renderer;
	if (!renderer.initialize(device.Get(), context.Get(), texture_desc.Width, texture_desc.Height))
	{
		std::fputs("Failed to initialize the private ImGui renderer.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input warm_up = { false, 0.0f, 0.0f, false };
	renderer.render(render_target.Get(), snapshot, warm_up);
	openxr_ui::render_input layout_capture = warm_up;
	layout_capture.preset_name = "Hubris Super Resolution Tuned Copy 12";
	renderer.render(render_target.Get(), snapshot, layout_capture);

	const openxr_ui::render_input toggle_active_first = { true, 94.0f, 69.0f, true };
	const openxr_ui::render_result active_first_result = renderer.render(render_target.Get(), snapshot, toggle_active_first);
	if (active_first_result.action != openxr_ui::render_result::command::move_active_to_top)
	{
		std::fputs("Active on top did not produce its one-shot sort command.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input move_down_first = { true, 690.0f, 144.0f, true };
	const openxr_ui::render_result move_down_result = renderer.render(render_target.Get(), snapshot, move_down_first);
	const openxr_ui::render_input move_up_second = { true, 650.0f, 180.0f, true };
	const openxr_ui::render_result move_up_result = renderer.render(render_target.Get(), snapshot, move_up_second);
	if (move_down_result.action != openxr_ui::render_result::command::move_technique || move_down_result.target_id != 1 || move_down_result.state ||
		move_up_result.action != openxr_ui::render_result::command::move_technique || move_up_result.target_id != 2 || !move_up_result.state)
	{
		std::fputs("Technique arrows did not produce distinct down and up commands.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input move_up_first = { true, 650.0f, 144.0f, true };
	if (renderer.render(render_target.Get(), snapshot, move_up_first).action != openxr_ui::render_result::command::none)
	{
		std::fputs("Top technique exposed an invalid move-up command.\n", stderr);
		return 1;
	}
	openxr_ui::render_input disabled_move_down = move_down_first;
	disabled_move_down.shaders_temporarily_disabled = true;
	if (renderer.render(render_target.Get(), snapshot, disabled_move_down).action != openxr_ui::render_result::command::none)
	{
		std::fputs("Technique ordering remained interactive while shaders were temporarily disabled.\n", stderr);
		return 1;
	}
	openxr_ui::technique_snapshot active_first_snapshot = snapshot;
	openxr_ui::move_enabled_techniques_to_top(active_first_snapshot);
	if (!expect_command(renderer, render_target.Get(), active_first_snapshot, 40.0f, 144.0f,
		openxr_ui::render_result::command::toggle_technique, 2))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, 40.0f, 144.0f,
		openxr_ui::render_result::command::toggle_technique, 1))
		return 1;

	openxr_ui::technique_snapshot favourite_snapshot = snapshot;
	favourite_snapshot.techniques.front().favourite = true;
	const openxr_ui::render_input star_first = { true, 730.0f, 144.0f, true };
	const openxr_ui::render_result unfavourite_result = renderer.render(render_target.Get(), favourite_snapshot, star_first);
	const openxr_ui::render_result favourite_result = renderer.render(render_target.Get(), snapshot, star_first);
	if (unfavourite_result.action != openxr_ui::render_result::command::set_technique_favourite ||
		unfavourite_result.target_id != 1 || unfavourite_result.state ||
		favourite_result.action != openxr_ui::render_result::command::set_technique_favourite ||
		favourite_result.target_id != 1 || !favourite_result.state)
	{
		std::fputs("Technique star did not produce distinct favourite and unfavourite commands.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input toggle_favourites_only = { true, 247.0f, 69.0f, true };
	renderer.render(render_target.Get(), favourite_snapshot, toggle_favourites_only);
	if (!expect_command(renderer, render_target.Get(), favourite_snapshot, 40.0f, 144.0f,
		openxr_ui::render_result::command::toggle_technique, 1))
		return 1;
	openxr_ui::technique_snapshot no_favourites_snapshot = snapshot;
	if (renderer.render(render_target.Get(), no_favourites_snapshot, { true, 40.0f, 144.0f, true }).action != openxr_ui::render_result::command::none)
	{
		std::fputs("Favourites-only view displayed a non-favourite technique.\n", stderr);
		return 1;
	}
	renderer.render(render_target.Get(), favourite_snapshot, toggle_favourites_only);

	openxr_ui::technique_snapshot hidden_snapshot = snapshot;
	hidden_snapshot.techniques.front().hidden = true;
	renderer.render(render_target.Get(), hidden_snapshot, warm_up);
	if (!expect_command(renderer, render_target.Get(), hidden_snapshot, 40.0f, 144.0f,
		openxr_ui::render_result::command::toggle_technique, 2))
		return 1;
	const openxr_ui::render_input toggle_show_hidden = { true, 400.0f, 69.0f, true };
	renderer.render(render_target.Get(), hidden_snapshot, toggle_show_hidden);
	const openxr_ui::render_input unhide_first = { true, 800.0f, 144.0f, true };
	const openxr_ui::render_result unhide_result = renderer.render(render_target.Get(), hidden_snapshot, unhide_first);
	if (unhide_result.action != openxr_ui::render_result::command::set_technique_hidden || unhide_result.target_id != 1 || unhide_result.state)
	{
		std::fputs("Visible hidden technique did not produce an Unhide command.\n", stderr);
		return 1;
	}
	renderer.render(render_target.Get(), hidden_snapshot, toggle_show_hidden);
	const openxr_ui::render_input hide_first = { true, 800.0f, 144.0f, true };
	const openxr_ui::render_result hide_result = renderer.render(render_target.Get(), snapshot, hide_first);
	if (hide_result.action != openxr_ui::render_result::command::set_technique_hidden || hide_result.target_id != 1 || !hide_result.state)
	{
		std::fputs("Technique Hide button did not produce a persistent hide command.\n", stderr);
		return 1;
	}
	if (!expect_command(renderer, render_target.Get(), snapshot, 983.0f, 32.0f,
		openxr_ui::render_result::command::hide_panel, 0))
		return 1;

	const openxr_ui::render_input joystick_down = { false, 0.0f, 0.0f, false, false, false, false, -1.0f };
	renderer.render(render_target.Get(), snapshot, joystick_down);
	if (!expect_command(renderer, render_target.Get(), snapshot, 40.0f, 144.0f,
		openxr_ui::render_result::command::toggle_technique, 2))
		return 1;
	const openxr_ui::render_input joystick_up = { false, 0.0f, 0.0f, false, false, false, false, 1.0f };
	renderer.render(render_target.Get(), snapshot, joystick_up);
	if (!expect_command(renderer, render_target.Get(), snapshot, 40.0f, 144.0f,
		openxr_ui::render_result::command::toggle_technique, 1))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, 400.0f, 144.0f,
		openxr_ui::render_result::command::toggle_technique, 1))
		return 1;

	const openxr_ui::render_input scroll_press = { true, 983.0f, 145.0f, true, false, true };
	renderer.render(render_target.Get(), snapshot, scroll_press);
	const openxr_ui::render_input scroll_crosses_row = { true, 900.0f, 274.0f, true, false, true };
	renderer.render(render_target.Get(), snapshot, scroll_crosses_row);
	const openxr_ui::render_result drag_lock_result = renderer.render(render_target.Get(), snapshot, scroll_crosses_row);
	if (drag_lock_result.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Technique scrollbar drag did not retain exclusive interaction capture.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input scroll_drag = { true, 983.0f, 500.0f, false, false, true };
	renderer.render(render_target.Get(), snapshot, scroll_drag);
	const openxr_ui::render_input release = { true, 983.0f, 360.0f, false, false, false, true };
	renderer.render(render_target.Get(), snapshot, release);
	const openxr_ui::render_input held_drift_over_open = { true, 900.0f, 274.0f, true, false, true };
	const openxr_ui::render_result held_drift_result = renderer.render(render_target.Get(), snapshot, held_drift_over_open);
	if (held_drift_result.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Trigger chatter during a held scrollbar drag escaped to another control.\n", stderr);
		return 1;
	}
	renderer.render(render_target.Get(), snapshot, scroll_drag);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	if (!expect_command(renderer, render_target.Get(), snapshot, 40.0f, 429.0f,
		openxr_ui::render_result::command::toggle_technique, 11))
	{
		std::fputs("Scrolled technique row did not toggle.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input open_technique_11 = { true, 900.0f, 429.0f, true };
	renderer.render(render_target.Get(), snapshot, open_technique_11);
	if (!expect_command(renderer, render_target.Get(), snapshot, 615.0f, 32.0f,
		openxr_ui::render_result::command::toggle_technique, 11))
	{
		std::fputs("Detail header technique button did not toggle.\n", stderr);
		return 1;
	}

	const openxr_ui::render_input back = { true, 50.0f, 32.0f, true };
	renderer.render(render_target.Get(), snapshot, back);
	const openxr_ui::render_input scroll_back_press = { true, 983.0f, 310.0f, true, false, true };
	renderer.render(render_target.Get(), snapshot, scroll_back_press);
	const openxr_ui::render_input scroll_back_drag = { true, 983.0f, 0.0f, false, false, true };
	renderer.render(render_target.Get(), snapshot, scroll_back_drag);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	const openxr_ui::render_input open_technique_1 = { true, 900.0f, 144.0f, true };
	renderer.render(render_target.Get(), snapshot, open_technique_1);
	if (!expect_command(renderer, render_target.Get(), snapshot, 202.5f, 105.0f,
		openxr_ui::render_result::command::set_parameter, 100, 0, 0.1377, false))
		return 1;
	const openxr_ui::render_input value_drag = { true, 371.6f, 105.0f, false, false, true };
	const openxr_ui::render_result dragged_value = renderer.render(render_target.Get(), snapshot, value_drag);
	if (dragged_value.action != openxr_ui::render_result::command::set_parameter || dragged_value.target_id != 100 ||
		std::abs(dragged_value.value - 0.6437) >= 0.0001)
	{
		std::fprintf(stderr, "Numeric slider did not update while select remained held: action %u, target %llu, value %.6f.\n",
			static_cast<unsigned>(dragged_value.action), static_cast<unsigned long long>(dragged_value.target_id), dragged_value.value);
		return 1;
	}
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	if (!expect_command(renderer, render_target.Get(), snapshot, 738.0f, 105.0f,
		openxr_ui::render_result::command::set_parameter, 100, 0, 0.6))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, 793.0f, 105.0f,
		openxr_ui::render_result::command::set_parameter, 100, 0, 1.5))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, 888.0f, 105.0f,
		openxr_ui::render_result::command::reset_parameter, 100))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, 653.0f, 210.0f,
		openxr_ui::render_result::command::set_parameter, 101, 0, 1.0))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, 725.0f, 32.0f,
		openxr_ui::render_result::command::reset_all_parameters, 1))
		return 1;
	const openxr_ui::render_input disabled_revert = { true, 830.0f, 32.0f, true };
	if (renderer.render(render_target.Get(), snapshot, disabled_revert).action != openxr_ui::render_result::command::none)
	{
		std::fputs("Revert remained interactive when the technique matched its saved values.\n", stderr);
		return 1;
	}
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	openxr_ui::technique_snapshot changed_snapshot = snapshot;
	changed_snapshot.techniques.front().differs_from_saved = true;
	if (!expect_command(renderer, render_target.Get(), changed_snapshot, 830.0f, 32.0f,
		openxr_ui::render_result::command::revert_technique, 1))
		return 1;
	openxr_ui::technique_snapshot restorable_snapshot = snapshot;
	restorable_snapshot.techniques.front().has_restore_state = true;
	if (!expect_command(renderer, render_target.Get(), restorable_snapshot, 830.0f, 32.0f,
		openxr_ui::render_result::command::restore_technique, 1))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, 950.0f, 32.0f,
		openxr_ui::render_result::command::save_preset, 0))
		return 1;

	renderer.render(render_target.Get(), snapshot, joystick_down);
	renderer.render(render_target.Get(), snapshot, warm_up);
	renderer.render(render_target.Get(), snapshot, joystick_down);
	if (!expect_command(renderer, render_target.Get(), snapshot, 738.0f, 105.0f,
		openxr_ui::render_result::command::set_parameter, 102, 0, 0.6))
	{
		std::fputs("Joystick did not scroll shader parameters by row.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input parameter_scroll_press = { true, 983.0f, 145.0f, true, false, true };
	renderer.render(render_target.Get(), snapshot, parameter_scroll_press);
	const openxr_ui::render_input parameter_scroll_drag = { true, 983.0f, 570.0f, false, false, true };
	renderer.render(render_target.Get(), snapshot, parameter_scroll_drag);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	if (!expect_command(renderer, render_target.Get(), snapshot, 738.0f, 105.0f,
		openxr_ui::render_result::command::set_parameter, 103, 0, 0.6))
	{
		std::fputs("Shader parameter scrollbar did not scroll to the final row offset.\n", stderr);
		return 1;
	}

	const openxr_ui::render_input detail_back = { true, 50.0f, 32.0f, true };
	renderer.render(render_target.Get(), snapshot, detail_back);
	if (!expect_command(renderer, render_target.Get(), snapshot, 950.0f, 609.0f,
		openxr_ui::render_result::command::toggle_panel_size, 0))
		return 1;
	const openxr_ui::render_input long_press_toggle = { true, 160.0f, 572.0f, true, false, false, false, 0.0f, false };
	const openxr_ui::render_result long_press_result = renderer.render(render_target.Get(), snapshot, long_press_toggle);
	if (long_press_result.action != openxr_ui::render_result::command::toggle_long_press)
	{
		std::fputs("Long-press visibility checkbox did not produce its command.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input starts_open_toggle = { true, 100.0f, 535.0f, true, false, false, false, 0.0f, false, true };
	const openxr_ui::render_result starts_open_result = renderer.render(render_target.Get(), snapshot, starts_open_toggle);
	if (starts_open_result.action != openxr_ui::render_result::command::set_ui_starts_open || starts_open_result.state)
	{
		std::fputs("UI starts open checkbox did not produce its persisted closed-state command.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input performance_toggle = { true, 100.0f, 498.0f, true, false, false, false, 0.0f, false, true, false };
	const openxr_ui::render_result performance_result = renderer.render(render_target.Get(), snapshot, performance_toggle);
	if (performance_result.action != openxr_ui::render_result::command::set_performance_mode || !performance_result.state)
	{
		std::fputs("Performance mode checkbox did not produce its persisted next-start command.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input statistics_toggle = { true, 420.0f, 498.0f, true };
	const openxr_ui::render_result statistics_result = renderer.render(render_target.Get(), snapshot, statistics_toggle);
	if (statistics_result.action != openxr_ui::render_result::command::set_always_show_statistics || !statistics_result.state)
	{
		std::fputs("Always show FPS checkbox did not produce its persisted command.\n", stderr);
		return 1;
	}
	openxr_ui::render_input frame_rate_toggle = { true, 440.0f, 535.0f, true };
	frame_rate_toggle.frame_rate_divisor = 3;
	const openxr_ui::render_result frame_rate_result = renderer.render(render_target.Get(), snapshot, frame_rate_toggle);
	if (frame_rate_result.action != openxr_ui::render_result::command::set_frame_rate_divisor || frame_rate_result.value != 1.0)
	{
		std::fputs("FPS target button did not cycle from third rate back to full rate.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input lock_toggle = { true, 420.0f, 572.0f, true };
	if (renderer.render(render_target.Get(), snapshot, lock_toggle).action != openxr_ui::render_result::command::toggle_panel_lock)
	{
		std::fputs("Lock UI in place checkbox did not produce its command.\n", stderr);
		return 1;
	}
	if (!expect_command(renderer, render_target.Get(), snapshot, 675.0f, 102.0f,
		openxr_ui::render_result::command::previous_preset, 0) ||
		!expect_command(renderer, render_target.Get(), snapshot, 715.0f, 102.0f,
		openxr_ui::render_result::command::next_preset, 0))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, 780.0f, 102.0f,
		openxr_ui::render_result::command::save_preset, 0))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, 935.0f, 102.0f,
		openxr_ui::render_result::command::delete_preset, 0))
		return 1;
	openxr_ui::render_input open_save_as = { true, 840.0f, 102.0f, true };
	open_save_as.preset_name = "Hubris Super Resolution Tuned Copy 12";
	if (renderer.render(render_target.Get(), snapshot, open_save_as).action != openxr_ui::render_result::command::none)
	{
		std::fputs("Save As header button bypassed the naming dialog.\n", stderr);
		return 1;
	}
	renderer.render(render_target.Get(), snapshot, { true, 720.0f, 387.0f, true });
	renderer.render(render_target.Get(), snapshot, { true, 604.0f, 279.0f, true });
	renderer.render(render_target.Get(), snapshot, { true, 742.0f, 229.0f, true });
	const openxr_ui::render_result create_preset = renderer.render(render_target.Get(), snapshot, { true, 920.0f, 453.0f, true });
	if (create_preset.action != openxr_ui::render_result::command::save_preset_as || create_preset.text != "HI")
	{
		std::fputs("Save As keyboard did not emit the entered preset name.\n", stderr);
		return 1;
	}
	renderer.notify_save_as_started(true);
	if (!expect_command(renderer, render_target.Get(), snapshot, 80.0f, 609.0f,
		openxr_ui::render_result::command::toggle_all_techniques, 0))
		return 1;
	const openxr_ui::render_input temporarily_disabled_save = {
		true, 780.0f, 102.0f, true, false, false, false, 0.0f, false, true, false, true, false
	};
	if (renderer.render(render_target.Get(), snapshot, temporarily_disabled_save).action != openxr_ui::render_result::command::none)
	{
		std::fputs("Save remained interactive while shaders were temporarily disabled.\n", stderr);
		return 1;
	}
	openxr_ui::render_input temporarily_disabled_delete = temporarily_disabled_save;
	temporarily_disabled_delete.pointer_x = 950.0f;
	if (renderer.render(render_target.Get(), snapshot, temporarily_disabled_delete).action != openxr_ui::render_result::command::none)
	{
		std::fputs("Delete remained interactive while shaders were temporarily disabled.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input restore_all = {
		true, 80.0f, 609.0f, true, false, false, false, 0.0f, false, true, false, true, false
	};
	if (renderer.render(render_target.Get(), snapshot, restore_all).action != openxr_ui::render_result::command::toggle_all_techniques)
	{
		std::fputs("Restore shaders did not remain interactive during temporary disable.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input open_expanded_technique_11 = { true, 900.0f, 449.0f, true, true };
	renderer.render(render_target.Get(), snapshot, open_expanded_technique_11);
	if (!expect_command(renderer, render_target.Get(), snapshot, 615.0f, 32.0f,
		openxr_ui::render_result::command::toggle_technique, 11, 0, 0.0, true))
		return 1;

	openxr_ui::frame_statistics_tracker tracker;
	std::chrono::steady_clock::time_point frame_time;
	const auto add_frames = [&tracker, &frame_time](int count, int milliseconds) {
		for (int index = 0; index < count; ++index)
		{
			frame_time += std::chrono::milliseconds(milliseconds);
			tracker.add_frame(frame_time);
		}
	};
	tracker.add_frame(frame_time);
	const bool default_refresh_ok = tracker.statistics().refresh_hz == openxr_ui::default_refresh_hz && !tracker.statistics().refresh_hz_reported;
	tracker.set_refresh_hz(120.0f);
	tracker.set_refresh_hz(0.0f);
	const bool reported_refresh_ok = tracker.statistics().refresh_hz == 120.0f && tracker.statistics().refresh_hz_reported;
	tracker.set_frame_rate_divisor(2);
	tracker.set_frame_rate_divisor(4);
	const bool half_rate_ok = tracker.statistics().target_hz() == 60.0f;
	tracker.set_frame_rate_divisor(1);
	if (!default_refresh_ok || !reported_refresh_ok || !half_rate_ok)
	{
		std::fputs("The FPS graph did not keep the headset refresh rate.\n", stderr);
		return 1;
	}
	add_frames(1000, 10);
	add_frames(1, 100);
	add_frames(200, 10);
	add_frames(1, 2000);
	const openxr_ui::frame_statistics statistics = tracker.statistics();
	constexpr double graph_column_seconds = openxr_ui::frame_graph_seconds / openxr_ui::frame_graph_columns;
	size_t other_columns = 0;
	size_t stutter_columns = 0;
	size_t newest_stutter_column = 0;
	for (size_t index = 0; index < openxr_ui::frame_graph_columns; ++index)
	{
		const float milliseconds = statistics.graph_ms[index];
		if (std::abs(milliseconds - 100.0f) < 0.1f)
		{
			++stutter_columns;
			newest_stutter_column = index;
		}
		else if (std::abs(milliseconds - 10.0f) > 0.1f)
			++other_columns;
	}
	const double stutter_seconds_ago = static_cast<double>(openxr_ui::frame_graph_columns - 1 - newest_stutter_column) * graph_column_seconds;
	if (statistics.revision == 0 || std::abs(statistics.average_fps - 100.0f) > 1.0f || std::abs(statistics.frame_time_ms - 10.0f) > 0.1f ||
		other_columns != 0 || stutter_columns < 9 || stutter_columns > 11 || stutter_seconds_ago < 1.4 || stutter_seconds_ago > 2.1)
	{
		std::fprintf(stderr, "Frame statistics were %.2f FPS, %.2f ms; graph had %zu empty or unexpected columns and %zu stutter columns ending %.2f s ago.\n",
			statistics.average_fps, statistics.frame_time_ms, other_columns, stutter_columns, stutter_seconds_ago);
		return 1;
	}

	texture_desc.Width = openxr_ui::statistics_width;
	texture_desc.Height = openxr_ui::statistics_height;
	ComPtr<ID3D11Texture2D> statistics_texture;
	ComPtr<ID3D11RenderTargetView> statistics_target;
	openxr_ui::panel_renderer statistics_renderer;
	if (FAILED(device->CreateTexture2D(&texture_desc, nullptr, &statistics_texture)) ||
		FAILED(device->CreateRenderTargetView(statistics_texture.Get(), nullptr, &statistics_target)) ||
		!statistics_renderer.initialize(device.Get(), context.Get(), openxr_ui::statistics_width, openxr_ui::statistics_height))
	{
		std::fputs("Failed to create the FPS counter renderer.\n", stderr);
		return 1;
	}
	statistics_renderer.render_statistics(statistics_target.Get(), openxr_ui::frame_statistics());
	statistics_renderer.render_statistics(statistics_target.Get(), statistics);

	std::puts("Private ImGui named preset save/delete controls, sorting, shader hiding, global favourites, close control, full-row toggles, drag scrolling, sliders, expansion, parameter controls, and the FPS counter passed.");
	return 0;
}