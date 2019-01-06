#include "stdafx.hpp"

#include <iostream>

namespace flex
{
#ifdef _WIN32
	HANDLE g_ConsoleHandle;
#endif

	bool g_bEnableLogToConsole = true;

	void GetConsoleHandle()
	{
#ifdef _WIN32
		g_ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(g_ConsoleHandle, CONSOLE_COLOR_DEFAULT);
#endif
	}

	void Print(const char* str, ...)
	{
		if (!g_bEnableLogToConsole)
		{
			return;
		}

		SetConsoleTextAttribute(g_ConsoleHandle, CONSOLE_COLOR_DEFAULT);

		va_list argList;
		va_start(argList, str);

		Print(str, argList);

		va_end(argList);
	}

	void PrintWarn(const char* str, ...)
	{
		if (!g_bEnableLogToConsole)
		{
			return;
		}

		SetConsoleTextAttribute(g_ConsoleHandle, CONSOLE_COLOR_WARNING);

		va_list argList;
		va_start(argList, str);

		Print(str, argList);

		va_end(argList);
	}

	void PrintError(const char* str, ...)
	{
		if (!g_bEnableLogToConsole)
		{
			return;
		}

		SetConsoleTextAttribute(g_ConsoleHandle, CONSOLE_COLOR_ERROR);

		va_list argList;
		va_start(argList, str);

		Print(str, argList);

		va_end(argList);
	}

	void PrintLong(const char* str, ...)
	{
		i32 len = strlen(str);
		for (i32 i = 0; i < len; i += MAX_CHARS)
		{
			Print(str + i);
		}
		Print(str + len - len % MAX_CHARS);
	}

	void Print(const char* str, va_list argList)
	{
		if (!g_bEnableLogToConsole)
		{
			return;
		}

		static char s_buffer[MAX_CHARS];

		vsnprintf(s_buffer, MAX_CHARS, str, argList);

		std::cout << s_buffer;
	}
} // namespace flex
