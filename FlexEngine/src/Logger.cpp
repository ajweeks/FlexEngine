#include "stdafx.hpp"

#include <iostream>
#include <cstdio> // For fprintf, ...

#include "Helpers.hpp"
#include "Platform/Platform.hpp"

namespace flex
{
	bool g_bEnableLogToConsole = true;
	// TODO: Use StringBuilder
	std::stringstream g_LogBuffer;
	const char* g_LogBufferFilePath;

	void InitializeLogger()
	{
		g_LogBufferFilePath = SAVED_DIRECTORY "flex.log";

		ClearLogFile();

		if (g_bEnableLogToConsole)
		{
			g_LogBuffer << '[' << Platform::GetDateString_YMDHMS() << ']' << '\n';
		}
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

		Platform::SetConsoleTextColour(Platform::ConsoleColour::DEFAULT);

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

		Platform::SetConsoleTextColour(Platform::ConsoleColour::WARNING);

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

		Platform::SetConsoleTextColour(Platform::ConsoleColour::ERROR);

		va_list argList;
		va_start(argList, str);

		Print(str, argList);

		va_end(argList);
	}

	void PrintLong(const char* str)
	{
		if (!g_bEnableLogToConsole)
		{
			return;
		}

		Platform::SetConsoleTextColour(Platform::ConsoleColour::DEFAULT);

		PrintSimple(str);
	}

	void PrintWarnLong(const char* str)
	{
		if (!g_bEnableLogToConsole)
		{
			return;
		}

		Platform::SetConsoleTextColour(Platform::ConsoleColour::WARNING);

		PrintSimple(str);
	}

	void PrintErrorLong(const char* str)
	{
		if (!g_bEnableLogToConsole)
		{
			return;
		}

		Platform::SetConsoleTextColour(Platform::ConsoleColour::ERROR);

		PrintSimple(str);
	}

	//
	// Private functions
	//

	void Print(const char* str, va_list argList)
	{
		if (strlen(str) == 0)
		{
			std::cout << "\n";
			Platform::PrintStringToDebuggerConsole("\n");
		}
		else
		{
			char buffer[MAX_CHARS];

			vsnprintf(buffer, MAX_CHARS, str, argList);

			std::string s(buffer);
			s[s.size() - 1] = '\n';
			// TODO: Make thread-safe
			g_LogBuffer << s;

			std::cout << buffer;

			Platform::PrintStringToDebuggerConsole(s.c_str());
		}
	}

	void PrintSimple(const char* str)
	{
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
} // namespace flex
