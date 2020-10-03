#pragma once

#include "VirtualMachine/Frontend/Span.hpp"

namespace flex
{
	struct Diagnostic
	{
		Diagnostic(Span span, const std::string& message) :
			span(span),
			message(message)
		{
		}

		void ComputeLineColumnIndicesFromSource(const std::vector<std::string>& sourceLines);

		Span span;
		std::string message;
		// TODO: Add line number/column index end for multiline support
		u32 lineNumber = 0;
		u32 columnIndex = 0;
	};

	struct DiagnosticContainer
	{
		void AddDiagnostic(Span span, const std::string& message);
		void AddDiagnostic(const Diagnostic& diagnostic);
		void ComputeLineColumnIndicesFromSource(const std::string& source);
		std::vector<Diagnostic> diagnostics;

	};
} // namespace flex
