#include "stdafx.hpp"

#include <iostream>

#include "Logger.hpp"

namespace flex
{
	void Logger::Log(const std::string& message, LogLevel logLevel, bool newline)
	{
		switch (logLevel)
		{
		case Logger::LogLevel::LOG_INFO:
		{
			std::cout << "[INFO] ";
		} break;
		case Logger::LogLevel::LOG_WARNING:
		{
			std::cout << "[WARNING] ";
		} break;
		case Logger::LogLevel::LOG_ERROR:
		{
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
			std::wcout << L"[INFO] ";
		} break;
		case Logger::LogLevel::LOG_WARNING:
		{
			std::wcout << L"[WARNING] ";
		} break;
		case Logger::LogLevel::LOG_ERROR:
		{
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
