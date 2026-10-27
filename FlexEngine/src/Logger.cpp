#include "stdafx.hpp"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <cstdio> // For fprintf, ...
#include <mutex>

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

	// Serializes access to g_LogBuffer, the console colour, stdout and the debugger console
	// so that concurrent calls to Print/PrintWarn/PrintError from multiple threads (e.g. the
	// main thread queuing texture loads while worker threads log load completions) do not
	// race and corrupt the underlying buffers.
	static std::recursive_mutex g_LogMutex;

	// Guarded by g_LogMutex
	static std::vector<LogSink*> g_LogSinks;
	// Allows skipping the lock & formatting entirely when no sinks are registered (the common case)
	static std::atomic<bool> g_bHasLogSinks = false;

	//
	// File-private function declarations
	//
	void Print(const char* str, va_list argList);
	void PrintSimple(const char* str);
	void DispatchToSinks(LogLevel level, const char* str, va_list argList);
	void DispatchToSinks(LogLevel level, const char* str);

	void InitializeLogger()
	{
		g_LogBufferFilePath = SAVED_DIRECTORY "flex.log";

		ClearLogFile();
	}

	// How much of g_LogBuffer is in the log file (guarded by g_LogMutex)
	static std::streamoff s_SavedLogLength = 0;

	void ClearLogFile()
	{
		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		FILE* f = fopen(g_LogBufferFilePath, "w");

		if (f != nullptr)
		{
			fclose(f);
		}
		// The whole buffer is written on the next save
		s_SavedLogLength = 0;
	}

	void SaveLogBufferToFile()
	{
		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		// Only what was logged since the last save is appended (rewriting the whole log every few seconds gets slow in
		// long sessions)
		const std::streamoff length = (std::streamoff)g_LogBuffer.tellp();
		if (length <= s_SavedLogLength)
		{
			return;
		}

		FILE* f = fopen(g_LogBufferFilePath, s_SavedLogLength == 0 ? "w" : "a");

		if (f != nullptr)
		{
			std::string newContents((size_t)(length - s_SavedLogLength), '\0');
			g_LogBuffer.seekg(s_SavedLogLength);
			g_LogBuffer.read(&newContents[0], (std::streamsize)newContents.size());
			g_LogBuffer.clear();
			fwrite(newContents.data(), 1, newContents.size(), f);
			fclose(f);
			s_SavedLogLength = length;
		}
	}

	void Print(const char* str, ...)
	{
		{
			va_list sinkArgList;
			va_start(sinkArgList, str);
			DispatchToSinks(LogLevel::MESSAGE, str, sinkArgList);
			va_end(sinkArgList);
		}

		if (!g_bEnableLogToConsole)
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		Platform::SetConsoleTextColour(Platform::ConsoleColour::DEFAULT);

		va_list argList;
		va_start(argList, str);

		Print(str, argList);

		va_end(argList);
	}

	void PrintWarn(const char* str, ...)
	{
		{
			va_list sinkArgList;
			va_start(sinkArgList, str);
			DispatchToSinks(LogLevel::WARNING, str, sinkArgList);
			va_end(sinkArgList);
		}

		if (!g_bEnableLogToConsole)
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		Platform::SetConsoleTextColour(Platform::ConsoleColour::WARNING);

		va_list argList;
		va_start(argList, str);

		Print(str, argList);

		va_end(argList);
	}

	void PrintError(const char* str, ...)
	{
		{
			va_list sinkArgList;
			va_start(sinkArgList, str);
			DispatchToSinks(LogLevel::ERROR, str, sinkArgList);
			va_end(sinkArgList);
		}

		if (!g_bEnableLogToConsole)
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

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

		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

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
		DispatchToSinks(LogLevel::MESSAGE, str);

		if (!g_bEnableLogToConsole)
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		Platform::SetConsoleTextColour(Platform::ConsoleColour::DEFAULT);

		PrintSimple(str);
	}

	void PrintColouredLong(Platform::ConsoleColour colour, const char* str)
	{
		DispatchToSinks(LogLevel::MESSAGE, str);

		if (!g_bEnableLogToConsole)
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		Platform::SetConsoleTextColour(colour);

		PrintSimple(str);
	}

	void PrintWarnLong(const char* str)
	{
		DispatchToSinks(LogLevel::WARNING, str);

		if (!g_bEnableLogToConsole)
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		Platform::SetConsoleTextColour(Platform::ConsoleColour::WARNING);

		PrintSimple(str);
	}

	void PrintErrorLong(const char* str)
	{
		DispatchToSinks(LogLevel::ERROR, str);

		if (!g_bEnableLogToConsole)
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		Platform::SetConsoleTextColour(Platform::ConsoleColour::ERROR);

		PrintSimple(str);
	}

	void AddLogSink(LogSink* sink)
	{
		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		g_LogSinks.push_back(sink);
		g_bHasLogSinks = true;
	}

	void RemoveLogSink(LogSink* sink)
	{
		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		g_LogSinks.erase(std::remove(g_LogSinks.begin(), g_LogSinks.end(), sink), g_LogSinks.end());
		g_bHasLogSinks = !g_LogSinks.empty();
	}

	//
	// File-private function definitions
	//

	void Print(const char* str, va_list argList)
	{
		// Callers in this file already hold g_LogMutex; lock here too in case this
		// file-private overload is ever invoked directly without going through the
		// public variadic wrappers above.
		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

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
			g_LogBuffer << s;

			std::cout << buffer;

			Platform::PrintStringToDebuggerConsole(s.c_str());
		}
	}

	void DispatchToSinks(LogLevel level, const char* str, va_list argList)
	{
		if (!g_bHasLogSinks.load(std::memory_order_relaxed))
		{
			return;
		}

		char buffer[MAX_CHARS];
		vsnprintf(buffer, MAX_CHARS, str, argList);

		DispatchToSinks(level, buffer);
	}

	void DispatchToSinks(LogLevel level, const char* str)
	{
		if (!g_bHasLogSinks.load(std::memory_order_relaxed))
		{
			return;
		}

		// Sinks are only ever called while holding the lock, so once RemoveLogSink returns a sink won't be called again
		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		for (LogSink* sink : g_LogSinks)
		{
			sink->OnLog(level, str);
		}
	}

	void PrintSimple(const char* str)
	{
		std::lock_guard<std::recursive_mutex> lock(g_LogMutex);

		if (strlen(str) == 0)
		{
			std::cout << "\n";
			Platform::PrintStringToDebuggerConsole("\n");
		}
		else
		{
			g_LogBuffer << str;
			std::cout << str;
			Platform::PrintStringToDebuggerConsole(str);
		}
	}
} // namespace flex
