#pragma once

#include <sstream>
#include <stdio.h> // For va_list

#include "Types.hpp"

namespace flex
{
	void InitializeLogger();
	void ClearLogFile();
	void SaveLogBufferToFile();

	void Print(FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST(1, 2);
	void PrintWarn(FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST(1, 2);
	void PrintError(FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST(1, 2);
	void PrintFatal(FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST(1, 2);
	void PrintFatal(const char* file, int line, FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST(3, 4);
	// Call when results are expected to be larger than MAX_CHARS
	void PrintLong(const char* str);
	void PrintWarnLong(const char* str);
	void PrintErrorLong(const char* str);
	void PrintFatal(const char* file, int line, const char* str);

#define PRINT_FATAL(...) \
	PrintFatal(__FILE__, __LINE__, __VA_ARGS__)

	extern bool g_bEnableLogToConsole;

} // namespace flex
