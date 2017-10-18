#pragma once

#include <string>

namespace flex
{
	class Logger
	{
	public:
		enum class LogLevel
		{
			LOG_INFO,
			LOG_WARNING,
			LOG_ERROR
		};

		static void LogInfo(const std::string& message, bool newline = true);
		static void LogWarning(const std::string& message, bool newline = true);
		static void LogError(const std::string& message, bool newline = true);

		static void LogInfo(const std::wstring& message, bool newline = true);
		static void LogWarning(const std::wstring& message, bool newline = true);
		static void LogError(const std::wstring& message, bool newline = true);

		static void Log(const std::string& message, LogLevel logLevel, bool newline);
		static void Log(const std::wstring& message, LogLevel logLevel, bool newline);

		static void SetLogInfo(bool logInfo);
		static void SetLogWarnings(bool logWarnings);
		static void SetLogErrors(bool logErrors);

	private:
		static bool m_LogInfo;
		static bool m_LogWarnings;
		static bool m_LogErrors;

	};
} // namespace flex
