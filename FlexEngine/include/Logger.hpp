#pragma once

#include <sstream>
#include <stdio.h> // For va_list

#include "Types.hpp"

namespace flex
{
	void PrintSimple(const char* str);
	void Print(FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST;
	void PrintWarn(FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST;
	void PrintError(FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST;
	// Call when results are expected to be larger than MAX_CHARS
	void PrintLong(const char* str);
	void Print(const char* str, va_list argList);

	void InitializeLogger();
	void ClearLogFile();
	void SaveLogBufferToFile();

	// Max number of characters allowed in a single message
	static const int MAX_CHARS = 1024;

	extern bool g_bEnableLogToConsole;

} // namespace flex
