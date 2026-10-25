#pragma once

#include "vr_menu_shortcut.hpp"
#include <cstdint>

namespace openxr_ui
{
	// Bring that hand up to the head and press the button to switch to the next preset, or to turn effects
	// off and on when there is only one preset. L3+R3 is a click of both thumbsticks with the hands anywhere.
	// settings.ini stores the number, so only append.
	enum preset_gesture : uint32_t
	{
		preset_gesture_off,
		preset_gesture_left_trigger,
		preset_gesture_left_grip,
		preset_gesture_left_x,
		preset_gesture_right_trigger,
		preset_gesture_right_grip,
		preset_gesture_right_a,
		preset_gesture_both_sticks,
		preset_gesture_count,
	};
	inline constexpr const char *preset_gesture_names[preset_gesture_count] = {
		"Off", "Left Trigger", "Left Grip", "Left X", "Right Trigger", "Right Grip", "Right A", "L3+R3" };

	// The L3+R3 gesture fires when the sticks are let go. It never fires when the same click also showed or hid
	// the menu: with the plain L3+R3 menu shortcut every click does, with Hold L3+R3 only a long hold does.
	inline constexpr bool stick_gesture_allowed(uint32_t menu_button, bool menu_toggled_by_this_click)
	{
		return menu_button != menu_button_both_sticks &&
			!(menu_button == menu_button_hold_both_sticks && menu_toggled_by_this_click);
	}

	enum class preset_gesture_button
	{
		none,
		trigger,
		grip,
		face,
	};

	inline constexpr bool preset_gesture_uses_left_hand(uint32_t gesture)
	{
		return gesture == preset_gesture_left_trigger || gesture == preset_gesture_left_grip || gesture == preset_gesture_left_x;
	}

	inline constexpr preset_gesture_button preset_gesture_button_for(uint32_t gesture)
	{
		switch (gesture)
		{
		case preset_gesture_left_trigger:
		case preset_gesture_right_trigger:
			return preset_gesture_button::trigger;
		case preset_gesture_left_grip:
		case preset_gesture_right_grip:
			return preset_gesture_button::grip;
		case preset_gesture_left_x:
		case preset_gesture_right_a:
			return preset_gesture_button::face;
		default:
			return preset_gesture_button::none;
		}
	}

	// Metres, in the head's own space (x right, y up, -z forward). The grip pose sits in the palm, so a hand
	// held against the temple or forehead is about 0.1 to 0.15 m away; a hand held out in front to aim is farther.
	inline constexpr float preset_gesture_head_radius = 0.22f;
	// Rejects a hand resting on the chest or shoulder, which can come close to the head from below.
	inline constexpr float preset_gesture_lowest_height = -0.12f;

	inline constexpr bool hand_near_head(float x, float y, float z)
	{
		return y >= preset_gesture_lowest_height &&
			x * x + y * y + z * z <= preset_gesture_head_radius * preset_gesture_head_radius;
	}

	// Fires once per press, only when the press starts with the hand at the head; holding the button and then
	// moving the hand to the head does not fire. The hand position is asked for only on the press frame.
	struct preset_gesture_tracker
	{
		bool was_pressed = false;

		// Returns true on the frame the button goes down.
		bool press_started(bool pressed)
		{
			const bool started = pressed && !was_pressed;
			was_pressed = pressed;
			return started;
		}

		// Returns true on the frame the button comes back up. Use either this or press_started, not both.
		bool press_ended(bool pressed)
		{
			const bool ended = !pressed && was_pressed;
			was_pressed = pressed;
			return ended;
		}
	};
}
