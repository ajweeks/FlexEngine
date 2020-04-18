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

		static sec CurrentSeconds();
		static us CurrentMicoseconds();
		static ms CurrentMilliseconds();
		static ns CurrentNanoseconds();

		static real ConvertFormats(real value, Format from, Format to);

		static constexpr real ConvertFormatsConstexpr(real value, Format from, Format to)
		{
			return value * ((real)to / (real)from);
		}
	};
} // namespace flex
