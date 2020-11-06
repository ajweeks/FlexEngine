#pragma once

#include <sstream>
#include <stdio.h> // For va_list

#include "Types.hpp"

namespace flex
{
	void PrintSimple(const char* str);
	void Print(const char* str, ...) FORMAT_STRING(1, 2);
	void PrintWarn(const char* str, ...) FORMAT_STRING(1,2);
	void PrintError(const char* str, ...) FORMAT_STRING(1,2);
	// Call when results are expected to be larger than MAX_CHARS
	void PrintLong(const char* str);
	void Print(const char* str, va_list argList);

	void InitializeLogger();
	void ClearLogFile();
	void SaveLogBufferToFile();

	void SetConsoleTextColour(u64 colour);

	// Max number of characters allowed in a single message
	static const int MAX_CHARS = 1024;

	extern bool g_bEnableLogToConsole;

} // namespace flex
