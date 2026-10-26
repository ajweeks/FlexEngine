#pragma once

namespace flex
{
	class Time
	{
	public:
		enum class Format
		{
			SECOND =		1,
			MILLISECOND =	1000,
			MICROSECOND =	1000000,
			NANOSECOND =	1000000000
		};

		// NOTE: These return absolute times as floats, which lose precision as the session gets longer (after 4.6 hours
		// a float holding seconds only has ~2ms resolution). Use NowTicks & MillisecondsSince to measure durations.
		static sec CurrentSeconds();
		static us CurrentMicoseconds();
		static ms CurrentMilliseconds();
		static ns CurrentNanoseconds();

		// Precise monotonic time in nanoseconds (for measuring durations)
		static u64 NowTicks();
		// Milliseconds elapsed since startTicks (from NowTicks)
		static ms MillisecondsSince(u64 startTicks);

		static real ConvertFormats(real value, Format from, Format to);

		static constexpr real ConvertFormatsConstexpr(real value, Format from, Format to)
		{
			return value * ((real)to / (real)from);
		}
	};
} // namespace flex
