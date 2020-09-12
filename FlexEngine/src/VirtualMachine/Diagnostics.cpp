#include "stdafx.hpp"

#include "VirtualMachine/Diagnostics.hpp"

#include "Helpers.hpp"

namespace flex
{
    void Diagnostic::ComputeLineColumnIndicesFromSource(const std::vector<std::string>& sourceLines)
    {
        u32 charCount = 0;
        for (u32 i = 0; i < (u32)sourceLines.size(); ++i)
        {
            u32 pCharCount = charCount;
            charCount += (u32)sourceLines[i].length() + 1;
            if ((u32)span.low >= pCharCount && (u32)span.low < charCount)
            {
                lineNumber = i;
                columnIndex = span.low - pCharCount;
                break;
            }
        }
    }
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
            diagnostics[i].ComputeLineColumnIndicesFromSource(sourceLines);
        }
    }
} // namespace flex
