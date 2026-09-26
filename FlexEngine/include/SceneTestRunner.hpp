#pragma once

#include <mutex>

namespace flex
{
	// Loads each scene in turn (or only those passed in), simulating framesPerScene frames in each,
	// and records any warnings/errors logged. Only created when running with --test-scenes.
	class SceneTestRunner final : public LogSink
	{
	public:
		SceneTestRunner(const std::vector<std::string>& sceneFileNames, u32 framesPerScene);
		~SceneTestRunner();

		SceneTestRunner(const SceneTestRunner&&) = delete;
		SceneTestRunner(const SceneTestRunner&) = delete;
		SceneTestRunner& operator=(const SceneTestRunner&&) = delete;
		SceneTestRunner& operator=(const SceneTestRunner&) = delete;

		// To be called once the engine has initialized & discovered all scenes
		void OnEngineInitialized();
		// To be called at the start of each frame, returns false once all scenes have been tested
		bool Update();

		// Returns the number of scenes which failed (plus one if startup failed)
		i32 GetFailureCount() const;
		// Prints per-scene results followed by a list of every warning/error, one per line
		void PrintSummary() const;

		virtual void OnLog(LogLevel level, const char* message) override;

	private:
		struct Issue
		{
			bool bError;
			std::string message;
		};

		struct Result
		{
			std::string sceneFileName;
			std::vector<Issue> issues;
			bool bLoaded = false;
		};

		// Subsequent issues will be attributed to this entry
		void BeginEntry(const std::string& name);
		void StopRecording();

		// Reports which scene was being tested, since a crash would otherwise prevent the summary from printing
		static void OnCrashSignal(i32 signal);
		static SceneTestRunner* s_Instance;

		u32 m_FramesPerScene = 0;
		u32 m_FramesRemaining = 0;
		std::vector<std::string> m_Queue;
		// Only written on the main thread, read without locking by the crash handler
		std::string m_CurrentEntryName;

		// Guards members below, as OnLog can be called from any thread
		mutable std::mutex m_Mutex;
		std::vector<Result> m_Results;
		bool m_bRecording = false;

	};
} // namespace flex
