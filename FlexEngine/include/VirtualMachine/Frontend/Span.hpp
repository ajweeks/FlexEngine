#pragma once

namespace flex
{
	struct Span
	{
		Span(u32 low, u32 high) :
			low(low),
			high(high)
		{
			assert(low <= high);
		}

		std::string ToString(const std::string& source)
		{
			return source.substr(low, high - low);
		}

		Span Clip()
		{
			return Span(high, high);
		}

		Span Shrink()
		{
			return Span(low, high - 1);
		}

		Span Grow()
		{
			return Span(low, high + 1);
		}

		i32 low;
		i32 high;
	};
} // namespace flex
