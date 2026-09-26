#pragma once

#include <sstream>
#include <stdio.h> // For va_list

#include "Types.hpp"

#undef ERROR

namespace flex
{
	void InitializeLogger();
	void ClearLogFile();
	void SaveLogBufferToFile();

	void Print(FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST(1, 2);
	void PrintWarn(FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST(1, 2);
	void PrintError(FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST(1, 2);
	void PrintFatal(const char* file, int line, FORMAT_STRING_PRE const char* str, ...) FORMAT_STRING_POST(3, 4);
	// Call when results are expected to be larger than MAX_CHARS
	void PrintLong(const char* str);
	void PrintWarnLong(const char* str);
	void PrintErrorLong(const char* str);

	enum class LogLevel
	{
		MESSAGE,
		WARNING,
		ERROR
	};

	// Receives every formatted log message, in addition to (and regardless of) console output.
	// OnLog may be called from any thread, but never concurrently.
	class LogSink
	{
	public:
		virtual ~LogSink() = default;
		virtual void OnLog(LogLevel level, const char* message) = 0;
	};

	void AddLogSink(LogSink* sink);
	void RemoveLogSink(LogSink* sink);

	extern bool g_bEnableLogToConsole;

} // namespace flex

#define PRINT_FATAL(...) \
	flex::PrintFatal(__FILE__, __LINE__, __VA_ARGS__)
