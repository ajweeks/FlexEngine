#pragma once

#undef ERROR

namespace flex
{
	class Platform
	{
	public:
		enum class ConsoleColour
		{
			DEFAULT = 0,
			WARNING,
			ERROR,

			_NONE
		};

		static void Init();
		static void Update();

		static void GetConsoleHandle();
		static void SetConsoleTextColor(ConsoleColour colour);

		static bool IsProcessRunning(u32 PID);
		static u32 GetCurrentProcessID();

		static void MoveConsole(i32 width = 800, i32 height = 800);

		static void PrintStringToDebuggerConsole(const char* str);


		// File system helpers
		static void RetrieveCurrentWorkingDirectory();
		static bool CreateDirectoryRecursive(const std::string& absoluteDirectoryPath);
		static void OpenExplorer(const std::string& absoluteDirectory);
		static bool DirectoryExists(const std::string& absoluteDirectoryPath);
		static bool CopyFile(const std::string& filePathFrom, const std::string& filePathTo);
		static bool DeleteFile(const std::string& filePath, bool bPrintErrorOnFailure = true);

		// Returns true if any files were found
		// Set fileType to "*" to retrieve all files
		static bool FindFilesInDirectory(const std::string& directoryPath, std::vector<std::string>& filePaths, const std::string& fileType);
		static bool OpenFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath, char filter[] = nullptr);

		// Returns the current year, month, & day  (YYYY-MM-DD)
		// Returns the current year, month, day, hour, minute, & second (YYYY-MM-DD_HH-MM-SS)
		static std::string GetDateString_YMD();
		static std::string GetDateString_YMDHMS();

	};

	class DirectoryWatcher
	{
	public:
		DirectoryWatcher(const std::string& directory, bool bWatchSubtree);
		~DirectoryWatcher();

		// Returns true when directory has changed
		bool Update();

		bool Installed() const;

	private:
		bool m_bInstalled = false;
		bool m_bWatchSubtree = false;
		std::string m_Directory;
		void* m_ChangeHandle = nullptr;

	};
} // namespace flex
