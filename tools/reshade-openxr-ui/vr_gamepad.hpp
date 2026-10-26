#pragma once

#include "vr_menu_shortcut.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace openxr_ui
{
	// Button bits as Windows XInput reports them.
	enum gamepad_button : uint16_t
	{
		gamepad_back = 0x0020,
		gamepad_left_thumb = 0x0040,
		gamepad_right_thumb = 0x0080,
		gamepad_a = 0x1000,
		gamepad_b = 0x2000,
		gamepad_y = 0x8000,
	};

	// XInput's recommended thumbstick dead zones.
	inline constexpr int16_t gamepad_left_stick_dead_zone = 7849;
	inline constexpr int16_t gamepad_right_stick_dead_zone = 8689;
	// Panel pixels per second at full stick; the squared response leaves small pushes precise.
	inline constexpr float gamepad_pointer_speed = 800.0f;
	inline constexpr float gamepad_menu_hold_seconds = 1.5f;

	struct gamepad_reading
	{
		bool connected = false;
		uint16_t buttons = 0;
		// -1 to 1, zero inside the dead zone; up is positive.
		float left_x = 0.0f;
		float left_y = 0.0f;
		float right_y = 0.0f;
	};

	// Scales a raw XInput stick axis to -1..1 with the dead zone removed.
	inline float gamepad_stick(int16_t raw, int16_t dead_zone)
	{
		const float value = static_cast<float>(raw);
		const float magnitude = std::abs(value);
		if (magnitude <= dead_zone)
			return 0.0f;
		const float scaled = (std::min)((magnitude - dead_zone) / (32767.0f - dead_zone), 1.0f);
		return value < 0.0f ? -scaled : scaled;
	}

	// The gamepad buttons that match the controller menu shortcut; zero for Off.
	inline constexpr uint16_t gamepad_menu_buttons(uint32_t menu_button)
	{
		switch (menu_button)
		{
		case menu_button_both_sticks:
		case menu_button_hold_both_sticks:
			return gamepad_left_thumb | gamepad_right_thumb;
		case menu_button_b_and_y:
		case menu_button_hold_b_and_y:
			return gamepad_b | gamepad_y;
		case menu_button_hold_left_menu:
			return gamepad_back;
		default:
			return 0;
		}
	}

	struct gamepad_frame
	{
		bool menu_toggled = false;
		// The gamepad drives the panel pointer this frame.
		bool pointer_active = false;
		float pointer_x = 0.0f;
		float pointer_y = 0.0f;
		bool select_down = false;
		bool select_clicked = false;
		bool select_released = false;
		float scroll_y = 0.0f;
	};

	// Left stick moves a pointer over the panel, A clicks, right stick scrolls, and the menu shortcut buttons
	// show or hide the menu. The pointer appears on the first stick push or A press while the menu is shown.
	class gamepad_pointer
	{
	public:
		gamepad_frame update(const gamepad_reading &reading, float seconds, uint32_t menu_button, bool panel_visible,
			float width, float height)
		{
			gamepad_frame frame;
			if (!reading.connected)
			{
				*this = {};
				return frame;
			}

			const uint16_t chord = gamepad_menu_buttons(menu_button);
			const bool chord_pressed = chord != 0 && (reading.buttons & chord) == chord;
			if (chord_pressed && !_chord_was_pressed)
			{
				_chord_held_seconds = 0.0f;
				_chord_triggered = !menu_button_needs_hold(menu_button);
				frame.menu_toggled = _chord_triggered;
			}
			else if (chord_pressed && !_chord_triggered && (_chord_held_seconds += seconds) >= gamepad_menu_hold_seconds)
			{
				_chord_triggered = true;
				frame.menu_toggled = true;
			}
			_chord_was_pressed = chord_pressed;

			const bool a_pressed = (reading.buttons & gamepad_a) != 0;
			const bool a_was_pressed = _a_was_pressed;
			_a_was_pressed = a_pressed;
			if (!panel_visible || frame.menu_toggled)
			{
				_active = false;
				_select_down = false;
				return frame;
			}

			if (!_active)
			{
				if (reading.left_x == 0.0f && reading.left_y == 0.0f && !(a_pressed && !a_was_pressed))
					return frame;
				// A press that only wakes the pointer does not click.
				_active = true;
				_x = width * 0.5f;
				_y = height * 0.5f;
				_select_down = false;
				_ignore_a_until_release = a_pressed;
			}

			const float step = gamepad_pointer_speed * seconds;
			_x = std::clamp(_x + reading.left_x * std::abs(reading.left_x) * step, 0.0f, width - 1.0f);
			_y = std::clamp(_y - reading.left_y * std::abs(reading.left_y) * step, 0.0f, height - 1.0f);
			if (!a_pressed)
				_ignore_a_until_release = false;
			const bool select = a_pressed && !_ignore_a_until_release;
			frame.pointer_active = true;
			frame.pointer_x = _x;
			frame.pointer_y = _y;
			frame.select_clicked = select && !_select_down;
			frame.select_released = !select && _select_down;
			frame.select_down = select;
			frame.scroll_y = reading.right_y;
			_select_down = select;
			return frame;
		}

	private:
		bool _chord_was_pressed = false;
		bool _chord_triggered = false;
		float _chord_held_seconds = 0.0f;
		bool _a_was_pressed = false;
		bool _active = false;
		bool _select_down = false;
		bool _ignore_a_until_release = false;
		float _x = 0.0f;
		float _y = 0.0f;
	};
}
