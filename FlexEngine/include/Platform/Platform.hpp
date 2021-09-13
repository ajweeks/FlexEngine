#pragma once

#undef ERROR

namespace flex
{
	struct CPUInfo
	{
		u32 logicalProcessorCount;
		u32 physicalCoreCount;
		u32 l1CacheCount;
		u32 l2CacheCount;
		u32 l3CacheCount;
	};

	struct Date
	{
		Date() {}

		Date(u32 year, u32 month, u32 day, u32 hour, u32 minute, u32 second, u32 millisecond) :
			year(year),
			month(month),
			day(day),
			hour(hour),
			minute(minute),
			second(second),
			millisecond(millisecond)
		{
		}

		bool operator!=(const Date& other)
		{
			return !(*this == other);
		}

		bool operator==(const Date& other)
		{
			return year == other.year &&
				month == other.month &&
				day == other.day &&
				hour == other.hour &&
				minute == other.minute &&
				second == other.second &&
				millisecond == other.millisecond;
		}

		u32 year, month, day, hour, minute, second, millisecond;
	};

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

		static void GetConsoleHandle();
		static void SetConsoleTextColour(ConsoleColour colour);

		static bool IsProcessRunning(u32 PID);
		static u32 GetCurrentProcessID();

		static void MoveConsole(i32 width = 800, i32 height = 800);

		static void PrintStringToDebuggerConsole(const char* str);

		static u32 GetLogicalProcessorCount()
		{
			return cpuInfo.logicalProcessorCount;
		}

		// File system helpers
		static void RetrieveCurrentWorkingDirectory();
		static void RetrievePathToExecutable();
		static bool CreateDirectoryRecursive(const std::string& absoluteDirectoryPath);
		static void OpenFileExplorer(const char* absoluteDirectory);
		static bool DirectoryExists(const std::string& absoluteDirectoryPath);
		static bool CopyFile(const std::string& filePathFrom, const std::string& filePathTo)
		{
			std::ifstream src(filePathFrom);
			std::ofstream dst(filePathTo);

			if (src.is_open() && dst.is_open())
			{
				dst << src.rdbuf();
				src.close();
				dst.close();
				return true;
			}
			else
			{
				PrintError("Failed to copy file from \"%s\" to \"%s\"\n", filePathFrom.c_str(), filePathTo.c_str());
				return false;
			}
		}

		static bool DeleteFile(const std::string& filePath, bool bPrintErrorOnFailure = true);

		static bool GetFileModifcationTime(const char* filePath, Date& outModificationDate);

		// Returns true if any files were found
		// Set fileType to "*" to retrieve all files
		static bool FindFilesInDirectory(const std::string& directoryPath, std::vector<std::string>& filePaths, const std::string& fileTypeFilter);
		static bool FindFilesInDirectory(const std::string& directoryPath, std::vector<std::string>& filePaths, const char* fileTypes[], u32 fileTypesLen);
		static bool OpenFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath, char filter[] = nullptr);

		static void OpenFileWithDefaultApplication(const std::string& absoluteDirectory);
		static void LaunchApplication(const std::string& applicationName, const std::string& param0);

		// Returns the current year, month, & day  (YYYY-MM-DD)
		// Returns the current year, month, day, hour, minute, & second (YYYY-MM-DD_HH-MM-SS)
		static std::string GetDateString_YMD();
		static std::string GetDateString_YMDHMS();

		static u32 AtomicIncrement(volatile u32* value);
		static u32 AtomicDecrement(volatile u32* value);
		static u32 AtomicCompareExchange(volatile u32* value, u32 exchange, u32 comparand);
		static u32 AtomicExchange(volatile u32* value, u32 exchange);

		static void JoinThreads();
		static void SpawnThreads(u32 threadCount, void* (entryPoint)(void*), void* userData);
		static void YieldProcessor();

		static void* InitCriticalSection();
		static void FreeCriticalSection(void* criticalSection);
		static void EnterCriticalSection(void* criticalSection);
		static void LeaveCriticalSection(void* criticalSection);

		static void Sleep(ms milliseconds);

		static u64 GetUSSinceEpoch();
		static GameObjectID GenerateGUID();

		static CPUInfo cpuInfo;

	private:
		static void RetrieveCPUInfo();

	};

	class DirectoryWatcher final
	{
	public:
		DirectoryWatcher(const std::string& directory, bool bWatchSubtree);
		~DirectoryWatcher();

		DirectoryWatcher(const DirectoryWatcher& other)
		{
			if (this != &other)
			{
				userData = other.userData;
				directory = other.directory;
				m_bInstalled = other.m_bInstalled;
				m_bWatchSubtree = other.m_bWatchSubtree;
				m_ChangeHandle = other.m_ChangeHandle;
			}
		}
		DirectoryWatcher(const DirectoryWatcher&& other)
		{
			if (this != &other)
			{
				userData = other.userData;
				directory = other.directory;
				m_bInstalled = other.m_bInstalled;
				m_bWatchSubtree = other.m_bWatchSubtree;
				m_ChangeHandle = other.m_ChangeHandle;
			}
		}
		DirectoryWatcher& operator=(const DirectoryWatcher& other)
		{
			if (this != &other)
			{
				userData = other.userData;
				directory = other.directory;
				m_bInstalled = other.m_bInstalled;
				m_bWatchSubtree = other.m_bWatchSubtree;
				m_ChangeHandle = other.m_ChangeHandle;
			}
		}
		DirectoryWatcher& operator=(const DirectoryWatcher&& other)
		{
			if (this != &other)
			{
				userData = other.userData;
				directory = other.directory;
				m_bInstalled = other.m_bInstalled;
				m_bWatchSubtree = other.m_bWatchSubtree;
				m_ChangeHandle = other.m_ChangeHandle;
			}
		}

		// Returns true when directory has changed
		bool Update();

		bool Installed() const;

		void* userData = nullptr;
		std::string directory;

	private:
		bool m_bInstalled = false;
		bool m_bWatchSubtree = false;
		void* m_ChangeHandle = nullptr;

	};
} // namespace flex
