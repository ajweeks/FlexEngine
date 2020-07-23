#pragma once

#include "VirtualMachine/Frontend/Span.hpp"

#include <vector>

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

	struct DiagnosticContainer
	{
		void AddDiagnostic(Span span, u32 lineNumber, u32 columnIndex, const std::string& message);
		void AddDiagnostic(const Diagnostic& diagnostic);

		std::vector<Diagnostic> diagnostics;

	};
} // namespace flex
