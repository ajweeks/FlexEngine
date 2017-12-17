#pragma once

#include <string>

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

		static sec Now();
		static real Difference(real start, real end);
		static real ConvertFormats(real value, Format from, Format to);

		static std::string SecondsToString(sec seconds, i32 precision = 1);
		static std::string MicrosecondsToString(us microseconds, i32 precision = 1);
		static std::string MillisecondsToString(ms milliseconds, i32 precision = 1);
		static std::string NanosecondsToString(ns nanoseconds, i32 precision = 1);

	private:
		Time();

	};
} // namespace flex
