#pragma once

#include "Span.hpp"

namespace flex
{
	struct Diagnostic
	{
		Diagnostic(Span span, u32 lineNumber, u32 columnIndex, const std::string& message) :
			span(span),
			lineNumber(lineNumber),
			columnIndex(columnIndex),
			message(message)
		{
		}

		Span span;
		u32 lineNumber;
		u32 columnIndex;
		std::string message;
	};
} // namespace flex
