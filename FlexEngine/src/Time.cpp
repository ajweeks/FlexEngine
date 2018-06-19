#include "stdafx.hpp"

#include "Time.hpp"

#include <iomanip>
#include <sstream>

namespace flex
{
	sec Time::CurrentSeconds()
	{
		return (sec)glfwGetTime();
	}

	us Time::CurrentMicoseconds()
	{
		return ConvertFormats(CurrentSeconds(), Format::SECOND, Format::MICROSECOND);
	}

	ms Time::CurrentMilliseconds()
	{
		return ConvertFormats(CurrentSeconds(), Format::SECOND, Format::MILLISECOND);
	}

	ns Time::CurrentNanoseconds()
	{
		return ConvertFormats(CurrentSeconds(), Format::SECOND, Format::NANOSECOND);
	}

	real Time::ConvertFormats(real value, Format from, Format to)
	{
		return value * ((real)to / (real)from);
	}

	std::string Time::SecondsToString(sec seconds, i32 precision)
	{
		std::stringstream ss;
		ss << std::fixed << std::setprecision(precision) << seconds << "s";

		return ss.str();
	}

	std::string Time::MicrosecondsToString(us microseconds, i32 precision)
	{
		std::stringstream ss;
		ss << std::fixed << std::setprecision(precision) << microseconds << "us";

		return ss.str();
	}

	std::string Time::MillisecondsToString(ms milliseconds, i32 precision)
	{
		std::stringstream ss;
		ss << std::fixed << std::setprecision(precision) << milliseconds << "ms";

		return ss.str();
	}

	std::string Time::NanosecondsToString(ns nanoseconds, i32 precision)
	{
		std::stringstream ss;
		ss << std::fixed << std::setprecision(precision) << nanoseconds << "ns";

		return ss.str();
	}
} // namespace flex
