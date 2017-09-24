#include "stdafx.h"

#include <iostream>

#include "Logger.h"

namespace flex
{
	void Logger::Log(const std::string& message, LogLevel logLevel)
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

		std::cout << message << std::endl;
	}

	void Logger::Log(const std::wstring& message, LogLevel logLevel)
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

		std::wcout << message << std::endl;
	}

	void Logger::LogInfo(const std::string& message)
	{
		Log(message, LogLevel::LOG_INFO);
	}

	void Logger::LogWarning(const std::string& message)
	{
		Log(message, LogLevel::LOG_WARNING);
	}

	void Logger::LogError(const std::string& message)
	{
		Log(message, LogLevel::LOG_ERROR);
	}

	void Logger::LogInfo(const std::wstring& message)
	{
		Log(message, LogLevel::LOG_INFO);
	}

	void Logger::LogWarning(const std::wstring& message)
	{
		Log(message, LogLevel::LOG_WARNING);
	}

	void Logger::LogError(const std::wstring& message)
	{
		Log(message, LogLevel::LOG_ERROR);
	}
} // namespace flex
