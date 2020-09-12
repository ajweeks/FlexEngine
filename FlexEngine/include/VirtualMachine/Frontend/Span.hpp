#pragma once

#include "Types.hpp"

#include <string>

namespace flex
{
	struct Span
	{
		enum class Source
		{
			INPUT,
			GENERATED,

			_NONE
		};

		Span(u32 low, u32 high) :
			low(low),
			high(high)
		{
		}

		Span(Source source) :
			low(0),
			high(0),
			source(source)
		{
		}

		std::string ToString(const std::string& inSource) const
		{
			return inSource.substr(low, high - low);
		}

		Span Clip() const
		{
			return Span(high, high);
		}

		Span Shrink() const
		{
			return Span(low, high - 1);
		}

		Span Grow() const
		{
			return Span(low, high + 1);
		}

		u32 Length() const
		{
			return high - low;
		}

		Span Extend(const Span& other) const
		{
			return Span(low, other.high);
		}

		i32 low;
		i32 high;
		Source source = Source::INPUT;
	};
} // namespace flex
