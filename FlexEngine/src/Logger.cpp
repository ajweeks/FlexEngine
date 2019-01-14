#include "stdafx.hpp"

#include <fstream>
#include <iostream>

#include "Helpers.hpp"

namespace flex
{
#ifdef _WIN32
	HANDLE g_ConsoleHandle;
#endif

	bool g_bEnableLogToConsole = true;

	void InitializeLogger()
	{
#ifdef _WIN32
		g_ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(g_ConsoleHandle, CONSOLE_COLOR_DEFAULT);
#endif

		g_LogBufferFilePath = ROOT_LOCATION "saved/FlexEngine.log";

		ClearLogFile();

		g_LogBuffer << '[' << GetDateString_YMDHMS() << ']' << '\n';
	}

	void ClearLogFile()
	{
		std::ofstream file(g_LogBufferFilePath);
		if (file)
		{
			char c = '\0';
			file.write(&c, 1);
			file.close();
		}
	}

	void SaveLogBufferToFile()
	{
		// TODO: Only append new content rather than overwriting old content?
		std::ofstream file(g_LogBufferFilePath, std::ios::trunc);
		if (file)
		{
			std::string fileContents(g_LogBuffer.str());
			file.write(fileContents.data(), fileContents.size());
			file.close();
		}
		else
		{
			PrintWarn("Failed to save log to %s\n", g_LogBufferFilePath);
		}
	}

	void DestroyLogger()
	{
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

		std::string s(s_buffer);
		s[s.size()-1] = '\n';
		g_LogBuffer << s;

		std::cout << s_buffer;
	}
} // namespace flex
