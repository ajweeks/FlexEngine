#pragma once

#include "Types.hpp"

#include <string>
#include <cassert>

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

		Span Extend(const Span& other)
		{
			return Span(low, other.high);
		}

		i32 low;
		i32 high;
	};
} // namespace flex
