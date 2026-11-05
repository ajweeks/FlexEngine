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
			if (source == Source::GENERATED)
			{
				low = -1;
				high = -1;
			}
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

		void ComputeLineColumnIndicesFromSource(const std::vector<std::string>& sourceLines)
		{
			u32 charCount = 0;
			for (u32 i = 0; i < (u32)sourceLines.size(); ++i)
			{
				u32 pCharCount = charCount;
				charCount += (u32)sourceLines[i].length() + 1;
				if ((u32)low >= pCharCount && (u32)low < charCount)
				{
					lineNumber = i;
					columnIndex = low - pCharCount;
					break;
				}
			}
		}

		// Same result as above (for many spans): binary searches lineStarts (see LineTable)
		void ComputeLineColumnIndices(const std::vector<u32>& lineStarts, u32 sourceLength)
		{
			if ((u32)low > sourceLength || lineStarts.empty())
			{
				return;
			}
			auto iter = std::upper_bound(lineStarts.begin(), lineStarts.end(), (u32)low);
			u32 line = (u32)(iter - lineStarts.begin()) - 1;
			lineNumber = line;
			columnIndex = (u32)low - lineStarts[line];
		}

		i32 low;
		i32 high;

		// TODO: Add line number/column index end for multiline support
		u32 lineNumber = 0;
		u32 columnIndex = 0;

		Source source = Source::INPUT;
	};
	// The offset each line of a source starts at, for looking up line numbers of spans
	struct LineTable
	{
		void Build(const std::string& source)
		{
			lineStarts.clear();
			lineStarts.push_back(0);
			for (u32 i = 0; i < (u32)source.size(); ++i)
			{
				if (source[i] == '\n')
				{
					lineStarts.push_back(i + 1);
				}
			}
			sourceLength = (u32)source.size();
		}

		void Apply(Span& span) const
		{
			span.ComputeLineColumnIndices(lineStarts, sourceLength);
		}

		std::vector<u32> lineStarts;
		u32 sourceLength = 0;
	};
} // namespace flex
