#include "stdafx.hpp"

#include <iostream>
#include <cstdio> // For fprintf, ...

#include "Helpers.hpp"

namespace flex
{
#ifdef _WIN32
	HANDLE g_ConsoleHandle;
#endif

	bool g_bEnableLogToConsole = true;
	std::stringstream g_LogBuffer;
	const char* g_LogBufferFilePath;

	void InitializeLogger()
	{
#ifdef _WIN32
		g_ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(g_ConsoleHandle, CONSOLE_COLOR_DEFAULT);
#endif

		g_LogBufferFilePath = SAVED_LOCATION "flex.log";

		ClearLogFile();

		g_LogBuffer << '[' << GetDateString_YMDHMS() << ']' << '\n';
	}

	void ClearLogFile()
	{
		FILE* f = fopen(g_LogBufferFilePath, "w");

		if (f)
		{
			fprintf(f, "%c", '\0');
			fclose(f);
		}
	}

	void SaveLogBufferToFile()
	{
		// TODO: Only append new content rather than overwriting old content?

		FILE* f = fopen(g_LogBufferFilePath, "w");

		if (f)
		{
			std::string fileContents(g_LogBuffer.str());
			fprintf(f, "%s", fileContents.c_str());
			fclose(f);
		}
	}

	void Print(const char* str, ...)
	{
		if (!g_bEnableLogToConsole)
		{
			return;
		}

		SetConsoleTextColor(CONSOLE_COLOR_DEFAULT);

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

		SetConsoleTextColor(CONSOLE_COLOR_WARNING);

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

		SetConsoleTextColor(CONSOLE_COLOR_ERROR);

		va_list argList;
		va_start(argList, str);

		Print(str, argList);

		va_end(argList);
	}

	void PrintLong(const char* str)
	{
		i32 len = strlen(str);
		for (i32 i = 0; i < len; i += MAX_CHARS)
		{
			Print(str + i, "");
		}
		Print(str + len - len % MAX_CHARS, "");
	}

	void Print(const char* str, va_list argList)
	{
		if (strlen(str) == 0)
		{
			std::cout << "\n";
			OutputDebugString("\n");
		}
		else
		{
			static char s_buffer[MAX_CHARS];

			vsnprintf(s_buffer, MAX_CHARS, str, argList);

			std::string s(s_buffer);
			s[s.size() - 1] = '\n';
			g_LogBuffer << s;

			std::cout << s_buffer;

			// TODO: Disable in shipping
			OutputDebugString(s.c_str());
		}
	}

	void SetConsoleTextColor(flex::u64 color)
	{
#ifdef _WIN32
		SetConsoleTextAttribute(g_ConsoleHandle, color);
#endif
	}

	void OutputDebugString(const char* str)
	{
#ifdef _WIN32
		::OutputDebugString(str);
#endif
	}


} // namespace flex
