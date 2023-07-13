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

	// Max number of characters allowed in a single message
	static const int MAX_CHARS = 1024;

	//
	// File-private function declarations
	//
	void Print(const char* str, va_list argList);
	void PrintSimple(const char* str);

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

		if (f != nullptr)
		{
			fprintf(f, "%c", '\0');
			fclose(f);
		}
	}

	void SaveLogBufferToFile()
	{
		// TODO: Only append new content rather than overwriting old content?

		FILE* f = fopen(g_LogBufferFilePath, "w");

		if (f != nullptr)
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

	void PrintFatal(const char* file, int line, const char* str, ...)
	{
		if (!g_bEnableLogToConsole)
		{
			return;
		}

		// TODO: Make thread safe?
		//static std::mutex mutex;
		//std::lock_guard<std::mutex> lock(mutex);

		// Strip leading directories
		const char* filePath = strstr(file, "FlexEngine/");
		if (!filePath) filePath = strstr(file, "FlexEngine\\");
		std::string shortFilePath(filePath ? (filePath + 11) : file);
		PrintError("[%s:%d] ", shortFilePath.c_str(), line);

		Platform::SetConsoleTextColour(Platform::ConsoleColour::ERROR);

		va_list argList;
		va_start(argList, str);

		Print(str, argList);

		va_end(argList);

		Platform::PrintStackTrace();

		abort();
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
	// File-private function definitions
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
