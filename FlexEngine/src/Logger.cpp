#include "stdafx.hpp"

#include <iostream>

#include "Logger.hpp"

namespace flex
{
	bool Logger::m_SuppressInfo = false;
	bool Logger::m_SuppressWarnings = false;
	bool Logger::m_SuppressErrors = false;

	i32 Logger::m_SuppressedInfoCount = 0;
	i32 Logger::m_SuppressedWarningCount = 0;
	i32 Logger::m_SuppressedErrorCount = 0;

#ifdef _WIN32
	HANDLE Logger::m_ConsoleHandle;
#endif

	void Logger::Initialize()
	{
#ifdef _WIN32
		m_ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);;
#endif
	}

	void Logger::Log(const std::string& message, LogLevel logLevel, bool newline)
	{
		switch (logLevel)
		{
		case Logger::LogLevel::LOG_INFO:
		{
			if (m_SuppressInfo)
			{
				++m_SuppressedInfoCount;
				return;
			}

#ifdef _WIN32
			SetConsoleTextAttribute(m_ConsoleHandle, CONSOLE_COLOR_INFO);
#endif
			std::cout << "[INFO] ";
		} break;
		case Logger::LogLevel::LOG_WARNING:
		{
			if (m_SuppressWarnings)
			{
				++m_SuppressedWarningCount;
				return;
			}

#ifdef _WIN32
			SetConsoleTextAttribute(m_ConsoleHandle, CONSOLE_COLOR_WARNING);
#endif
			std::cout << "[WARNING] ";
		} break;
		case Logger::LogLevel::LOG_ERROR:
		{
			if (m_SuppressErrors)
			{
				++m_SuppressedErrorCount;
				return;
			}

#ifdef _WIN32
			SetConsoleTextAttribute(m_ConsoleHandle, CONSOLE_COLOR_ERROR);
#endif
			std::cout << "[ERROR] ";
		} break;
		default:
		{
			std::cout << "UNHANDLED LOG LEVEL!: " + std::to_string(i32(logLevel)) + ", message: ";
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
			if (m_SuppressInfo)
			{
				++m_SuppressedInfoCount;
				return;
			}

#ifdef _WIN32
			SetConsoleTextAttribute(m_ConsoleHandle, CONSOLE_COLOR_INFO);
#endif
			std::wcout << L"[INFO] ";
		} break;
		case Logger::LogLevel::LOG_WARNING:
		{
			if (m_SuppressWarnings)
			{
				++m_SuppressedWarningCount;
				return;
			}

#ifdef _WIN32
			SetConsoleTextAttribute(m_ConsoleHandle, CONSOLE_COLOR_WARNING);
#endif
			std::wcout << L"[WARNING] ";
		} break;
		case Logger::LogLevel::LOG_ERROR:
		{
			if (m_SuppressErrors)
			{
				++m_SuppressedErrorCount;
				return;
			}

#ifdef _WIN32
			SetConsoleTextAttribute(m_ConsoleHandle, CONSOLE_COLOR_ERROR);
#endif
			std::wcout << L"[ERROR] ";
		} break;
		default:
		{
			std::wcout << L"UNHANDLED LOG LEVEL!: " + std::to_wstring(i32(logLevel)) + L", message: ";
		} break;
		}

		std::wcout << message;
		if (newline) std::wcout << std::endl;
	}

	void Logger::SetSuppressInfo(bool suppressInfo)
	{
		if (m_SuppressInfo == suppressInfo) return;

		m_SuppressInfo = false; // Enable info logging to display message
		LogInfo((suppressInfo ? "Enabled" : "Disabled") + std::string(" info suppression"));

		m_SuppressInfo = suppressInfo;
	}

	bool Logger::GetSuppressInfo()
	{
		return m_SuppressInfo;
	}

	void Logger::SetSuppressWarnings(bool suppressWarnings)
	{
		if (m_SuppressWarnings == suppressWarnings) return;

		const bool prevSuppressLog = m_SuppressInfo;
		m_SuppressInfo = false; // Enable info logging to display message
		LogInfo((suppressWarnings ? "Enabled" : "Disabled") + std::string(" warning suppression"));
		m_SuppressInfo = prevSuppressLog;

		m_SuppressWarnings = suppressWarnings;
	}

	bool Logger::GetSuppressWarnings()
	{
		return m_SuppressWarnings;
	}

	void Logger::SetSuppressErrors(bool suppressErrors)
	{
		if (m_SuppressErrors == suppressErrors) return;

		const bool prevSuppressLog = m_SuppressInfo;
		m_SuppressInfo = false; // Enable info logging to display message
		LogInfo((suppressErrors ? "Enabled" : "Disabled") + std::string(" error suppression"));
		m_SuppressInfo = prevSuppressLog;

		m_SuppressErrors = suppressErrors;
	}

	bool Logger::GetSuppressErrors()
	{
		return m_SuppressErrors;
	}

	i32 Logger::GetSuppressedInfoCount()
	{
		return m_SuppressedInfoCount;
	}

	i32 Logger::GetSuppressedWarningCount()
	{
		return m_SuppressedWarningCount;
	}

	i32 Logger::GetSuppressedErrorCount()
	{
		return m_SuppressedErrorCount;
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
