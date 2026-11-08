#pragma once

#include <algorithm>

namespace openxr_ui
{
	// Panel pixels per screen pixel of mouse movement.
	inline constexpr float mouse_pointer_speed = 1.0f;

	struct mouse_reading
	{
		// The add-on holds the mouse for the menu: the menu is shown and the game is in front.
		bool connected = false;
		// Movement since the previous reading, in screen pixels; down is positive.
		float dx = 0.0f;
		float dy = 0.0f;
		bool left_down = false;
		// Wheel notches since the previous reading; positive is away from the player.
		int wheel = 0;
	};

	struct mouse_frame
	{
		// The mouse drives the panel pointer this frame.
		bool pointer_active = false;
		bool input_active = false;
		float pointer_x = 0.0f;
		float pointer_y = 0.0f;
		bool select_down = false;
		bool select_clicked = false;
		bool select_released = false;
		float scroll_y = 0.0f;
	};

	// Moving the mouse moves a pointer over the panel, the left button clicks or drags, and each wheel notch scrolls
	// one row. The pointer appears in the panel centre on the first movement, click or wheel turn while the menu is
	// shown; a click that only wakes it does not click.
	class mouse_pointer
	{
	public:
		mouse_frame update(const mouse_reading &reading, bool panel_visible, float width, float height)
		{
			mouse_frame frame;
			if (!reading.connected || !panel_visible)
			{
				*this = {};
				return frame;
			}

			const bool moved = reading.dx != 0.0f || reading.dy != 0.0f;
			const bool pressed = reading.left_down && !_left_was_down;
			_left_was_down = reading.left_down;
			_pending_wheel += reading.wheel;
			if (!_active)
			{
				if (!moved && !pressed && reading.wheel == 0)
					return frame;
				_active = true;
				_x = width * 0.5f;
				_y = height * 0.5f;
				_ignore_left_until_release = reading.left_down;
			}

			// The menu moves one row per scroll pulse and needs a frame without one in between.
			float scroll = 0.0f;
			if (_pending_wheel != 0 && !_scrolled_last_frame)
			{
				scroll = _pending_wheel > 0 ? 1.0f : -1.0f;
				_pending_wheel += _pending_wheel > 0 ? -1 : 1;
			}
			_scrolled_last_frame = scroll != 0.0f;

			_x = std::clamp(_x + reading.dx * mouse_pointer_speed, 0.0f, width - 1.0f);
			_y = std::clamp(_y + reading.dy * mouse_pointer_speed, 0.0f, height - 1.0f);
			if (!reading.left_down)
				_ignore_left_until_release = false;
			const bool select = reading.left_down && !_ignore_left_until_release;
			frame.input_active = moved || reading.left_down || scroll != 0.0f || _select_down;
			frame.pointer_active = true;
			frame.pointer_x = _x;
			frame.pointer_y = _y;
			frame.select_clicked = select && !_select_down;
			frame.select_released = !select && _select_down;
			frame.select_down = select;
			frame.scroll_y = scroll;
			_select_down = select;
			return frame;
		}

	private:
		bool _active = false;
		bool _left_was_down = false;
		bool _select_down = false;
		bool _ignore_left_until_release = false;
		bool _scrolled_last_frame = false;
		int _pending_wheel = 0;
		float _x = 0.0f;
		float _y = 0.0f;
	};
}
