#pragma once

#include <string>
#include <stdio.h> // For va_list
#include "wtypes.h" // For HANDLE, WORD

namespace flex
{
	void Print(const char* str, ...);
	void PrintWarn(const char* str, ...);
	void PrintError(const char* str, ...);
	// Call when results are expected to be larger than MAX_CHARS
	void PrintLong(const char* str, ...);
	void Print(const char* str, va_list argList);

	void GetConsoleHandle();

	static const int MAX_CHARS = 1024;

	extern bool g_bEnableLogToConsole;

#if _WIN32
	extern HANDLE g_ConsoleHandle;
	const WORD CONSOLE_COLOR_DEFAULT = 0 | FOREGROUND_INTENSITY;
	const WORD CONSOLE_COLOR_WARNING = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
	const WORD CONSOLE_COLOR_ERROR = FOREGROUND_RED | FOREGROUND_INTENSITY;
#endif
} // namespace flex
