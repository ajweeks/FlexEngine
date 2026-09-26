#include "stdafx.hpp"

#include "SceneTestRunner.hpp"

#include <csignal>

#include "Helpers.hpp"
#include "Pair.hpp"
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

	void SceneTestRunner::PrintSummary() const
	{
		using Colour = Platform::ConsoleColour;

		i32 failureCount = GetFailureCount();

		std::vector<Pair<Colour, std::string>> lines;
		std::vector<Pair<Colour, std::string>> failureLines;
		u32 issueCount = 0;

		{
			std::lock_guard<std::mutex> lock(m_Mutex);

			lines.emplace_back(Colour::DEFAULT, "\n=== Scene test results (" + UIntToString(m_FramesPerScene) + " frames per scene) ===\n");

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
					lines.emplace_back(Colour::ERROR, "  FAIL  " + result.sceneFileName + " (failed to load)\n");
					failureLines.emplace_back(Colour::ERROR, "  " + result.sceneFileName + ": failed to load\n");
					++issueCount;
				}
				else if (!result.issues.empty())
				{
					lines.emplace_back(Colour::ERROR, "  FAIL  " + result.sceneFileName + " (" + UIntToString(warningCount) + " warnings, " + UIntToString(errorCount) + " errors)\n");
				}
				else
				{
					lines.emplace_back(Colour::SUCCESS, "  PASS  " + result.sceneFileName + "\n");
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

					std::string line = "  " + result.sceneFileName + ": " + (issue.bError ? "error: " : "warning: ") + message;
					if (repeatCount > 1)
					{
						line += " (x" + UIntToString(repeatCount) + ")";
					}
					failureLines.emplace_back(issue.bError ? Colour::ERROR : Colour::WARNING, line + "\n");
					issueCount += repeatCount;
				}
			}

			lines.emplace_back(failureCount == 0 ? Colour::SUCCESS : Colour::ERROR,
				IntToString((i32)m_Results.size() - failureCount) + "/" + UIntToString((u32)m_Results.size()) + " passed\n");
		}

		if (failureCount > 0)
		{
			lines.emplace_back(Colour::ERROR, "\n" + IntToString(failureCount) + " failed, " + UIntToString(issueCount) + " issues:\n");
			lines.insert(lines.end(), failureLines.begin(), failureLines.end());
		}

		// Print outside of m_Mutex, as printing calls back into OnLog
		for (const Pair<Colour, std::string>& line : lines)
		{
			PrintColouredLong(line.first, line.second.c_str());
		}
		Platform::SetConsoleTextColour(Colour::DEFAULT);
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
