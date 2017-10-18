#include "stdafx.hpp"

#include <iostream>

#include "Logger.hpp"

namespace flex
{
	bool Logger::m_LogInfo = true;
	bool Logger::m_LogWarnings = true;
	bool Logger::m_LogErrors = true;

	void Logger::Log(const std::string& message, LogLevel logLevel, bool newline)
	{
		switch (logLevel)
		{
		case Logger::LogLevel::LOG_INFO:
		{
			if (!m_LogInfo) return;

			std::cout << "[INFO] ";
		} break;
		case Logger::LogLevel::LOG_WARNING:
		{
			if (!m_LogWarnings) return;
			
			std::cout << "[WARNING] ";
		} break;
		case Logger::LogLevel::LOG_ERROR:
		{
			if (!m_LogErrors) return;

			std::cout << "[ERROR] ";
		} break;
		default:
		{
			std::cout << "UNHANDLED LOG LEVEL!: " + std::to_string(int(logLevel)) + ", message: ";
		} break;
		}

		std::cout << message;
		if (newline) std::cout << std::endl;
	}

	void Logger::Log(const std::wstring& message, LogLevel logLevel, bool newline)
	{
		switch (logLevel)
		{
		case Logger::LogLevel::LOG_INFO:
		{
			if (!m_LogInfo) return;

			std::wcout << L"[INFO] ";
		} break;
		case Logger::LogLevel::LOG_WARNING:
		{
			if (!m_LogWarnings) return;

			std::wcout << L"[WARNING] ";
		} break;
		case Logger::LogLevel::LOG_ERROR:
		{
			if (!m_LogErrors) return;

			std::wcout << L"[ERROR] ";
		} break;
		default:
		{
			std::wcout << L"UNHANDLED LOG LEVEL!: " + std::to_wstring(int(logLevel)) + L", message: ";
		} break;
		}

		std::wcout << message;
		if (newline) std::wcout << std::endl;
	}

	void Logger::SetLogInfo(bool logInfo)
	{
		m_LogInfo = true; // Enable info logging to display message
		LogInfo(logInfo ? "Enabled" : "Disabled" + std::string(" info logging"));

		m_LogInfo = logInfo;
	}

	void Logger::SetLogWarnings(bool logWarnings)
	{
		const bool prevLogInfo = m_LogInfo;
		m_LogInfo = true; // Enable info logging to display message
		LogInfo(logWarnings ? "Enabled" : "Disabled" + std::string(" warning logging"));
		m_LogInfo = prevLogInfo;

		m_LogWarnings = logWarnings;
	}

	void Logger::SetLogErrors(bool logErrors)
	{
		const bool prevLogInfo = m_LogInfo;
		m_LogInfo = true; // Enable info logging to display message
		LogInfo(logErrors ? "Enabled" : "Disabled" + std::string(" error logging"));
		m_LogInfo = prevLogInfo;

		m_LogErrors = logErrors;
	}

	void Logger::LogInfo(const std::string& message, bool newline)
	{
		Log(message, LogLevel::LOG_INFO, newline);
	}

	void Logger::LogWarning(const std::string& message, bool newline)
	{
		Log(message, LogLevel::LOG_WARNING, newline);
	}

	void Logger::LogError(const std::string& message, bool newline)
	{
		Log(message, LogLevel::LOG_ERROR, newline);
	}

	void Logger::LogInfo(const std::wstring& message, bool newline)
	{
		Log(message, LogLevel::LOG_INFO, newline);
	}

	void Logger::LogWarning(const std::wstring& message, bool newline)
	{
		Log(message, LogLevel::LOG_WARNING, newline);
	}

	void Logger::LogError(const std::wstring& message, bool newline)
	{
		Log(message, LogLevel::LOG_ERROR, newline);
	}
} // namespace flex
