#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace openxr_ui
{
	// Swing strength in percent; saved per game.
	inline constexpr uint32_t arm_swing_strength_min = 25;
	inline constexpr uint32_t arm_swing_strength_max = 300;
	inline constexpr uint32_t arm_swing_strength_default = 100;
	inline constexpr uint32_t arm_swing_strength_step = 5;

	inline uint32_t clamp_arm_swing_strength(uint32_t percent)
	{
		return std::clamp(percent, arm_swing_strength_min, arm_swing_strength_max);
	}

	// The button fast swings press to make the game run, on top of the forward push; saved per game. Click presses the left
	// thumbstick once to start running and once to stop, for games that toggle running; the Hold choices keep their button
	// pressed while running.
	inline constexpr uint32_t arm_swing_run_off = 0;
	inline constexpr uint32_t arm_swing_run_click = 1;
	inline constexpr uint32_t arm_swing_run_hold_a = 2;
	inline constexpr uint32_t arm_swing_run_hold_b = 3;
	inline constexpr uint32_t arm_swing_run_hold_x = 4;
	inline constexpr uint32_t arm_swing_run_hold_y = 5;
	inline constexpr uint32_t arm_swing_run_mode_count = 6;
	inline constexpr const char *arm_swing_run_names[arm_swing_run_mode_count] = {
		"Off", "Click left stick", "Hold A", "Hold B", "Hold X", "Hold Y" };

	// The controller buttons running can press. A and B are on the right controller, the others on the left.
	enum class arm_swing_run_button_id : uint32_t { left_stick, a, b, x, y, count };

	inline arm_swing_run_button_id arm_swing_run_button_for(uint32_t mode)
	{
		switch (mode)
		{
		case arm_swing_run_hold_a: return arm_swing_run_button_id::a;
		case arm_swing_run_hold_b: return arm_swing_run_button_id::b;
		case arm_swing_run_hold_x: return arm_swing_run_button_id::x;
		case arm_swing_run_hold_y: return arm_swing_run_button_id::y;
		default: return arm_swing_run_button_id::left_stick;
		}
	}

	inline bool arm_swing_run_button_on_right_hand(arm_swing_run_button_id button)
	{
		return button == arm_swing_run_button_id::a || button == arm_swing_run_button_id::b;
	}

	// Swing speed that starts running, in percent of the swing speed that walks at full speed at 100% strength.
	inline constexpr uint32_t arm_swing_run_at_min = 100;
	inline constexpr uint32_t arm_swing_run_at_max = 300;
	inline constexpr uint32_t arm_swing_run_at_default = 150;

	inline uint32_t clamp_arm_swing_run_at(uint32_t percent)
	{
		return std::clamp(percent, arm_swing_run_at_min, arm_swing_run_at_max);
	}

	// With the stick block on, the game's left thumbstick cannot walk forward by itself; backwards and sideways still work.
	inline float block_stick_forward(float stick_y)
	{
		return (std::min)(stick_y, 0.0f);
	}

	// Turns both hands moving up and down in opposite directions, as when swinging the arms while walking,
	// into a forward thumbstick value from 0 to 1. Faster swings give a larger value.
	class arm_swing_tracker
	{
	public:
		// Hand heights in metres relative to the head, and the seconds since the previous update.
		float update(float left_height, float right_height, float seconds, uint32_t strength_percent)
		{
			if (!_ready || seconds <= 0.0f)
			{
				_left_height = left_height;
				_right_height = right_height;
				_ready = true;
				return _forward;
			}
			const float left_speed = (left_height - _left_height) / seconds;
			const float right_speed = (right_height - _right_height) / seconds;
			_left_height = left_height;
			_right_height = right_height;
			// One hand going up while the other goes down; both hands moving together (reaching, crouching) is not walking.
			const float swing_speed = left_speed * right_speed < 0.0f ? (std::min)(std::abs(left_speed), std::abs(right_speed)) : 0.0f;
			// The hands slow down to a stop at the top and bottom of every swing. The top speed of the previous half-swing is
			// kept until the current one gets as fast, so a steady swing walks at a steady speed however slow it is.
			const float direction = left_speed > 0.0f ? 1.0f : (left_speed < 0.0f ? -1.0f : _direction);
			if (direction != _direction)
			{
				_direction = direction;
				// Tracking jitter around a turn flips the direction too; only a half-swing faster than the dead zone counts.
				if (_current.speed > dead_zone_speed)
				{
					_previous = _current;
					_current = {};
				}
			}
			_current.age += seconds;
			_previous.age += seconds;
			if (swing_speed >= _current.speed)
				_current = { swing_speed, 0.0f };
			const float held_speed = (std::max)(_current.held(), _previous.held());
			const float strength = static_cast<float>(clamp_arm_swing_strength(strength_percent)) / 100.0f;
			_level = (std::max)(0.0f, (held_speed - dead_zone_speed) / (full_speed - dead_zone_speed));
			_forward = (std::min)(_level * strength, 1.0f);
			return _forward;
		}

		// Swing speed without the strength setting: 1 is the speed that walks at full speed at 100% strength. Compared with the run setting.
		float level() const
		{
			return _level;
		}

		void reset()
		{
			*this = {};
		}

	private:
		// Metres per second. Below the dead zone, tracking noise and small hand movements never walk.
		static constexpr float dead_zone_speed = 0.3f;
		static constexpr float full_speed = 1.5f;
		// A top speed counts in full for this long, enough to bridge the turn of a slow swing, then fades so stopping the arms stops walking.
		static constexpr float hold_seconds = 0.9f;
		static constexpr float fade_seconds = 0.2f;

		// The top speed of one half-swing and the seconds since it was reached.
		struct swing_peak
		{
			float speed = 0.0f;
			float age = 0.0f;

			float held() const
			{
				return age <= hold_seconds ? speed : speed * std::exp(-(age - hold_seconds) / fade_seconds);
			}
		};

		bool _ready = false;
		float _left_height = 0.0f;
		float _right_height = 0.0f;
		float _direction = 0.0f;
		swing_peak _current;
		swing_peak _previous;
		float _level = 0.0f;
		float _forward = 0.0f;
	};

	// Turns the swing level into the game's run button: true while the button should read as pressed.
	class arm_swing_run_button
	{
	public:
		bool update(float level, uint32_t mode, uint32_t run_at_percent, float seconds)
		{
			const float start = static_cast<float>(clamp_arm_swing_run_at(run_at_percent)) / 100.0f;
			const bool was_running = _running;
			// Running stops a little below where it starts, so a swing speed near the setting does not flicker between the two.
			_running = mode != arm_swing_run_off && (level >= start || (_running && level >= start * stop_fraction));
			if (mode != arm_swing_run_click)
			{
				_pending_clicks = 0;
				_phase_seconds = 0.0f;
				_pressed = _running;
				return _pressed;
			}
			// A click is pressed and then released for click_seconds each, so the game sees both changes even at a low frame rate.
			_pending_clicks += _running != was_running ? 1 : 0;
			_phase_seconds -= seconds;
			if (_phase_seconds <= 0.0f)
			{
				if (_pressed)
				{
					_pressed = false;
					_phase_seconds = click_seconds;
				}
				else if (_pending_clicks > 0)
				{
					--_pending_clicks;
					_pressed = true;
					_phase_seconds = click_seconds;
				}
			}
			return _pressed;
		}

		bool running() const
		{
			return _running;
		}

	private:
		static constexpr float stop_fraction = 0.8f;
		static constexpr float click_seconds = 0.1f;

		bool _running = false;
		bool _pressed = false;
		uint32_t _pending_clicks = 0;
		float _phase_seconds = 0.0f;
	};
}
