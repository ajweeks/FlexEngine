#include "stdafx.hpp"

#include "Time.hpp"

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
} // namespace flex
