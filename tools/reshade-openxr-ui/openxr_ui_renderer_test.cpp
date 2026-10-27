#include "openxr_ui_renderer.hpp"
#include "vr_gamepad.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Microsoft::WRL::ComPtr;

	// The helpers below find controls by id in the frame they render, so a layout change moves the clicks with it.
	// Every helper first draws a frame without the pointer so the controls match this snapshot and input.
	void render_layout(openxr_ui::panel_renderer &renderer, ID3D11RenderTargetView *render_target,
		const openxr_ui::technique_snapshot &snapshot, const openxr_ui::render_input &input_template)
	{
		openxr_ui::render_input input = input_template;
		input.pointer_visible = false;
		input.select_clicked = false;
		input.select_down = false;
		input.select_released = false;
		input.scroll_y = 0.0f;
		renderer.render(render_target, snapshot, input);
	}

	bool find_rect(const openxr_ui::panel_renderer &renderer, std::string_view id, openxr_ui::panel_renderer::control_rect &rect)
	{
		for (const openxr_ui::panel_renderer::control_rect &control : renderer.recorded_controls())
		{
			if (control.id != id)
				continue;
			rect = control;
			return true;
		}
		std::fprintf(stderr, "Control \"%.*s\" was not drawn.\n", static_cast<int>(id.size()), id.data());
		return false;
	}

	bool find_point(const openxr_ui::panel_renderer &renderer, std::string_view id, float &x, float &y)
	{
		if (renderer.find_control(id, x, y))
			return true;
		std::fprintf(stderr, "Control \"%.*s\" was not drawn.\n", static_cast<int>(id.size()), id.data());
		return false;
	}

	// The spot where a third effect row would be, from the first two rows of the last frame (the point has no control).
	bool below_second_row(const openxr_ui::panel_renderer &renderer, float &x, float &y)
	{
		float first_y = 0.0f;
		float second_y = 0.0f;
		if (!find_point(renderer, "effect/0/toggle", x, first_y) || !find_point(renderer, "effect/1/toggle", x, second_y))
			return false;
		y = second_y + (second_y - first_y);
		return true;
	}

	// A point on a scroll bar's track (inset 8 px like the drawn track), `fraction` of the way down.
	bool scroll_bar_point(const openxr_ui::panel_renderer &renderer, std::string_view id, float fraction, float &x, float &y)
	{
		openxr_ui::panel_renderer::control_rect rect;
		if (!find_rect(renderer, id, rect))
			return false;
		x = (rect.min_x + rect.max_x) * 0.5f;
		y = rect.min_y + 8.0f + fraction * (rect.max_y - rect.min_y - 16.0f);
		return true;
	}

	// The id of the `index`th choice in the open list of the drop-down `dropdown`.
	std::string dropdown_item(std::string_view dropdown, uint32_t index)
	{
		return std::string(dropdown) + "/item/" + std::to_string(index);
	}

	// Clicks the centre of the control `id`. `input_template` carries every other render_input field.
	bool click(openxr_ui::panel_renderer &renderer, ID3D11RenderTargetView *render_target,
		const openxr_ui::technique_snapshot &snapshot, std::string_view id, openxr_ui::render_result &result,
		const openxr_ui::render_input &input_template = {})
	{
		render_layout(renderer, render_target, snapshot, input_template);
		openxr_ui::render_input input = input_template;
		if (!renderer.find_control(id, input.pointer_x, input.pointer_y))
		{
			std::fprintf(stderr, "Control \"%.*s\" was not drawn.\n", static_cast<int>(id.size()), id.data());
			return false;
		}
		input.pointer_visible = true;
		input.select_clicked = true;
		result = renderer.render(render_target, snapshot, input);
		return true;
	}

	// For clicks whose result the test does not look at.
	bool click(openxr_ui::panel_renderer &renderer, ID3D11RenderTargetView *render_target,
		const openxr_ui::technique_snapshot &snapshot, std::string_view id, const openxr_ui::render_input &input_template = {})
	{
		openxr_ui::render_result result;
		return click(renderer, render_target, snapshot, id, result, input_template);
	}

	// Clicks a point that is not a control, such as empty space or the spot where a control used to be.
	openxr_ui::render_result click_at(openxr_ui::panel_renderer &renderer, ID3D11RenderTargetView *render_target,
		const openxr_ui::technique_snapshot &snapshot, float x, float y, openxr_ui::render_input input = {})
	{
		input.pointer_visible = true;
		input.pointer_x = x;
		input.pointer_y = y;
		input.select_clicked = true;
		return renderer.render(render_target, snapshot, input);
	}

	bool expect_result(const openxr_ui::render_result &result, openxr_ui::render_result::command expected_action,
		uint64_t expected_id, uint32_t expected_component = 0, double expected_value = 0.0)
	{
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

	bool expect_command(openxr_ui::panel_renderer &renderer, ID3D11RenderTargetView *render_target,
		const openxr_ui::technique_snapshot &snapshot, std::string_view id,
		openxr_ui::render_result::command expected_action, uint64_t expected_id, uint32_t expected_component = 0,
		double expected_value = 0.0, bool expanded = false)
	{
		openxr_ui::render_input input_template;
		input_template.expanded = expanded;
		openxr_ui::render_result result;
		return click(renderer, render_target, snapshot, id, result, input_template) &&
			expect_result(result, expected_action, expected_id, expected_component, expected_value);
	}

	// Like expect_command, but at a point instead of a control.
	bool expect_command_at(openxr_ui::panel_renderer &renderer, ID3D11RenderTargetView *render_target,
		const openxr_ui::technique_snapshot &snapshot, float x, float y,
		openxr_ui::render_result::command expected_action, uint64_t expected_id)
	{
		return expect_result(click_at(renderer, render_target, snapshot, x, y), expected_action, expected_id);
	}

	bool preset_gesture_logic_works()
	{
		// Hand against the left temple fires; held out to aim, on the chest, or below the chin does not.
		if (!openxr_ui::hand_near_head(-0.12f, 0.02f, -0.03f) || openxr_ui::hand_near_head(-0.10f, -0.05f, -0.40f) ||
			openxr_ui::hand_near_head(0.0f, -0.35f, -0.10f) || openxr_ui::hand_near_head(-0.05f, -0.15f, -0.05f))
		{
			std::fputs("Preset gesture hand-near-head rule accepted or rejected the wrong hand position.\n", stderr);
			return false;
		}
		openxr_ui::preset_gesture_tracker tracker;
		if (tracker.press_started(false) || !tracker.press_started(true) || tracker.press_started(true) ||
			tracker.press_started(false) || !tracker.press_started(true))
		{
			std::fputs("Preset gesture fired other than once per button press.\n", stderr);
			return false;
		}
		openxr_ui::preset_gesture_tracker release_tracker;
		if (release_tracker.press_ended(false) || release_tracker.press_ended(true) || !release_tracker.press_ended(false) ||
			release_tracker.press_ended(false))
		{
			std::fputs("L3+R3 preset gesture fired other than once per release.\n", stderr);
			return false;
		}
		if (openxr_ui::stick_gesture_allowed(openxr_ui::menu_button_both_sticks, false) ||
			!openxr_ui::stick_gesture_allowed(openxr_ui::menu_button_hold_both_sticks, false) ||
			openxr_ui::stick_gesture_allowed(openxr_ui::menu_button_hold_both_sticks, true) ||
			!openxr_ui::stick_gesture_allowed(openxr_ui::menu_button_b_and_y, true) ||
			openxr_ui::preset_gesture_button_for(openxr_ui::preset_gesture_both_sticks) != openxr_ui::preset_gesture_button::none)
		{
			std::fputs("L3+R3 preset gesture clashed with the L3+R3 menu shortcut.\n", stderr);
			return false;
		}
		if (openxr_ui::preset_gesture_button_for(openxr_ui::preset_gesture_off) != openxr_ui::preset_gesture_button::none ||
			openxr_ui::preset_gesture_button_for(openxr_ui::preset_gesture_left_x) != openxr_ui::preset_gesture_button::face ||
			openxr_ui::preset_gesture_button_for(openxr_ui::preset_gesture_right_grip) != openxr_ui::preset_gesture_button::grip ||
			!openxr_ui::preset_gesture_uses_left_hand(openxr_ui::preset_gesture_left_trigger) ||
			openxr_ui::preset_gesture_uses_left_hand(openxr_ui::preset_gesture_right_trigger))
		{
			std::fputs("Preset gesture choice mapped to the wrong hand or button.\n", stderr);
			return false;
		}
		return true;
	}

	bool gamepad_logic_works()
	{
		if (openxr_ui::gamepad_stick(7000, openxr_ui::gamepad_left_stick_dead_zone) != 0.0f ||
			openxr_ui::gamepad_stick(32767, openxr_ui::gamepad_left_stick_dead_zone) != 1.0f ||
			openxr_ui::gamepad_stick(-32768, openxr_ui::gamepad_left_stick_dead_zone) != -1.0f)
		{
			std::fputs("Gamepad stick dead zone or range is wrong.\n", stderr);
			return false;
		}

		constexpr float width = 1000.0f;
		constexpr float height = 600.0f;
		constexpr float frame = 0.01f;
		openxr_ui::gamepad_reading pad;
		pad.connected = true;
		openxr_ui::gamepad_pointer pointer;
		pad.buttons = openxr_ui::gamepad_left_thumb | openxr_ui::gamepad_right_thumb;
		if (!openxr_ui::gamepad_menu_pressed(pad, openxr_ui::menu_button_both_sticks) ||
			openxr_ui::gamepad_menu_pressed(pad, openxr_ui::menu_button_b_and_y) ||
			openxr_ui::gamepad_menu_pressed(pad, openxr_ui::menu_button_off))
		{
			std::fputs("Gamepad buttons did not match the chosen menu shortcut.\n", stderr);
			return false;
		}
		openxr_ui::menu_shortcut_press shortcut;
		if (!shortcut.update(true, frame, openxr_ui::menu_button_both_sticks) ||
			shortcut.update(true, frame, openxr_ui::menu_button_both_sticks))
		{
			std::fputs("L3+R3 did not toggle the menu exactly once per press.\n", stderr);
			return false;
		}
		// Some VR mods report the VR controllers also as a gamepad, so one press reaches the add-on from two devices,
		// sometimes a frame apart. It must still show or hide the menu only once.
		openxr_ui::menu_shortcut_press shared;
		const bool controller_down[] = { false, true, true, true, false, false, false };
		const bool gamepad_down[] = { false, false, true, true, true, false, false };
		int toggles = 0;
		for (size_t i = 0; i < std::size(controller_down); ++i)
		{
			pad.buttons = gamepad_down[i] ? openxr_ui::gamepad_left_thumb | openxr_ui::gamepad_right_thumb : 0;
			toggles += shared.update(controller_down[i] || openxr_ui::gamepad_menu_pressed(pad, openxr_ui::menu_button_both_sticks),
				frame, openxr_ui::menu_button_both_sticks) ? 1 : 0;
		}
		if (toggles != 1)
		{
			std::fputs("One L3+R3 press reported by both the VR controllers and a gamepad did not toggle the menu exactly once.\n", stderr);
			return false;
		}
		openxr_ui::menu_shortcut_press hold;
		bool toggled_early = false;
		for (int i = 0; i < 140; ++i)
			toggled_early |= hold.update(true, frame, openxr_ui::menu_button_hold_b_and_y);
		bool toggled_late = false;
		for (int i = 0; i < 20; ++i)
			toggled_late |= hold.update(true, frame, openxr_ui::menu_button_hold_b_and_y);
		if (toggled_early || !toggled_late || hold.update(true, frame, openxr_ui::menu_button_hold_b_and_y))
		{
			std::fputs("Hold B+Y did not wait 1.5 seconds before toggling the menu once.\n", stderr);
			return false;
		}

		pad.buttons = 0;
		if (pointer.update(pad, frame, true, width, height).pointer_active)
		{
			std::fputs("Gamepad pointer appeared without a stick push or A press.\n", stderr);
			return false;
		}
		pad.buttons = openxr_ui::gamepad_a;
		openxr_ui::gamepad_frame result = pointer.update(pad, frame, true, width, height);
		if (!result.pointer_active || result.select_clicked || result.pointer_x != width * 0.5f || result.pointer_y != height * 0.5f)
		{
			std::fputs("Gamepad A press that showed the pointer also clicked, or the pointer did not start centred.\n", stderr);
			return false;
		}
		pad.buttons = 0;
		pad.left_x = 1.0f;
		pad.left_y = 1.0f;
		result = pointer.update(pad, 0.1f, true, width, height);
		if (result.pointer_x <= width * 0.5f || result.pointer_y >= height * 0.5f)
		{
			std::fputs("Gamepad left stick did not move the pointer right and up.\n", stderr);
			return false;
		}
		pad.left_x = 0.0f;
		pad.left_y = 0.0f;
		pad.buttons = openxr_ui::gamepad_a;
		const bool clicked = pointer.update(pad, frame, true, width, height).select_clicked;
		pad.buttons = 0;
		const bool released = pointer.update(pad, frame, true, width, height).select_released;
		if (!clicked || !released)
		{
			std::fputs("Gamepad A did not click and release.\n", stderr);
			return false;
		}
		if (pointer.update(pad, frame, false, width, height).pointer_active)
		{
			std::fputs("Gamepad pointer stayed active with the menu hidden.\n", stderr);
			return false;
		}
		return true;
	}

	// Runs `seconds` of swinging, the hands moving in opposite directions at `speed` metres per second, 90 frames a second.
	float swing(openxr_ui::arm_swing_tracker &tracker, float speed, float seconds, uint32_t strength, bool opposite = true)
	{
		constexpr float frame = 1.0f / 90.0f;
		constexpr float half_swing_seconds = 0.5f;
		float left = -0.6f;
		float right = -0.6f;
		float forward = 0.0f;
		for (float time = 0.0f; time < seconds; time += frame)
		{
			const float direction = std::fmod(time, 2.0f * half_swing_seconds) < half_swing_seconds ? 1.0f : -1.0f;
			left += direction * speed * frame;
			right += (opposite ? -direction : direction) * speed * frame;
			forward = tracker.update(left, right, frame, strength);
		}
		return forward;
	}

	bool arm_swing_logic_works()
	{
		openxr_ui::arm_swing_tracker still;
		openxr_ui::arm_swing_tracker together;
		openxr_ui::arm_swing_tracker slow;
		openxr_ui::arm_swing_tracker fast;
		openxr_ui::arm_swing_tracker strong;
		if (swing(still, 0.0f, 2.0f, 100) != 0.0f || swing(together, 1.5f, 2.0f, 100, false) != 0.0f)
		{
			std::fputs("Arm swing walked with still hands or with both hands moving the same way.\n", stderr);
			return false;
		}
		const float slow_forward = swing(slow, 0.7f, 2.0f, 100);
		const float fast_forward = swing(fast, 1.2f, 2.0f, 100);
		const float strong_forward = swing(strong, 0.7f, 2.0f, 200);
		if (slow_forward <= 0.0f || fast_forward <= slow_forward || strong_forward <= slow_forward || fast_forward > 1.0f)
		{
			std::fprintf(stderr, "Arm swing forward values are wrong: slow %.3f, fast %.3f, slow at 200%% %.3f.\n",
				slow_forward, fast_forward, strong_forward);
			return false;
		}
		if (swing(fast, 0.0f, 2.0f, 100) != 0.0f)
		{
			std::fputs("Arm swing kept walking after the arms stopped.\n", stderr);
			return false;
		}
		// Real hands slow to a stop at the top and bottom of every swing. A slow, steady swing (0.8 seconds per half-swing)
		// must still walk at a steady speed instead of stepping and stopping.
		openxr_ui::arm_swing_tracker steady;
		constexpr float frame = 1.0f / 90.0f;
		constexpr float half_swing_seconds = 0.8f;
		constexpr float pi = 3.14159265f;
		float lowest = 1.0f;
		float highest = 0.0f;
		for (float time = 0.0f; time < 6.0f; time += frame)
		{
			const float height = -0.6f + 0.15f * std::sin(pi * time / half_swing_seconds);
			const float forward = steady.update(height, -1.2f - height, frame, 100);
			if (time >= 3.0f)
			{
				lowest = (std::min)(lowest, forward);
				highest = (std::max)(highest, forward);
			}
		}
		if (highest <= 0.0f || lowest < highest * 0.9f)
		{
			std::fprintf(stderr, "A slow, steady arm swing walked unevenly: forward went from %.3f to %.3f.\n", lowest, highest);
			return false;
		}
		if (openxr_ui::clamp_arm_swing_strength(0) != openxr_ui::arm_swing_strength_min ||
			openxr_ui::clamp_arm_swing_strength(1000) != openxr_ui::arm_swing_strength_max)
		{
			std::fputs("Arm swing strength was not kept within its range.\n", stderr);
			return false;
		}
		return true;
	}

	// Counts the presses the game sees while the swing level follows `levels`, one entry per frame of `seconds`.
	uint32_t run_presses(openxr_ui::arm_swing_run_button &button, std::initializer_list<float> levels, uint32_t mode, float seconds,
		bool &pressed)
	{
		uint32_t presses = 0;
		for (const float level : levels)
		{
			const bool now = button.update(level, mode, openxr_ui::arm_swing_run_at_default, seconds);
			presses += now && !pressed ? 1 : 0;
			pressed = now;
		}
		return presses;
	}

	bool arm_swing_run_logic_works()
	{
		openxr_ui::arm_swing_tracker weak;
		openxr_ui::arm_swing_tracker strong;
		swing(weak, 3.0f, 2.0f, openxr_ui::arm_swing_strength_min);
		swing(strong, 3.0f, 2.0f, openxr_ui::arm_swing_strength_max);
		if (weak.level() < 1.5f || weak.level() != strong.level())
		{
			std::fprintf(stderr, "Fast swings gave run levels %.3f and %.3f; they must reach 1.5 and ignore the strength.\n",
				weak.level(), strong.level());
			return false;
		}

		constexpr float frame = 1.0f / 90.0f;
		bool pressed = false;
		openxr_ui::arm_swing_run_button off;
		if (run_presses(off, { 3.0f, 3.0f, 3.0f }, openxr_ui::arm_swing_run_off, frame, pressed) != 0)
		{
			std::fputs("Running with Run set to Off pressed the stick button.\n", stderr);
			return false;
		}
		// Hold: pressed from 150% on, still pressed just above 80% of that, released below.
		openxr_ui::arm_swing_run_button hold;
		pressed = false;
		if (run_presses(hold, { 1.0f, 1.5f }, openxr_ui::arm_swing_run_hold_a, frame, pressed) != 1 || !pressed ||
			run_presses(hold, { 1.25f }, openxr_ui::arm_swing_run_hold_a, frame, pressed) != 0 || !pressed ||
			run_presses(hold, { 1.1f }, openxr_ui::arm_swing_run_hold_a, frame, pressed) != 0 || pressed)
		{
			std::fputs("Hold running did not hold the button from 150% and let go below 120%.\n", stderr);
			return false;
		}
		// Each choice presses its own button: the left stick for Click, A and B on the right controller, X and Y on the left.
		using button_id = openxr_ui::arm_swing_run_button_id;
		if (openxr_ui::arm_swing_run_button_for(openxr_ui::arm_swing_run_click) != button_id::left_stick ||
			openxr_ui::arm_swing_run_button_for(openxr_ui::arm_swing_run_hold_a) != button_id::a ||
			openxr_ui::arm_swing_run_button_for(openxr_ui::arm_swing_run_hold_b) != button_id::b ||
			openxr_ui::arm_swing_run_button_for(openxr_ui::arm_swing_run_hold_x) != button_id::x ||
			openxr_ui::arm_swing_run_button_for(openxr_ui::arm_swing_run_hold_y) != button_id::y ||
			!openxr_ui::arm_swing_run_button_on_right_hand(button_id::a) || !openxr_ui::arm_swing_run_button_on_right_hand(button_id::b) ||
			openxr_ui::arm_swing_run_button_on_right_hand(button_id::x) || openxr_ui::arm_swing_run_button_on_right_hand(button_id::y) ||
			openxr_ui::arm_swing_run_button_on_right_hand(button_id::left_stick))
		{
			std::fputs("A Run choice pressed the wrong button or hand.\n", stderr);
			return false;
		}
		// Click: one click to start, none while running, one when slowing to a walk.
		openxr_ui::arm_swing_run_button click;
		pressed = false;
		uint32_t presses = run_presses(click, { 2.0f }, openxr_ui::arm_swing_run_click, frame, pressed);
		for (int index = 0; index < 90; ++index)
			presses += run_presses(click, { 2.0f }, openxr_ui::arm_swing_run_click, frame, pressed);
		if (presses != 1 || pressed || !click.running())
		{
			std::fprintf(stderr, "Click running pressed %u times in its first second instead of once.\n", presses);
			return false;
		}
		presses = 0;
		for (int index = 0; index < 90; ++index)
			presses += run_presses(click, { 0.5f }, openxr_ui::arm_swing_run_click, frame, pressed);
		if (presses != 1 || pressed || click.running())
		{
			std::fprintf(stderr, "Click running pressed %u times when slowing down instead of once.\n", presses);
			return false;
		}
		// At 5 frames a second, starting and stopping on back-to-back frames still gives two separate clicks.
		openxr_ui::arm_swing_run_button slow_frames;
		pressed = false;
		if (run_presses(slow_frames, { 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, openxr_ui::arm_swing_run_click, 0.2f, pressed) != 2 || pressed)
		{
			std::fputs("Click running merged its start and stop clicks at a low frame rate.\n", stderr);
			return false;
		}

		if (openxr_ui::block_stick_forward(0.8f) != 0.0f || openxr_ui::block_stick_forward(-0.5f) != -0.5f)
		{
			std::fputs("The left stick block did not remove only forward.\n", stderr);
			return false;
		}
		if (openxr_ui::clamp_arm_swing_run_at(0) != openxr_ui::arm_swing_run_at_min ||
			openxr_ui::clamp_arm_swing_run_at(1000) != openxr_ui::arm_swing_run_at_max)
		{
			std::fputs("Run at was not kept within its range.\n", stderr);
			return false;
		}
		return true;
	}
}

int main()
{
	if (!preset_gesture_logic_works() || !gamepad_logic_works() || !arm_swing_logic_works() || !arm_swing_run_logic_works())
		return 1;
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
	for (uint64_t id = 100; id < 116; ++id)
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
	renderer.set_control_recording(true);
	renderer.render(render_target.Get(), snapshot, warm_up);
	openxr_ui::render_input layout_capture = warm_up;
	layout_capture.preset_name = "Hubris Super Resolution Tuned Copy 12";
	renderer.render(render_target.Get(), snapshot, layout_capture);

	openxr_ui::render_result active_first_result;
	if (!click(renderer, render_target.Get(), snapshot, "active_first", active_first_result))
		return 1;
	if (active_first_result.action != openxr_ui::render_result::command::move_active_to_top)
	{
		std::fputs("Active on top did not produce its one-shot sort command.\n", stderr);
		return 1;
	}
	openxr_ui::render_result move_down_result;
	openxr_ui::render_result move_up_result;
	if (!click(renderer, render_target.Get(), snapshot, "effect/0/down", move_down_result) ||
		!click(renderer, render_target.Get(), snapshot, "effect/1/up", move_up_result))
		return 1;
	if (move_down_result.action != openxr_ui::render_result::command::move_technique || move_down_result.target_id != 1 || move_down_result.state ||
		move_up_result.action != openxr_ui::render_result::command::move_technique || move_up_result.target_id != 2 || !move_up_result.state)
	{
		std::fputs("Technique arrows did not produce distinct down and up commands.\n", stderr);
		return 1;
	}
	openxr_ui::render_result move_up_first_result;
	if (!click(renderer, render_target.Get(), snapshot, "effect/0/up", move_up_first_result))
		return 1;
	if (move_up_first_result.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Top technique exposed an invalid move-up command.\n", stderr);
		return 1;
	}
	openxr_ui::render_input disabled_input;
	disabled_input.shaders_temporarily_disabled = true;
	openxr_ui::render_result disabled_move_down_result;
	if (!click(renderer, render_target.Get(), snapshot, "effect/0/down", disabled_move_down_result, disabled_input))
		return 1;
	if (disabled_move_down_result.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Technique ordering remained interactive while shaders were temporarily disabled.\n", stderr);
		return 1;
	}
	openxr_ui::technique_snapshot active_first_snapshot = snapshot;
	openxr_ui::move_enabled_techniques_to_top(active_first_snapshot);
	if (!expect_command(renderer, render_target.Get(), active_first_snapshot, "effect/0/toggle",
		openxr_ui::render_result::command::toggle_technique, 2))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "effect/0/toggle",
		openxr_ui::render_result::command::toggle_technique, 1))
		return 1;

	openxr_ui::technique_snapshot favourite_snapshot = snapshot;
	favourite_snapshot.techniques.front().favourite = true;
	openxr_ui::render_result unfavourite_result;
	openxr_ui::render_result favourite_result;
	if (!click(renderer, render_target.Get(), favourite_snapshot, "effect/0/star", unfavourite_result) ||
		!click(renderer, render_target.Get(), snapshot, "effect/0/star", favourite_result))
		return 1;
	if (unfavourite_result.action != openxr_ui::render_result::command::set_technique_favourite ||
		unfavourite_result.target_id != 1 || unfavourite_result.state ||
		favourite_result.action != openxr_ui::render_result::command::set_technique_favourite ||
		favourite_result.target_id != 1 || !favourite_result.state)
	{
		std::fputs("Technique star did not produce distinct favourite and unfavourite commands.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), favourite_snapshot, "filter/1/segment"))
		return 1;
	if (!expect_command(renderer, render_target.Get(), favourite_snapshot, "effect/0/toggle",
		openxr_ui::render_result::command::toggle_technique, 1))
		return 1;
	// The Starred view of a snapshot with no favourites has no first row, so its old position is clicked instead.
	float first_row_x = 0.0f;
	float first_row_y = 0.0f;
	if (!renderer.find_control("effect/0/toggle", first_row_x, first_row_y))
	{
		std::fputs("Control \"effect/0/toggle\" was not drawn.\n", stderr);
		return 1;
	}
	openxr_ui::technique_snapshot no_favourites_snapshot = snapshot;
	if (click_at(renderer, render_target.Get(), no_favourites_snapshot, first_row_x, first_row_y).action != openxr_ui::render_result::command::none)
	{
		std::fputs("Starred view displayed a non-favourite technique.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), favourite_snapshot, "filter/0/segment"))
		return 1;

	openxr_ui::technique_snapshot hidden_snapshot = snapshot;
	hidden_snapshot.techniques.front().hidden = true;
	renderer.render(render_target.Get(), hidden_snapshot, warm_up);
	if (!expect_command(renderer, render_target.Get(), hidden_snapshot, "effect/0/toggle",
		openxr_ui::render_result::command::toggle_technique, 2))
		return 1;
	if (!click(renderer, render_target.Get(), hidden_snapshot, "filter/2/segment"))
		return 1;
	// The Hidden view lists only hidden effects, and its Hide button reads Show.
	openxr_ui::render_result unhide_result;
	if (!click(renderer, render_target.Get(), hidden_snapshot, "effect/0/hide", unhide_result))
		return 1;
	if (unhide_result.action != openxr_ui::render_result::command::set_technique_hidden || unhide_result.target_id != 1 || unhide_result.state)
	{
		std::fputs("Hidden view did not produce a Show command for the hidden technique.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), hidden_snapshot, "filter/0/segment"))
		return 1;
	openxr_ui::render_result hide_result;
	if (!click(renderer, render_target.Get(), snapshot, "effect/0/hide", hide_result))
		return 1;
	if (hide_result.action != openxr_ui::render_result::command::set_technique_hidden || hide_result.target_id != 1 || !hide_result.state)
	{
		std::fputs("Technique Hide button did not produce a persistent hide command.\n", stderr);
		return 1;
	}
	if (!expect_command(renderer, render_target.Get(), snapshot, "close",
		openxr_ui::render_result::command::hide_panel, 0))
		return 1;

	const openxr_ui::render_input joystick_down = { false, 0.0f, 0.0f, false, false, false, false, -1.0f };
	renderer.render(render_target.Get(), snapshot, joystick_down);
	if (!expect_command(renderer, render_target.Get(), snapshot, "effect/0/toggle",
		openxr_ui::render_result::command::toggle_technique, 2))
		return 1;
	const openxr_ui::render_input joystick_up = { false, 0.0f, 0.0f, false, false, false, false, 1.0f };
	renderer.render(render_target.Get(), snapshot, joystick_up);
	if (!expect_command(renderer, render_target.Get(), snapshot, "effect/0/toggle",
		openxr_ui::render_result::command::toggle_technique, 1))
		return 1;
	// The rest of the row, away from its buttons, also toggles.
	if (!expect_command(renderer, render_target.Get(), snapshot, "effect/0/technique",
		openxr_ui::render_result::command::toggle_technique, 1))
		return 1;

	// The scroll bar drags from its top to its bottom; the pointer drifting over an Open button must not click it.
	float bar_x = 0.0f;
	float bar_top = 0.0f;
	float bar_middle = 0.0f;
	float bar_bottom = 0.0f;
	float open_x = 0.0f;
	float open_y = 0.0f;
	if (!scroll_bar_point(renderer, "technique_scroll", 0.0f, bar_x, bar_top) ||
		!scroll_bar_point(renderer, "technique_scroll", 0.5f, bar_x, bar_middle) ||
		!scroll_bar_point(renderer, "technique_scroll", 1.0f, bar_x, bar_bottom) ||
		!renderer.find_control("effect/3/open", open_x, open_y))
	{
		std::fputs("The technique scroll bar or an Open button was not drawn.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input scroll_press = { true, bar_x, bar_top, true, false, true };
	renderer.render(render_target.Get(), snapshot, scroll_press);
	const openxr_ui::render_input scroll_crosses_row = { true, open_x, open_y, true, false, true };
	renderer.render(render_target.Get(), snapshot, scroll_crosses_row);
	const openxr_ui::render_result drag_lock_result = renderer.render(render_target.Get(), snapshot, scroll_crosses_row);
	if (drag_lock_result.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Technique scrollbar drag did not retain exclusive interaction capture.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input scroll_drag = { true, bar_x, bar_bottom, false, false, true };
	renderer.render(render_target.Get(), snapshot, scroll_drag);
	const openxr_ui::render_input release = { true, bar_x, bar_middle, false, false, false, true };
	renderer.render(render_target.Get(), snapshot, release);
	const openxr_ui::render_input held_drift_over_open = { true, open_x, open_y, true, false, true };
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
	if (!expect_command(renderer, render_target.Get(), snapshot, "effect/7/toggle",
		openxr_ui::render_result::command::toggle_technique, 11))
	{
		std::fputs("Scrolled technique row did not toggle.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), snapshot, "effect/7/open"))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "technique_on",
		openxr_ui::render_result::command::toggle_technique, 11))
	{
		std::fputs("Detail header This effect on switch did not toggle.\n", stderr);
		return 1;
	}

	if (!click(renderer, render_target.Get(), snapshot, "back"))
		return 1;
	render_layout(renderer, render_target.Get(), snapshot, {});
	if (!scroll_bar_point(renderer, "technique_scroll", 1.0f, bar_x, bar_bottom))
	{
		std::fputs("The technique scroll bar was not drawn after returning from the effect page.\n", stderr);
		return 1;
	}
	const openxr_ui::render_input scroll_back_press = { true, bar_x, bar_bottom, true, false, true };
	renderer.render(render_target.Get(), snapshot, scroll_back_press);
	const openxr_ui::render_input scroll_back_drag = { true, bar_x, 0.0f, false, false, true };
	renderer.render(render_target.Get(), snapshot, scroll_back_drag);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	if (!click(renderer, render_target.Get(), snapshot, "effect/0/open"))
		return 1;
	// The first parameter's slider track is where the value runs from 0 to 1; click a quarter of the way along it.
	render_layout(renderer, render_target.Get(), snapshot, {});
	openxr_ui::panel_renderer::control_rect slider_rect;
	if (!find_rect(renderer, "param/0/slider", slider_rect))
		return 1;
	const float slider_quarter_x = slider_rect.min_x + 8.0f + (slider_rect.max_x - slider_rect.min_x - 16.0f) * 0.25f;
	const float slider_three_quarters_x = slider_rect.min_x + 8.0f + (slider_rect.max_x - slider_rect.min_x - 16.0f) * 0.75f;
	const float slider_y = (slider_rect.min_y + slider_rect.max_y) * 0.5f;
	if (!expect_result(click_at(renderer, render_target.Get(), snapshot, slider_quarter_x, slider_y),
		openxr_ui::render_result::command::set_parameter, 100, 0, 0.25))
		return 1;
	const openxr_ui::render_input value_drag = { true, slider_three_quarters_x, slider_y, false, false, true };
	const openxr_ui::render_result dragged_value = renderer.render(render_target.Get(), snapshot, value_drag);
	if (dragged_value.action != openxr_ui::render_result::command::set_parameter || dragged_value.target_id != 100 ||
		std::abs(dragged_value.value - 0.75) >= 0.0001)
	{
		std::fprintf(stderr, "Numeric slider did not update while select remained held: action %u, target %llu, value %.6f.\n",
			static_cast<unsigned>(dragged_value.action), static_cast<unsigned long long>(dragged_value.target_id), dragged_value.value);
		return 1;
	}
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	if (!expect_command(renderer, render_target.Get(), snapshot, "param/0/minus10",
		openxr_ui::render_result::command::set_parameter, 100, 0, -0.5) ||
		!expect_command(renderer, render_target.Get(), snapshot, "param/0/minus",
		openxr_ui::render_result::command::set_parameter, 100, 0, 0.4) ||
		!expect_command(renderer, render_target.Get(), snapshot, "param/0/plus",
		openxr_ui::render_result::command::set_parameter, 100, 0, 0.6) ||
		!expect_command(renderer, render_target.Get(), snapshot, "param/0/plus10",
		openxr_ui::render_result::command::set_parameter, 100, 0, 1.5))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "param/0/reset",
		openxr_ui::render_result::command::reset_parameter, 100))
		return 1;
	openxr_ui::technique_snapshot default_value_snapshot = snapshot;
	default_value_snapshot.parameters.front().values = default_value_snapshot.parameters.front().default_values;
	openxr_ui::render_result reset_at_default;
	if (!click(renderer, render_target.Get(), default_value_snapshot, "param/0/reset", reset_at_default))
		return 1;
	if (reset_at_default.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Parameter reset remained interactive when the value already matched its default.\n", stderr);
		return 1;
	}
	// The second parameter is the On/Off one; its switch sits at the right end of the step-button column.
	if (!expect_command(renderer, render_target.Get(), snapshot, "param/1/switch",
		openxr_ui::render_result::command::set_parameter, 101, 0, 1.0))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "param/1/reset",
		openxr_ui::render_result::command::reset_parameter, 101))
		return 1;
	// Reset this effect needs two presses: the first only arms it, the second sends the command.
	if (!expect_command(renderer, render_target.Get(), snapshot, "reset_all",
		openxr_ui::render_result::command::none, 0))
	{
		std::fputs("The first Reset this effect press sent a command instead of only arming.\n", stderr);
		return 1;
	}
	if (!expect_command(renderer, render_target.Get(), snapshot, "reset_all",
		openxr_ui::render_result::command::reset_all_parameters, 1))
		return 1;
	// Sending the command disarms it again.
	if (!expect_command(renderer, render_target.Get(), snapshot, "reset_all",
		openxr_ui::render_result::command::none, 0))
		return 1;
	renderer.render(render_target.Get(), snapshot, warm_up);
	openxr_ui::render_result disabled_revert;
	if (!click(renderer, render_target.Get(), snapshot, "revert_restore", disabled_revert))
		return 1;
	if (disabled_revert.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Revert remained interactive when the technique matched its saved values.\n", stderr);
		return 1;
	}
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	openxr_ui::technique_snapshot changed_snapshot = snapshot;
	changed_snapshot.techniques.front().differs_from_saved = true;
	if (!expect_command(renderer, render_target.Get(), changed_snapshot, "revert_restore",
		openxr_ui::render_result::command::revert_technique, 1))
		return 1;
	openxr_ui::technique_snapshot restorable_snapshot = snapshot;
	restorable_snapshot.techniques.front().has_restore_state = true;
	if (!expect_command(renderer, render_target.Get(), restorable_snapshot, "revert_restore",
		openxr_ui::render_result::command::restore_technique, 1))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "save",
		openxr_ui::render_result::command::save_preset, 0))
		return 1;

	renderer.render(render_target.Get(), snapshot, joystick_down);
	renderer.render(render_target.Get(), snapshot, warm_up);
	renderer.render(render_target.Get(), snapshot, joystick_down);
	if (!expect_command(renderer, render_target.Get(), snapshot, "param/0/plus",
		openxr_ui::render_result::command::set_parameter, 102, 0, 0.6))
	{
		std::fputs("Joystick did not scroll shader parameters by row.\n", stderr);
		return 1;
	}
	float parameter_bar_x = 0.0f;
	float parameter_bar_top = 0.0f;
	float parameter_bar_bottom = 0.0f;
	if (!scroll_bar_point(renderer, "parameter_scroll", 0.0f, parameter_bar_x, parameter_bar_top) ||
		!scroll_bar_point(renderer, "parameter_scroll", 1.0f, parameter_bar_x, parameter_bar_bottom))
		return 1;
	const openxr_ui::render_input parameter_scroll_press = { true, parameter_bar_x, parameter_bar_top, true, false, true };
	renderer.render(render_target.Get(), snapshot, parameter_scroll_press);
	const openxr_ui::render_input parameter_scroll_drag = { true, parameter_bar_x, parameter_bar_bottom, false, false, true };
	renderer.render(render_target.Get(), snapshot, parameter_scroll_drag);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	renderer.render(render_target.Get(), snapshot, release);
	if (!expect_command(renderer, render_target.Get(), snapshot, "param/0/plus",
		openxr_ui::render_result::command::set_parameter, 108, 0, 0.6))
	{
		std::fputs("Shader parameter scrollbar did not scroll to the final row offset.\n", stderr);
		return 1;
	}

	if (!click(renderer, render_target.Get(), snapshot, "back"))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "expand",
		openxr_ui::render_result::command::toggle_panel_size, 0))
		return 1;
	if (!click(renderer, render_target.Get(), snapshot, "tab/1/segment"))
		return 1;
	// "Controller buttons" and "Keyboard key" are the first two rows; each list opens below its box.
	openxr_ui::render_result menu_button_opened;
	if (!click(renderer, render_target.Get(), snapshot, "menu_button", menu_button_opened))
		return 1;
	if (menu_button_opened.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Controller menu drop-down changed the setting instead of opening its list.\n", stderr);
		return 1;
	}
	openxr_ui::render_result menu_button_result;
	if (!click(renderer, render_target.Get(), snapshot, dropdown_item("menu_button", openxr_ui::menu_button_b_and_y), menu_button_result))
		return 1;
	if (menu_button_result.action != openxr_ui::render_result::command::set_menu_button ||
		menu_button_result.value != static_cast<double>(openxr_ui::menu_button_b_and_y))
	{
		std::fputs("Controller menu drop-down did not pick B + Y from its list.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), snapshot, "menu_key"))
		return 1;
	openxr_ui::render_result menu_key_result;
	if (!click(renderer, render_target.Get(), snapshot, dropdown_item("menu_key", openxr_ui::menu_key_off), menu_key_result))
		return 1;
	if (menu_key_result.action != openxr_ui::render_result::command::set_menu_key ||
		menu_key_result.value != static_cast<double>(openxr_ui::menu_key_off))
	{
		std::fputs("Keyboard menu drop-down did not pick Off from its list.\n", stderr);
		return 1;
	}
	openxr_ui::render_input menu_button_off_input;
	menu_button_off_input.menu_button = openxr_ui::menu_button_off;
	if (!click(renderer, render_target.Get(), snapshot, "menu_key", menu_button_off_input))
		return 1;
	openxr_ui::render_result menu_key_off_without_button;
	if (!click(renderer, render_target.Get(), snapshot, dropdown_item("menu_key", openxr_ui::menu_key_off), menu_key_off_without_button, menu_button_off_input))
		return 1;
	if (menu_key_off_without_button.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Keyboard menu drop-down allowed Off while the controller shortcut is also Off.\n", stderr);
		return 1;
	}
	// The gestures have their own tab.
	if (!click(renderer, render_target.Get(), snapshot, "tab/2/segment"))
		return 1;
	openxr_ui::render_result gesture_opened;
	if (!click(renderer, render_target.Get(), snapshot, "preset_gesture", gesture_opened))
		return 1;
	if (gesture_opened.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Head gesture drop-down changed the setting instead of opening its list.\n", stderr);
		return 1;
	}
	openxr_ui::render_result gesture_result;
	if (!click(renderer, render_target.Get(), snapshot, dropdown_item("preset_gesture", openxr_ui::preset_gesture_right_trigger), gesture_result))
		return 1;
	if (gesture_result.action != openxr_ui::render_result::command::set_preset_gesture ||
		gesture_result.value != static_cast<double>(openxr_ui::preset_gesture_right_trigger))
	{
		std::fputs("Head gesture drop-down did not pick Right Trigger from its list.\n", stderr);
		return 1;
	}
	// L3+R3 (the last row) cannot be picked while the menu shortcut is also L3+R3; with another shortcut it can.
	if (!click(renderer, render_target.Get(), snapshot, "preset_gesture"))
		return 1;
	openxr_ui::render_result gesture_sticks_blocked;
	if (!click(renderer, render_target.Get(), snapshot, dropdown_item("preset_gesture", openxr_ui::preset_gesture_both_sticks), gesture_sticks_blocked))
		return 1;
	if (gesture_sticks_blocked.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Next preset drop-down picked L3+R3 while L3+R3 opens the menu.\n", stderr);
		return 1;
	}
	openxr_ui::render_input menu_button_other_input;
	menu_button_other_input.menu_button = openxr_ui::menu_button_b_and_y;
	if (!click(renderer, render_target.Get(), snapshot, "preset_gesture", menu_button_other_input))
		return 1;
	openxr_ui::render_result gesture_sticks_result;
	if (!click(renderer, render_target.Get(), snapshot, dropdown_item("preset_gesture", openxr_ui::preset_gesture_both_sticks), gesture_sticks_result, menu_button_other_input))
		return 1;
	if (gesture_sticks_result.action != openxr_ui::render_result::command::set_preset_gesture ||
		gesture_sticks_result.value != static_cast<double>(openxr_ui::preset_gesture_both_sticks))
	{
		std::fputs("Next preset drop-down did not pick L3+R3 while the menu uses another shortcut.\n", stderr);
		return 1;
	}
	openxr_ui::render_result arm_swing_result;
	if (!click(renderer, render_target.Get(), snapshot, "arm_swing_walk", arm_swing_result))
		return 1;
	if (arm_swing_result.action != openxr_ui::render_result::command::set_arm_swing_walk || !arm_swing_result.state)
	{
		std::fputs("Walk by swinging arms switch did not turn the setting on.\n", stderr);
		return 1;
	}
	{
		openxr_ui::panel_renderer::control_rect strength_rect;
		render_layout(renderer, render_target.Get(), snapshot, {});
		if (!find_rect(renderer, "arm_swing_strength", strength_rect))
			return 1;
		// The right end of the track (inset 8 px like the drawn track) is the largest strength.
		const openxr_ui::render_result strength_result = click_at(renderer, render_target.Get(), snapshot,
			strength_rect.max_x - 8.0f, (strength_rect.min_y + strength_rect.max_y) * 0.5f);
		if (strength_result.action != openxr_ui::render_result::command::set_arm_swing_strength ||
			strength_result.value != static_cast<double>(openxr_ui::arm_swing_strength_max))
		{
			std::fprintf(stderr, "Swing strength slider did not pick %u%% at its right end (action %u, value %.1f).\n",
				openxr_ui::arm_swing_strength_max, static_cast<unsigned>(strength_result.action), strength_result.value);
			return 1;
		}
		openxr_ui::render_input strength_release;
		strength_release.select_released = true;
		for (int frame = 0; frame < 4; ++frame)
			renderer.render(render_target.Get(), snapshot, strength_release);
	}
	if (!click(renderer, render_target.Get(), snapshot, "arm_swing_run"))
		return 1;
	openxr_ui::render_result run_result;
	if (!click(renderer, render_target.Get(), snapshot, dropdown_item("arm_swing_run", openxr_ui::arm_swing_run_hold_y), run_result))
		return 1;
	if (run_result.action != openxr_ui::render_result::command::set_arm_swing_run ||
		run_result.value != static_cast<double>(openxr_ui::arm_swing_run_hold_y))
	{
		std::fputs("Run drop-down did not pick Hold Y, the last choice in its list.\n", stderr);
		return 1;
	}
	{
		openxr_ui::panel_renderer::control_rect run_at_rect;
		render_layout(renderer, render_target.Get(), snapshot, {});
		if (!find_rect(renderer, "arm_swing_run_at", run_at_rect))
			return 1;
		// The left end of the track is the lowest value.
		const openxr_ui::render_result run_at_result = click_at(renderer, render_target.Get(), snapshot,
			run_at_rect.min_x + 8.0f, (run_at_rect.min_y + run_at_rect.max_y) * 0.5f);
		if (run_at_result.action != openxr_ui::render_result::command::set_arm_swing_run_at ||
			run_at_result.value != static_cast<double>(openxr_ui::arm_swing_run_at_min))
		{
			std::fprintf(stderr, "Run at slider did not pick %u%% at its left end (action %u, value %.1f).\n",
				openxr_ui::arm_swing_run_at_min, static_cast<unsigned>(run_at_result.action), run_at_result.value);
			return 1;
		}
		openxr_ui::render_input run_at_release;
		run_at_release.select_released = true;
		for (int frame = 0; frame < 4; ++frame)
			renderer.render(render_target.Get(), snapshot, run_at_release);
	}
	openxr_ui::render_result block_result;
	if (!click(renderer, render_target.Get(), snapshot, "arm_swing_block_stick", block_result))
		return 1;
	if (block_result.action != openxr_ui::render_result::command::set_arm_swing_block_stick || !block_result.state)
	{
		std::fputs("Block left stick forward switch did not turn the setting on.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), snapshot, "tab/1/segment"))
		return 1;
	openxr_ui::render_input starts_open_input;
	starts_open_input.ui_starts_open = true;
	openxr_ui::render_result starts_open_result;
	if (!click(renderer, render_target.Get(), snapshot, "starts_open", starts_open_result, starts_open_input))
		return 1;
	if (starts_open_result.action != openxr_ui::render_result::command::set_ui_starts_open || starts_open_result.state)
	{
		std::fputs("UI starts open checkbox did not produce its persisted closed-state command.\n", stderr);
		return 1;
	}
	openxr_ui::render_input performance_input;
	performance_input.ui_starts_open = true;
	openxr_ui::render_result performance_result;
	if (!click(renderer, render_target.Get(), snapshot, "performance_mode", performance_result, performance_input))
		return 1;
	if (performance_result.action != openxr_ui::render_result::command::set_performance_mode || !performance_result.state)
	{
		std::fputs("Performance mode checkbox did not produce its persisted next-start command.\n", stderr);
		return 1;
	}
	openxr_ui::render_result statistics_result;
	if (!click(renderer, render_target.Get(), snapshot, "always_show_statistics", statistics_result))
		return 1;
	if (statistics_result.action != openxr_ui::render_result::command::set_always_show_statistics || !statistics_result.state)
	{
		std::fputs("Always show FPS checkbox did not produce its persisted command.\n", stderr);
		return 1;
	}
	// The theme list is in the Look card; the third item is Slate.
	openxr_ui::render_result theme_opened;
	if (!click(renderer, render_target.Get(), snapshot, "theme", theme_opened))
		return 1;
	if (theme_opened.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Theme drop-down changed the setting instead of opening its list.\n", stderr);
		return 1;
	}
	openxr_ui::render_result theme_result;
	if (!click(renderer, render_target.Get(), snapshot, dropdown_item("theme", 2), theme_result))
		return 1;
	if (theme_result.action != openxr_ui::render_result::command::set_theme || theme_result.value != 2.0)
	{
		std::fputs("Theme drop-down did not pick Slate from its list.\n", stderr);
		return 1;
	}
	openxr_ui::render_result frame_rate_opened;
	if (!click(renderer, render_target.Get(), snapshot, "frame_rate_divisor", frame_rate_opened))
		return 1;
	if (frame_rate_opened.action != openxr_ui::render_result::command::none)
	{
		std::fputs("FPS target drop-down changed the setting instead of opening its list.\n", stderr);
		return 1;
	}
	// The third item is "One-third headset rate".
	// The Faster shaders and depth rows sit under the list, so a pick that leaked through would toggle one of them.
	openxr_ui::render_result frame_rate_result;
	if (!click(renderer, render_target.Get(), snapshot, dropdown_item("frame_rate_divisor", 2), frame_rate_result))
		return 1;
	if (frame_rate_result.action != openxr_ui::render_result::command::set_frame_rate_divisor || frame_rate_result.value != 3.0)
	{
		std::fputs("FPS target drop-down did not pick third rate from its list.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), snapshot, "frame_rate_divisor"))
		return 1;
	// The click lands on the "Open when the game starts" row, outside the open list, which must only close the list.
	openxr_ui::render_result frame_rate_dismiss;
	if (!click(renderer, render_target.Get(), snapshot, "starts_open", frame_rate_dismiss))
		return 1;
	if (frame_rate_dismiss.action != openxr_ui::render_result::command::none)
	{
		std::fputs("A click outside the open FPS target list reached the control underneath.\n", stderr);
		return 1;
	}
	openxr_ui::render_result depth_result;
	if (!click(renderer, render_target.Get(), snapshot, "depth_buffer", depth_result))
		return 1;
	if (depth_result.action != openxr_ui::render_result::command::set_depth_detection_disabled || !depth_result.state)
	{
		std::fputs("Depth detection checkbox did not produce its persisted command.\n", stderr);
		return 1;
	}
	openxr_ui::render_result lock_result;
	if (!click(renderer, render_target.Get(), snapshot, "pin", lock_result))
		return 1;
	if (lock_result.action != openxr_ui::render_result::command::toggle_panel_lock)
	{
		std::fputs("Pin button did not produce its command.\n", stderr);
		return 1;
	}
	// Back on the Effects tab, the subfolder notice pushes the list down.
	if (!click(renderer, render_target.Get(), snapshot, "tab/0/segment"))
		return 1;
	openxr_ui::render_input subfolders_input;
	subfolders_input.effect_subfolders = { openxr_ui::effect_subfolder_state::offered, 42, 8 };
	openxr_ui::render_result subfolders_result;
	if (!click(renderer, render_target.Get(), snapshot, "subfolders_load", subfolders_result, subfolders_input))
		return 1;
	if (subfolders_result.action != openxr_ui::render_result::command::include_effect_subfolders)
	{
		std::fputs("Load subfolders button did not produce its command.\n", stderr);
		return 1;
	}
	openxr_ui::render_input subfolders_unsaved = subfolders_input;
	subfolders_unsaved.preset_dirty = true;
	if (!click(renderer, render_target.Get(), snapshot, "subfolders_load", subfolders_result, subfolders_unsaved))
		return 1;
	if (subfolders_result.action != openxr_ui::render_result::command::include_effect_subfolders)
	{
		std::fputs("Load anyway did not work while the preset had unsaved changes.\n", stderr);
		return 1;
	}
	openxr_ui::render_input subfolders_disabled = subfolders_input;
	subfolders_disabled.shaders_temporarily_disabled = true;
	if (!click(renderer, render_target.Get(), snapshot, "subfolders_load", subfolders_result, subfolders_disabled))
		return 1;
	if (subfolders_result.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Load subfolders button worked while all shaders were temporarily disabled.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), snapshot, "subfolders_not_now", subfolders_result, subfolders_input))
		return 1;
	if (subfolders_result.action != openxr_ui::render_result::command::dismiss_effect_subfolder_notice)
	{
		std::fputs("Not now button did not dismiss the subfolder notice.\n", stderr);
		return 1;
	}
	openxr_ui::render_input subfolders_ok = subfolders_input;
	subfolders_ok.effect_subfolders.state = openxr_ui::effect_subfolder_state::loaded;
	if (!click(renderer, render_target.Get(), snapshot, "subfolders_ok", subfolders_result, subfolders_ok))
		return 1;
	if (subfolders_result.action != openxr_ui::render_result::command::dismiss_effect_subfolder_notice)
	{
		std::fputs("OK button did not close the subfolder result notice.\n", stderr);
		return 1;
	}
	// The loading notice has no button, so click where the OK button was.
	float notice_button_x = 0.0f;
	float notice_button_y = 0.0f;
	if (!renderer.find_control("subfolders_ok", notice_button_x, notice_button_y))
	{
		std::fputs("Control \"subfolders_ok\" was not drawn.\n", stderr);
		return 1;
	}
	openxr_ui::render_input subfolders_loading = subfolders_ok;
	subfolders_loading.effect_subfolders.state = openxr_ui::effect_subfolder_state::loading;
	if (click_at(renderer, render_target.Get(), snapshot, notice_button_x, notice_button_y, subfolders_loading).action != openxr_ui::render_result::command::none)
	{
		std::fputs("The subfolder notice offered a button while effects were loading.\n", stderr);
		return 1;
	}
	subfolders_loading.effect_subfolders.loading_seconds = 12;
	if (click_at(renderer, render_target.Get(), openxr_ui::technique_snapshot {}, notice_button_x, notice_button_y, subfolders_loading).action != openxr_ui::render_result::command::none)
	{
		std::fputs("The loading notice with an empty effect list returned a command.\n", stderr);
		return 1;
	}
	// The remaining notices also have no button, so click the same spot.
	openxr_ui::render_input waiting_for_reshade;
	waiting_for_reshade.effect_runtime_available = false;
	waiting_for_reshade.effect_runtime_wait_seconds = 7;
	if (click_at(renderer, render_target.Get(), openxr_ui::technique_snapshot {}, notice_button_x, notice_button_y, waiting_for_reshade).action != openxr_ui::render_result::command::none)
	{
		std::fputs("The waiting-for-ReShade notice returned a command.\n", stderr);
		return 1;
	}
	waiting_for_reshade.reshade_loaded = false;
	if (click_at(renderer, render_target.Get(), openxr_ui::technique_snapshot {}, notice_button_x, notice_button_y, waiting_for_reshade).action != openxr_ui::render_result::command::none)
	{
		std::fputs("The ReShade-not-loaded notice returned a command.\n", stderr);
		return 1;
	}
	openxr_ui::render_input depth_unavailable;
	depth_unavailable.depth_effects_unavailable = true;
	if (click_at(renderer, render_target.Get(), snapshot, notice_button_x, notice_button_y, depth_unavailable).action != openxr_ui::render_result::command::none)
	{
		std::fputs("The UEVR depth notice returned a command.\n", stderr);
		return 1;
	}
	if (!expect_command(renderer, render_target.Get(), snapshot, "previous_preset",
		openxr_ui::render_result::command::previous_preset, 0) ||
		!expect_command(renderer, render_target.Get(), snapshot, "next_preset",
		openxr_ui::render_result::command::next_preset, 0))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "save",
		openxr_ui::render_result::command::save_preset, 0))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "delete_preset",
		openxr_ui::render_result::command::delete_preset, 0))
		return 1;
	openxr_ui::render_input open_save_as;
	open_save_as.preset_name = "Hubris Super Resolution Tuned Copy 12";
	openxr_ui::render_result open_save_as_result;
	if (!click(renderer, render_target.Get(), snapshot, "save_as", open_save_as_result, open_save_as))
		return 1;
	if (open_save_as_result.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Save As header button bypassed the naming dialog.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), snapshot, "key/clear", open_save_as) ||
		!click(renderer, render_target.Get(), snapshot, "key/H", open_save_as) ||
		!click(renderer, render_target.Get(), snapshot, "key/I", open_save_as))
		return 1;
	openxr_ui::render_result create_preset;
	if (!click(renderer, render_target.Get(), snapshot, "key/create", create_preset, open_save_as))
		return 1;
	if (create_preset.action != openxr_ui::render_result::command::save_preset_as || create_preset.text != "HI")
	{
		std::fputs("Save As keyboard did not emit the entered preset name.\n", stderr);
		return 1;
	}
	renderer.notify_save_as_started(true);
	if (!expect_command(renderer, render_target.Get(), snapshot, "effects_on",
		openxr_ui::render_result::command::toggle_all_techniques, 0))
		return 1;
	openxr_ui::render_input temporarily_disabled_input;
	temporarily_disabled_input.ui_starts_open = true;
	temporarily_disabled_input.shaders_temporarily_disabled = true;
	openxr_ui::render_result temporarily_disabled_save;
	if (!click(renderer, render_target.Get(), snapshot, "save", temporarily_disabled_save, temporarily_disabled_input))
		return 1;
	if (temporarily_disabled_save.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Save remained interactive while shaders were temporarily disabled.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), snapshot, "delete_preset", temporarily_disabled_save, temporarily_disabled_input))
		return 1;
	if (temporarily_disabled_save.action != openxr_ui::render_result::command::none)
	{
		std::fputs("Delete remained interactive while shaders were temporarily disabled.\n", stderr);
		return 1;
	}
	openxr_ui::render_result restore_all_result;
	if (!click(renderer, render_target.Get(), snapshot, "effects_on", restore_all_result, temporarily_disabled_input))
		return 1;
	if (restore_all_result.action != openxr_ui::render_result::command::toggle_all_techniques)
	{
		std::fputs("Restore shaders did not remain interactive during temporary disable.\n", stderr);
		return 1;
	}
	// Tabs: Settings controls answer on the Settings tab only, and the Effects tab comes back unchanged.
	// The spots where the other tab's controls were drawn are kept to check that they stay dead.
	float effect_row_x = 0.0f;
	float effect_row_y = 0.0f;
	render_layout(renderer, render_target.Get(), snapshot, {});
	if (!find_point(renderer, "effect/0/toggle", effect_row_x, effect_row_y))
		return 1;
	if (!click(renderer, render_target.Get(), snapshot, "tab/1/segment"))
		return 1;
	float statistics_x = 0.0f;
	float statistics_y = 0.0f;
	render_layout(renderer, render_target.Get(), snapshot, {});
	if (!find_point(renderer, "always_show_statistics", statistics_x, statistics_y))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "always_show_statistics",
		openxr_ui::render_result::command::set_always_show_statistics, 0))
	{
		std::fputs("Settings tab controls did not respond after clicking the Settings tab.\n", stderr);
		return 1;
	}
	if (!expect_command_at(renderer, render_target.Get(), snapshot, effect_row_x, effect_row_y,
		openxr_ui::render_result::command::none, 0))
	{
		std::fputs("An effect row responded on the Settings tab.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), snapshot, "tab/0/segment"))
		return 1;
	if (!expect_command_at(renderer, render_target.Get(), snapshot, statistics_x, statistics_y,
		openxr_ui::render_result::command::none, 0) ||
		!expect_command(renderer, render_target.Get(), snapshot, "effect/0/toggle",
		openxr_ui::render_result::command::toggle_technique, 1))
	{
		std::fputs("Clicking the Effects tab did not bring the effect list back.\n", stderr);
		return 1;
	}

	// Filters: All = not hidden, Starred = starred and not hidden, Hidden = hidden only.
	openxr_ui::technique_snapshot filter_snapshot = snapshot;
	filter_snapshot.techniques[0].hidden = true;
	filter_snapshot.techniques[1].hidden = true;
	filter_snapshot.techniques[1].favourite = true;
	filter_snapshot.techniques[2].favourite = true;
	filter_snapshot.techniques[4].favourite = true;
	renderer.render(render_target.Get(), filter_snapshot, warm_up);
	if (!expect_command(renderer, render_target.Get(), filter_snapshot, "effect/0/toggle",
		openxr_ui::render_result::command::toggle_technique, 3) ||
		!expect_command(renderer, render_target.Get(), filter_snapshot, "effect/1/toggle",
		openxr_ui::render_result::command::toggle_technique, 4))
	{
		std::fputs("The All view did not skip hidden effects.\n", stderr);
		return 1;
	}
	float empty_row_x = 0.0f;
	float empty_row_y = 0.0f;
	if (!click(renderer, render_target.Get(), filter_snapshot, "filter/1/segment"))
		return 1;
	if (!expect_command(renderer, render_target.Get(), filter_snapshot, "effect/0/toggle",
		openxr_ui::render_result::command::toggle_technique, 3) ||
		!expect_command(renderer, render_target.Get(), filter_snapshot, "effect/1/toggle",
		openxr_ui::render_result::command::toggle_technique, 5) ||
		!below_second_row(renderer, empty_row_x, empty_row_y) ||
		!expect_command_at(renderer, render_target.Get(), filter_snapshot, empty_row_x, empty_row_y,
		openxr_ui::render_result::command::none, 0))
	{
		std::fputs("The Starred view did not list exactly the starred effects that are not hidden.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), filter_snapshot, "filter/2/segment"))
		return 1;
	if (!expect_command(renderer, render_target.Get(), filter_snapshot, "effect/0/toggle",
		openxr_ui::render_result::command::toggle_technique, 1) ||
		!expect_command(renderer, render_target.Get(), filter_snapshot, "effect/1/toggle",
		openxr_ui::render_result::command::toggle_technique, 2) ||
		!expect_command_at(renderer, render_target.Get(), filter_snapshot, empty_row_x, empty_row_y,
		openxr_ui::render_result::command::none, 0))
	{
		std::fputs("The Hidden view did not list exactly the hidden effects.\n", stderr);
		return 1;
	}
	// In the Hidden view the button reads Show, so it sends "not hidden"; in the other views it sends "hidden".
	openxr_ui::render_result show_result;
	if (!click(renderer, render_target.Get(), filter_snapshot, "effect/1/hide", show_result))
		return 1;
	if (show_result.action != openxr_ui::render_result::command::set_technique_hidden || show_result.target_id != 2 || show_result.state)
	{
		std::fputs("The Hidden view button did not send Show for the second hidden effect.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), filter_snapshot, "filter/0/segment"))
		return 1;
	openxr_ui::render_result hide_third_result;
	if (!click(renderer, render_target.Get(), filter_snapshot, "effect/0/hide", hide_third_result))
		return 1;
	if (hide_third_result.action != openxr_ui::render_result::command::set_technique_hidden || hide_third_result.target_id != 3 || !hide_third_result.state)
	{
		std::fputs("The All view button did not send Hide for the first visible effect.\n", stderr);
		return 1;
	}

	// One press of "Active on top" sends one command; the frame after it sends nothing.
	float active_first_x = 0.0f;
	float active_first_y = 0.0f;
	if (!expect_command(renderer, render_target.Get(), snapshot, "active_first",
		openxr_ui::render_result::command::move_active_to_top, 0) ||
		!find_point(renderer, "active_first", active_first_x, active_first_y))
		return 1;
	openxr_ui::render_input active_first_held;
	active_first_held.pointer_visible = true;
	active_first_held.pointer_x = active_first_x;
	active_first_held.pointer_y = active_first_y;
	if (renderer.render(render_target.Get(), snapshot, active_first_held).action != openxr_ui::render_result::command::none)
	{
		std::fputs("Active on top did not send exactly one move_active_to_top per press.\n", stderr);
		return 1;
	}

	// Reset this effect: armed state does not survive leaving the effect page.
	if (!click(renderer, render_target.Get(), snapshot, "effect/0/open"))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "reset_all",
		openxr_ui::render_result::command::none, 0))
	{
		std::fputs("The first Reset this effect press sent a command.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), snapshot, "back") ||
		!click(renderer, render_target.Get(), snapshot, "effect/0/open"))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "reset_all",
		openxr_ui::render_result::command::none, 0) ||
		!expect_command(renderer, render_target.Get(), snapshot, "reset_all",
		openxr_ui::render_result::command::reset_all_parameters, 1))
	{
		std::fputs("Reset this effect stayed armed after leaving the effect page.\n", stderr);
		return 1;
	}
	if (!click(renderer, render_target.Get(), snapshot, "back"))
		return 1;

	// The pointer is matched against the previous frame's panel size, so the panel is drawn expanded once before the
	// click below the compact height, as happens in the headset.
	openxr_ui::render_input expanded_input;
	expanded_input.expanded = true;
	renderer.render(render_target.Get(), snapshot, expanded_input);
	if (!click(renderer, render_target.Get(), snapshot, "effect/10/open", expanded_input))
		return 1;
	if (!expect_command(renderer, render_target.Get(), snapshot, "technique_on",
		openxr_ui::render_result::command::toggle_technique, 11, 0, 0.0, true))
		return 1;
	// Expanded: the bottom bar and Save move down with the panel (906 tall instead of 650).
	if (!expect_command(renderer, render_target.Get(), snapshot, "save",
		openxr_ui::render_result::command::save_preset, 0, 0, 0.0, true))
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

	openxr_ui::frame_statistics_tracker cpu_tracker;
	std::chrono::steady_clock::time_point cpu_frame_time;
	for (int index = 0; index <= 100; ++index)
	{
		cpu_frame_time += std::chrono::milliseconds(10);
		cpu_tracker.add_frame(cpu_frame_time, 0.004);
	}
	const openxr_ui::frame_statistics cpu_statistics = cpu_tracker.statistics();
	if (statistics.cpu_reported || !cpu_statistics.cpu_reported || std::abs(cpu_statistics.cpu_ms - 6.0f) > 0.1f)
	{
		std::fprintf(stderr, "CPU time was %.2f ms (reported %d); frames without a wait time reported %d.\n",
			cpu_statistics.cpu_ms, cpu_statistics.cpu_reported, statistics.cpu_reported);
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
	statistics_renderer.render_statistics(statistics_target.Get(), cpu_statistics);

	std::puts("Private ImGui named preset save/delete controls, sorting, shader hiding, global favourites, close control, full-row toggles, drag scrolling, sliders, expansion, parameter controls, and the FPS counter passed.");
	return 0;
}
