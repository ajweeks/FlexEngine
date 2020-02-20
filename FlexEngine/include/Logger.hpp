#pragma once

#include <sstream>
#include <stdio.h> // For va_list

#include "Types.hpp"

typedef void * HANDLE;
typedef unsigned short WORD;

// Taken from ntddvdeo.h:
#define FOREGROUND_BLUE      0x0001
#define FOREGROUND_GREEN     0x0002
#define FOREGROUND_RED       0x0004
#define FOREGROUND_INTENSITY 0x0008
#define BACKGROUND_BLUE      0x0010
#define BACKGROUND_GREEN     0x0020
#define BACKGROUND_RED       0x0040
#define BACKGROUND_INTENSITY 0x0080

namespace flex
{
	void Print(const char* str, ...) FORMAT_STRING(1,2);
	void PrintWarn(const char* str, ...) FORMAT_STRING(1,2);
	void PrintError(const char* str, ...) FORMAT_STRING(1,2);
	// Call when results are expected to be larger than MAX_CHARS
	void PrintLong(const char* str, ...) FORMAT_STRING(1,2);
	void Print(const char* str, va_list argList);

	void InitializeLogger();
	void ClearLogFile();
	void SaveLogBufferToFile();

	void SetConsoleTextColor(u64 color);
	void OutputDebugString(const char* str);

	// Max number of characters allowed in a single message
	static const int MAX_CHARS = 1024;

	extern bool g_bEnableLogToConsole;

	const WORD CONSOLE_COLOR_DEFAULT = 0 | FOREGROUND_INTENSITY;
	const WORD CONSOLE_COLOR_WARNING = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
	const WORD CONSOLE_COLOR_ERROR = FOREGROUND_RED | FOREGROUND_INTENSITY;

#if _WIN32
	extern HANDLE g_ConsoleHandle;
#endif
} // namespace flex
