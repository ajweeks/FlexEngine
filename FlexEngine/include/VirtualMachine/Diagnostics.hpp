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

		Span span;
		std::string message;
	};

	struct DiagnosticContainer
	{
		void AddDiagnostic(Span span, const std::string& message);
		void AddDiagnostic(const Diagnostic& diagnostic);
		void ComputeLineColumnIndicesFromSource(const std::string& source);
		std::vector<Diagnostic> diagnostics;

	};
} // namespace flex
