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

	inline bool gamepad_menu_pressed(const gamepad_reading &reading, uint32_t menu_button)
	{
		const uint16_t chord = gamepad_menu_buttons(menu_button);
		return reading.connected && chord != 0 && (reading.buttons & chord) == chord;
	}

	struct gamepad_frame
	{
		// The gamepad drives the panel pointer this frame.
		bool pointer_active = false;
		float pointer_x = 0.0f;
		float pointer_y = 0.0f;
		bool select_down = false;
		bool select_clicked = false;
		bool select_released = false;
		float scroll_y = 0.0f;
	};

	// Left stick moves a pointer over the panel, A clicks, and the right stick scrolls. The pointer appears on the
	// first stick push or A press while the menu is shown. The menu shortcut is handled by menu_shortcut_press.
	class gamepad_pointer
	{
	public:
		gamepad_frame update(const gamepad_reading &reading, float seconds, bool panel_visible, float width, float height)
		{
			gamepad_frame frame;
			if (!reading.connected)
			{
				*this = {};
				return frame;
			}

			const bool a_pressed = (reading.buttons & gamepad_a) != 0;
			const bool a_was_pressed = _a_was_pressed;
			_a_was_pressed = a_pressed;
			if (!panel_visible)
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
		bool _a_was_pressed = false;
		bool _active = false;
		bool _select_down = false;
		bool _ignore_a_until_release = false;
		float _x = 0.0f;
		float _y = 0.0f;
	};
}
