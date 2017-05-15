#pragma once

#include <string>

class Logger
{
public:
	enum class LogLevel
	{
		LOG_INFO, 
		LOG_WARNING,
		LOG_ERROR
	};

	static void LogInfo(const std::string& message);
	static void LogWarning(const std::string& message);
	static void LogError(const std::string& message);

	static void LogInfo(const std::wstring& message);
	static void LogWarning(const std::wstring& message);
	static void LogError(const std::wstring& message);

private:
	static void Log(const std::string& message, LogLevel logLevel = LogLevel::LOG_INFO);
	static void Log(const std::wstring& message, LogLevel logLevel = LogLevel::LOG_INFO);

};
