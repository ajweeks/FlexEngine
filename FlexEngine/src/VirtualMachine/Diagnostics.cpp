#pragma once

#include "VirtualMachine/Diagnostics.hpp"

namespace flex
{
    void DiagnosticContainer::AddDiagnostic(Span span, u32 lineNumber, u32 columnIndex, const std::string& message)
    {
        AddDiagnostic({ span, lineNumber, columnIndex, message });
    }

    void DiagnosticContainer::AddDiagnostic(const Diagnostic& diagnostic)
    {
        diagnostics.push_back(diagnostic);
    }
} // namespace flex
