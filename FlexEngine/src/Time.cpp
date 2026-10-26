#include "stdafx.hpp"

#include "Time.hpp"

namespace flex
{
	sec Time::CurrentSeconds()
	{
		// TODO: Replace
		return (sec)glfwGetTime();
	}

	// Converted from double (rather than from CurrentSeconds) to avoid compounding precision loss
	us Time::CurrentMicoseconds()
	{
		return (us)(glfwGetTime() * 1e6);
	}

	ms Time::CurrentMilliseconds()
	{
		return (ms)(glfwGetTime() * 1e3);
	}

	ns Time::CurrentNanoseconds()
	{
		return (ns)(glfwGetTime() * 1e9);
	}

	u64 Time::NowTicks()
	{
		return (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	ms Time::MillisecondsSince(u64 startTicks)
	{
		const u64 now = NowTicks();
		return now > startTicks ? (ms)((double)(now - startTicks) / 1e6) : 0.0f;
	}

	real Time::ConvertFormats(real value, Format from, Format to)
	{
		return value * ((real)to / (real)from);
	}
} // namespace flex
