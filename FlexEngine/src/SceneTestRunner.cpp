#include "stdafx.hpp"

#include "SceneTestRunner.hpp"

#include <csignal>

#include "Helpers.hpp"
#include "Platform/Platform.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	SceneTestRunner* SceneTestRunner::s_Instance = nullptr;

	SceneTestRunner::SceneTestRunner(const std::vector<std::string>& sceneFileNames, u32 framesPerScene) :
		m_FramesPerScene(glm::max(framesPerScene, 1u)),
		m_Queue(sceneFileNames)
	{
		AddLogSink(this);

		s_Instance = this;
		std::signal(SIGSEGV, OnCrashSignal);
		std::signal(SIGFPE, OnCrashSignal);
		std::signal(SIGILL, OnCrashSignal);

		// Anything logged before the first scene test begins is attributed to startup
		BeginEntry("<startup>");
	}

	SceneTestRunner::~SceneTestRunner()
	{
		std::signal(SIGSEGV, SIG_DFL);
		std::signal(SIGFPE, SIG_DFL);
		std::signal(SIGILL, SIG_DFL);
		s_Instance = nullptr;

		RemoveLogSink(this);
	}

	void SceneTestRunner::OnEngineInitialized()
	{
		if (m_Queue.empty())
		{
			m_Queue = g_SceneManager->GetSceneFileNames();
		}
	}

	bool SceneTestRunner::Update()
	{
		if (m_FramesRemaining > 0)
		{
			--m_FramesRemaining;
			return true;
		}

		if (m_Queue.empty())
		{
			// Don't capture anything logged during shutdown
			StopRecording();
			return false;
		}

		std::string sceneFileName = m_Queue.front();
		m_Queue.erase(m_Queue.begin());

		BeginEntry(sceneFileName);
		bool bLoaded = g_SceneManager->SetCurrentScene(sceneFileName);
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Results.back().bLoaded = bLoaded;
		}
		// The current frame counts as the first frame
		m_FramesRemaining = bLoaded ? (m_FramesPerScene - 1) : 0;

		return true;
	}

	i32 SceneTestRunner::GetFailureCount() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		i32 failureCount = 0;
		for (const Result& result : m_Results)
		{
			if (!result.bLoaded || !result.issues.empty())
			{
				++failureCount;
			}
		}
		return failureCount;
	}

	std::string SceneTestRunner::GetSummary() const
	{
		i32 failureCount = GetFailureCount();

		std::lock_guard<std::mutex> lock(m_Mutex);

		std::string summary = "\n=== Scene test results (" + UIntToString(m_FramesPerScene) + " frames per scene) ===\n";

		std::string failureList;
		u32 issueCount = 0;
		for (const Result& result : m_Results)
		{
			u32 errorCount = 0;
			for (const Issue& issue : result.issues)
			{
				errorCount += issue.bError ? 1 : 0;
			}
			u32 warningCount = (u32)result.issues.size() - errorCount;

			if (!result.bLoaded)
			{
				summary += "  FAIL  " + result.sceneFileName + " (failed to load)\n";
				failureList += "  " + result.sceneFileName + ": failed to load\n";
				++issueCount;
			}
			else if (!result.issues.empty())
			{
				summary += "  FAIL  " + result.sceneFileName + " (" + UIntToString(warningCount) + " warnings, " + UIntToString(errorCount) + " errors)\n";
			}
			else
			{
				summary += "  PASS  " + result.sceneFileName + "\n";
			}

			// Only use the first line of each message, and collapse consecutive duplicates
			for (u32 i = 0; i < (u32)result.issues.size();)
			{
				const Issue& issue = result.issues[i];
				u32 repeatCount = 1;
				while (i + repeatCount < (u32)result.issues.size() &&
					result.issues[i + repeatCount].bError == issue.bError &&
					result.issues[i + repeatCount].message == issue.message)
				{
					++repeatCount;
				}
				i += repeatCount;

				std::string message = TrimLeadingWhitespace(issue.message);
				message = Trim(message.substr(0, message.find('\n')));
				if (message.empty())
				{
					continue;
				}

				failureList += "  " + result.sceneFileName + ": " + (issue.bError ? "error: " : "warning: ") + message;
				if (repeatCount > 1)
				{
					failureList += " (x" + UIntToString(repeatCount) + ")";
				}
				failureList += "\n";
				issueCount += repeatCount;
			}
		}

		summary += IntToString((i32)m_Results.size() - failureCount) + "/" + UIntToString((u32)m_Results.size()) + " passed\n";

		if (failureCount > 0)
		{
			summary += "\n" + IntToString(failureCount) + " failed, " + UIntToString(issueCount) + " issues:\n" + failureList;
		}

		return summary;
	}

	void SceneTestRunner::OnLog(LogLevel level, const char* message)
	{
		if (level == LogLevel::MESSAGE)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(m_Mutex);

		if (m_bRecording)
		{
			m_Results.back().issues.push_back({ level == LogLevel::ERROR, std::string(message) });
		}
	}

	void SceneTestRunner::BeginEntry(const std::string& name)
	{
		m_CurrentEntryName = name;

		std::lock_guard<std::mutex> lock(m_Mutex);

		Result result = {};
		result.sceneFileName = name;
		// Startup has nothing to load
		result.bLoaded = m_Results.empty();
		m_Results.push_back(result);
		m_bRecording = true;
	}

	void SceneTestRunner::StopRecording()
	{
		m_CurrentEntryName = "<shutdown>";

		std::lock_guard<std::mutex> lock(m_Mutex);

		m_bRecording = false;
	}

	void SceneTestRunner::OnCrashSignal(i32 signal)
	{
		// Not async-signal-safe, but the process is going down anyway. Print is used (rather than
		// PrintError) since OnLog ignores messages and so won't try to take m_Mutex, which may be held.
		const char* entryName = s_Instance != nullptr ? s_Instance->m_CurrentEntryName.c_str() : "unknown";
		Print("\n=== Scene test crashed (signal %d) while testing %s ===\n", signal, entryName);
		Platform::PrintStackTrace();

		std::signal(signal, SIG_DFL);
		std::raise(signal);
	}
} // namespace flex
