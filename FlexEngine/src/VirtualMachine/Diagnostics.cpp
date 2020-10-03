#include "stdafx.hpp"

#include "VirtualMachine/Diagnostics.hpp"

#include "Helpers.hpp"

namespace flex
{
    void DiagnosticContainer::AddDiagnostic(Span span, const std::string& message)
    {
        AddDiagnostic(Diagnostic(span, message));
    }

    void DiagnosticContainer::AddDiagnostic(const Diagnostic& diagnostic)
    {
        diagnostics.push_back(diagnostic);
    }

    void DiagnosticContainer::ComputeLineColumnIndicesFromSource(const std::string& source)
    {
        std::vector<std::string> sourceLines = SplitNoStrip(source, '\n');
        for (u32 i = 0; i < (u32)diagnostics.size(); ++i)
        {
            diagnostics[i].span.ComputeLineColumnIndicesFromSource(sourceLines);
        }
    }
} // namespace flex
