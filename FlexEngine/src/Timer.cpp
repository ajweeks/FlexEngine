
#include "stdafx.hpp"

#include "Timer.hpp"

namespace flex
{
	Timer::Timer(sec duration) :
		duration(duration)
	{}

	Timer::Timer(sec duration, sec initialRemaining) :
		duration(duration),
		remaining(initialRemaining)
	{}

	bool Timer::Update()
	{
		if (remaining > 0.0f)
		{
			remaining -= bUseUnpausedDeltaTime ? g_UnpausedDeltaTime : g_DeltaTime;
			if (remaining <= 0.0f)
			{
				remaining = 0.0f;
				return true;
			}
		}

		return (remaining == 0.0f);
	}

	void Timer::Restart()
	{
		remaining = duration;
	}

	void Timer::Complete()
	{
		remaining = 0.0f;
	}
} // namespace flex