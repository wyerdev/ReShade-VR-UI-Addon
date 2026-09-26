#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>

namespace openxr_ui
{
	inline constexpr double frame_graph_seconds = 5.0;
	// Columns of about 10 ms; at higher refresh rates a column shows the slower of the frames it holds.
	inline constexpr size_t frame_graph_columns = 480;
	// Used until the headset reports its own rate.
	inline constexpr float default_refresh_hz = 90.0f;
	// The game may run at every refresh (1), every second (2) or every third (3).
	inline constexpr uint32_t max_frame_rate_divisor = 3;

	inline const char *frame_rate_divisor_name(uint32_t divisor)
	{
		return divisor == 2 ? "Half rate" : divisor == 3 ? "Third rate" : "Full rate";
	}

	struct frame_statistics
	{
		float average_fps = 0.0f;
		float frame_time_ms = 0.0f;
		float refresh_hz = default_refresh_hz;
		bool refresh_hz_reported = false;
		uint32_t frame_rate_divisor = 1;
		float target_hz() const
		{
			return refresh_hz / static_cast<float>(frame_rate_divisor);
		}
		// Slowest frame time overlapping each slice of the last frame_graph_seconds, oldest first; zero where no frame ran.
		std::array<float, frame_graph_columns> graph_ms = {};
		// Zero until the first update; increases with every update.
		uint64_t revision = 0;
	};

	class frame_statistics_tracker
	{
	public:
		static constexpr double update_seconds = 0.5;
		// A longer gap is a pause such as a loading screen or a removed headset, not a frame.
		static constexpr double pause_seconds = 1.0;

		void add_frame(std::chrono::steady_clock::time_point now)
		{
			const bool has_previous = _has_previous_frame;
			const std::chrono::duration<double> elapsed = now - _previous_frame;
			_previous_frame = now;
			_has_previous_frame = true;
			if (!has_previous || elapsed.count() <= 0.0 || elapsed.count() > pause_seconds)
				return;

			const double frame_seconds = elapsed.count();
			_frame_times.push_back({ frame_seconds, now });
			while (seconds_between(_frame_times.front().end, now) >= frame_graph_seconds)
				_frame_times.pop_front();

			++_interval_frames;
			_interval_total += frame_seconds;
			if (_interval_total < update_seconds)
				return;

			_statistics.average_fps = static_cast<float>(_interval_frames / _interval_total);
			_statistics.frame_time_ms = static_cast<float>(_interval_total * 1000.0 / _interval_frames);
			update_graph(now);
			++_statistics.revision;
			_interval_frames = 0;
			_interval_total = 0.0;
		}

		const frame_statistics &statistics() const
		{
			return _statistics;
		}

		// Takes effect with the next update; values outside a plausible headset range are ignored.
		void set_refresh_hz(float hz)
		{
			if (hz < 30.0f || hz > 500.0f)
				return;
			_statistics.refresh_hz = hz;
			_statistics.refresh_hz_reported = true;
		}

		void set_frame_rate_divisor(uint32_t divisor)
		{
			if (divisor >= 1 && divisor <= max_frame_rate_divisor)
				_statistics.frame_rate_divisor = divisor;
		}

	private:
		static double seconds_between(std::chrono::steady_clock::time_point earlier, std::chrono::steady_clock::time_point later)
		{
			return std::chrono::duration<double>(later - earlier).count();
		}

		// A long frame fills every column it spans, so a stutter is as wide as it lasted.
		void update_graph(std::chrono::steady_clock::time_point now)
		{
			constexpr double column_seconds = frame_graph_seconds / frame_graph_columns;
			_statistics.graph_ms.fill(0.0f);
			for (const recorded_frame &frame : _frame_times)
			{
				const double end_age = seconds_between(frame.end, now);
				const size_t newest = static_cast<size_t>(end_age / column_seconds);
				if (newest >= frame_graph_columns)
					continue;
				const size_t oldest = (std::min)(frame_graph_columns - 1,
					(std::max)(newest, static_cast<size_t>(std::ceil((end_age + frame.seconds) / column_seconds)) - 1));
				const float milliseconds = static_cast<float>(frame.seconds * 1000.0);
				for (size_t age = newest; age <= oldest; ++age)
				{
					float &column = _statistics.graph_ms[frame_graph_columns - 1 - age];
					column = (std::max)(column, milliseconds);
				}
			}
		}

		struct recorded_frame
		{
			double seconds;
			std::chrono::steady_clock::time_point end;
		};

		std::chrono::steady_clock::time_point _previous_frame;
		bool _has_previous_frame = false;
		std::deque<recorded_frame> _frame_times;
		uint32_t _interval_frames = 0;
		double _interval_total = 0.0;
		frame_statistics _statistics;
	};
}
