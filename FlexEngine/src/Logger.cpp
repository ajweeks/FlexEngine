#include "stdafx.hpp"

#include <iostream>
#include <cstdio> // For fprintf, ...

#include "Helpers.hpp"
#include "Platform/Platform.hpp"

namespace flex
{
	bool g_bEnableLogToConsole = true;
	std::stringstream g_LogBuffer;
	const char* g_LogBufferFilePath;

	void InitializeLogger()
	{
		g_LogBufferFilePath = SAVED_LOCATION "flex.log";

		ClearLogFile();

		g_LogBuffer << '[' << Platform::GetDateString_YMDHMS() << ']' << '\n';
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

		Platform::SetConsoleTextColor(Platform::ConsoleColour::DEFAULT);

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

		Platform::SetConsoleTextColor(Platform::ConsoleColour::WARNING);

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

		Platform::SetConsoleTextColor(Platform::ConsoleColour::ERROR);

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
			PrintSimple(str + i);
		}
		PrintSimple(str + len - len % MAX_CHARS);
	}

	void PrintSimple(const char* str)
	{
		if (!g_bEnableLogToConsole)
		{
			return;
		}

		Platform::SetConsoleTextColor(Platform::ConsoleColour::DEFAULT);

		if (strlen(str) == 0)
		{
			std::cout << "\n";
			Platform::PrintStringToDebuggerConsole("\n");
		}
		else
		{
			std::cout << str;

			Platform::PrintStringToDebuggerConsole(str);
		}
	}

	void Print(const char* str, va_list argList)
	{
		if (strlen(str) == 0)
		{
			std::cout << "\n";
			Platform::PrintStringToDebuggerConsole("\n");
		}
		else
		{
			static char s_buffer[MAX_CHARS];

			vsnprintf(s_buffer, MAX_CHARS, str, argList);

			std::string s(s_buffer);
			s[s.size() - 1] = '\n';
			g_LogBuffer << s;

			std::cout << s_buffer;

			Platform::PrintStringToDebuggerConsole(s.c_str());
		}
	}
} // namespace flex
