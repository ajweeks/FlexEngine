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

private:
	static void Log(const std::string& message, LogLevel logLevel = LogLevel::LOG_INFO);

};
