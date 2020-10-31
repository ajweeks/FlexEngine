#include "stdafx.hpp"

#ifdef _WINDOWS

#include "Platform/Platform.hpp"

IGNORE_WARNINGS_PUSH
#include <tlhelp32.h> // For CreateToolhelp32Snapshot, Process32First

#include <commdlg.h> // For OPENFILENAME
#include <shellapi.h> // For ShellExecute

#include <direct.h> // For _getcwd
#include <stdio.h> // For gcvt, fopen
IGNORE_WARNINGS_POP

#include "FlexEngine.hpp"
#include "Helpers.hpp"

// Taken from ntddvdeo.h:
#define FOREGROUND_BLUE      0x0001
#define FOREGROUND_GREEN     0x0002
#define FOREGROUND_RED       0x0004
#define FOREGROUND_INTENSITY 0x0008
#define BACKGROUND_BLUE      0x0010
#define BACKGROUND_GREEN     0x0020
#define BACKGROUND_RED       0x0040
#define BACKGROUND_INTENSITY 0x0080

namespace flex
{
	typedef void* HANDLE;
	typedef unsigned short WORD;

	const WORD CONSOLE_COLOR_DEFAULT = 0 | FOREGROUND_INTENSITY;
	const WORD CONSOLE_COLOR_WARNING = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
	const WORD CONSOLE_COLOR_ERROR = FOREGROUND_RED | FOREGROUND_INTENSITY;

	HANDLE g_ConsoleHandle;

	CPUInfo Platform::cpuInfo;

	struct ThreadData
	{
		u32 num;
	};

	struct WorkItem
	{
		u32 a;
	};

	struct CompletedWorkItem
	{
		u32 ans;
	};

	std::vector<HANDLE> ThreadHandles;

	void Platform::Init()
	{
		RetrieveCPUInfo();
	}

	void Platform::GetConsoleHandle()
	{
		g_ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(g_ConsoleHandle, CONSOLE_COLOR_DEFAULT);
	}

	void Platform::SetConsoleTextColor(ConsoleColour colour)
	{
#if ENABLE_CONSOLE_COLOURS
		static WORD w_colours[] = { CONSOLE_COLOR_DEFAULT, CONSOLE_COLOR_WARNING, CONSOLE_COLOR_ERROR };

		SetConsoleTextAttribute(g_ConsoleHandle, w_colours[(u32)colour]);
#else
		FLEX_UNUSED(colour);
#endif
	}

	bool Platform::IsProcessRunning(u32 PID)
	{
		HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, PID);
		if (h != INVALID_HANDLE_VALUE)
		{
			PROCESSENTRY32 entry = {};
			entry.dwSize = sizeof(PROCESSENTRY32);
			if (Process32First(h, &entry))
			{
				do
				{
					if (entry.th32ProcessID == PID)
					{
						return true;
					}
				} while (Process32Next(h, &entry));
			}
		}

		return false;
	}

	u32 Platform::GetCurrentProcessID()
	{
		return (u32)GetCurrentProcessId();
	}

	// TODO: Optionally save exact position of console in window-settings.json
	void Platform::MoveConsole(i32 width /* = 800 */, i32 height /* = 800 */)
	{
		HWND hWnd = GetConsoleWindow();

		// The following four variables store the bounding rectangle of all monitors
		i32 virtualScreenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
		//i32 virtualScreenTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
		i32 virtualScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		//i32 virtualScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

		i32 monitorWidth = GetSystemMetrics(SM_CXSCREEN);
		//i32 monitorHeight = GetSystemMetrics(SM_CYSCREEN);

		// If another monitor is present, move the console to it
		if (virtualScreenWidth > monitorWidth)
		{
			i32 newX;
			i32 newY = 10;

			if (virtualScreenLeft < 0)
			{
				// The other monitor is to the left of the main one
				newX = -(width + 67);
			}
			else
			{
				// The other monitor is to the right of the main one
				newX = virtualScreenWidth - monitorWidth + 10;
			}

			MoveWindow(hWnd, newX, newY, width, height, TRUE);

			// Call again to set size correctly (based on other monitor's DPI)
			MoveWindow(hWnd, newX, newY, width, height, TRUE);
		}
		else // There's only one monitor, move the console to the top left corner
		{
			RECT rect;
			GetWindowRect(hWnd, &rect);
			if (rect.top != 0)
			{
				// A negative value is needed to line the console up to the left side of my monitor
				MoveWindow(hWnd, -7, 0, width, height, TRUE);
			}
		}
	}

	void Platform::PrintStringToDebuggerConsole(const char* str)
	{
		OutputDebugString(str);
	}

	void Platform::RetrieveCurrentWorkingDirectory()
	{
		char cwdBuffer[MAX_PATH];
		char* str = _getcwd(cwdBuffer, sizeof(cwdBuffer));
		if (str)
		{
			FlexEngine::s_CurrentWorkingDirectory = ReplaceBackSlashesWithForward(str);
		}
	}

	bool Platform::CreateDirectoryRecursive(const std::string& absoluteDirectoryPath)
	{
		if (absoluteDirectoryPath.find("..") != std::string::npos)
		{
			PrintError("Attempted to create directory using relative path! Must specify absolute path!\n");
			return false;
		}

		if (Platform::DirectoryExists(absoluteDirectoryPath))
		{
			// Directory already exists
			return true;
		}

		size_t pos = absoluteDirectoryPath.find_first_of('/', 0);
		while (pos != std::string::npos)
		{
			CreateDirectory(absoluteDirectoryPath.substr(0, pos).c_str(), NULL);
			pos = absoluteDirectoryPath.find_first_of('/', pos + 1);
			// TODO: Return false on failure here
			//GetLastError() == ERROR_ALREADY_EXISTS;
		}

		return true;
	}

	void Platform::OpenExplorer(const std::string& absoluteDirectory)
	{
		ShellExecute(NULL, "open", absoluteDirectory.c_str(), NULL, NULL, SW_SHOWDEFAULT);
	}

	bool Platform::DirectoryExists(const std::string& absoluteDirectoryPath)
	{
		if (absoluteDirectoryPath.find("..") != std::string::npos)
		{
			PrintError("Attempted to create directory using relative path! Must specify absolute path!\n");
			return false;
		}

		DWORD dwAttrib = GetFileAttributes(absoluteDirectoryPath.c_str());

		return (dwAttrib != INVALID_FILE_ATTRIBUTES && dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool Platform::CopyFile(const std::string& filePathFrom, const std::string& filePathTo)
	{
		if (::CopyFile(filePathFrom.c_str(), filePathTo.c_str(), 0))
		{
			return true;
		}
		else
		{
			PrintError("Failed to copy file from \"%s\" to \"%s\"\n", filePathFrom.c_str(), filePathTo.c_str());
			return false;
		}
	}

	bool Platform::DeleteFile(const std::string& filePath, bool bPrintErrorOnFailure /* = true */)
	{
		if (::DeleteFile(filePath.c_str()))
		{
			return true;
		}
		else
		{
			if (bPrintErrorOnFailure)
			{
				PrintError("Failed to delete file %s\n", filePath.c_str());
			}
			return false;
		}
	}

	bool Platform::FindFilesInDirectory(const std::string& directoryPath, std::vector<std::string>& filePaths, const std::string& fileType)
	{
		std::string cleanedFileType = fileType;
		{
			size_t dotPos = cleanedFileType.find('.');
			if (dotPos != std::string::npos)
			{
				cleanedFileType.erase(dotPos, 1);
			}
		}

		std::string cleanedDirPath = directoryPath;
		if (cleanedDirPath[cleanedDirPath.size() - 1] != '/')
		{
			cleanedDirPath += '/';
		}

		std::string cleanedDirPathWithWildCard = cleanedDirPath + '*';


		HANDLE hFind;
		WIN32_FIND_DATAA findData;

		hFind = FindFirstFile(cleanedDirPathWithWildCard.c_str(), &findData);

		if (hFind == INVALID_HANDLE_VALUE)
		{
			PrintError("Failed to find any file in directory %s\n", cleanedDirPath.c_str());
			return false;
		}

		do
		{
			if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				// Skip over directories
				//Print(findData.cFileName);
			}
			else
			{
				bool foundFileTypeMatches = false;
				if (cleanedFileType == "*")
				{
					foundFileTypeMatches = true;
				}
				else
				{
					std::string fileNameStr(findData.cFileName);
					size_t dotPos = fileNameStr.find('.');

					if (dotPos != std::string::npos)
					{
						std::string foundFileType = Split(fileNameStr, '.')[1];
						if (foundFileType == cleanedFileType)
						{
							foundFileTypeMatches = true;
						}
					}
				}

				if (foundFileTypeMatches)
				{
					// File size retrieval:
					//LARGE_INTEGER filesize;
					//filesize.LowPart = findData.nFileSizeLow;
					//filesize.HighPart = findData.nFileSizeHigh;

					filePaths.push_back(cleanedDirPath + findData.cFileName);
				}
			}
		} while (FindNextFile(hFind, &findData) != 0);

		FindClose(hFind);

		DWORD dwError = GetLastError();
		if (dwError != ERROR_NO_MORE_FILES)
		{
			PrintError("Error encountered while finding files in directory %s\n", cleanedDirPath.c_str());
			return false;
		}

		return !filePaths.empty();
	}

	bool Platform::OpenFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath, char filter[] /* = nullptr */)
	{
		OPENFILENAME openFileName = {};
		openFileName.lStructSize = sizeof(OPENFILENAME);
		openFileName.lpstrInitialDir = absoluteDirectory.c_str();
		openFileName.nMaxFile = (filter == nullptr ? 0 : (u32)strlen(filter));
		if (openFileName.nMaxFile && filter)
		{
			openFileName.lpstrFilter = filter;
		}
		openFileName.nFilterIndex = 0;
		const i32 MAX_FILE_PATH_LEN = 512;
		char fileBuf[MAX_FILE_PATH_LEN];
		memset(fileBuf, '\0', MAX_FILE_PATH_LEN - 1);
		openFileName.lpstrFile = fileBuf;
		openFileName.nMaxFile = MAX_FILE_PATH_LEN;
		openFileName.lpstrTitle = windowTitle.c_str();
		openFileName.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
		bool bSuccess = GetOpenFileName(&openFileName) == 1;

		if (bSuccess && openFileName.lpstrFile)
		{
			outSelectedAbsFilePath = ReplaceBackSlashesWithForward(openFileName.lpstrFile);
		}

		return bSuccess;
	}

	void Platform::OpenFileWithDefaultApplication(const std::string& absoluteDirectory)
	{
		ShellExecute(0, 0, absoluteDirectory.c_str(), 0, 0, SW_SHOW);
	}

	void Platform::LaunchApplication(const std::string& applicationName, const std::string& param0)
	{
		ShellExecute(0, 0, applicationName.c_str(), param0.c_str(), 0, SW_SHOW);
	}

	std::string Platform::GetDateString_YMD()
	{
		std::stringstream result;

		SYSTEMTIME time;
		GetLocalTime(&time);

		result << IntToString(time.wYear, 4) << '-' <<
			IntToString(time.wMonth, 2) << '-' <<
			IntToString(time.wDay, 2);

		return result.str();
	}

	std::string Platform::GetDateString_YMDHMS()
	{
		std::stringstream result;

		SYSTEMTIME time;
		GetLocalTime(&time);

		result << IntToString(time.wYear, 4) << '-' <<
			IntToString(time.wMonth, 2) << '-' <<
			IntToString(time.wDay, 2) << '_' <<
			IntToString(time.wHour, 2) << '-' <<
			IntToString(time.wMinute, 2) << '-' <<
			IntToString(time.wSecond, 2);

		return result.str();
	}

	u32 Platform::AtomicIncrement(volatile u32* value)
	{
		return InterlockedIncrement(value);
	}

	u32 Platform::AtomicCompareExchange(volatile u32* value, u32 exchange, u32 comparand)
	{
		return InterlockedCompareExchange(value, exchange, comparand);
	}

	u32 Platform::AtomicExchange(volatile u32* value, u32 exchange)
	{
		return InterlockedExchange(value, exchange);
	}

	void Platform::JoinThreads()
	{
		for (u32 i = 0; i < (u32)ThreadHandles.size(); ++i)
		{
			WaitForSingleObject(ThreadHandles[i], INFINITE);
		}
		ThreadHandles.clear();
	}

	void Platform::SpawnThreads(u32 threadCount, void* (entryPoint)(void*), void* userData)
	{
		ThreadHandles.resize(threadCount);

		for (u32 i = 0; i < (u32)ThreadHandles.size(); ++i)
		{
			ThreadHandles[i] = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)entryPoint, userData, 0, 0);
		}
	}

	void Platform::YieldProcessor()
	{
		::YieldProcessor();
	}

	void* Platform::InitCriticalSection()
	{
		CRITICAL_SECTION* criticalSection = new CRITICAL_SECTION();
		InitializeCriticalSection(criticalSection);
		return criticalSection;
	}

	void Platform::FreeCriticalSection(void* criticalSection)
	{
		delete (CRITICAL_SECTION*)criticalSection;
	}

	void Platform::EnterCriticalSection(void* criticalSection)
	{
		::EnterCriticalSection((CRITICAL_SECTION*)criticalSection);
	}

	void Platform::LeaveCriticalSection(void* criticalSection)
	{
		::LeaveCriticalSection((CRITICAL_SECTION*)criticalSection);
	}

	void Platform::Sleep(ms milliseconds)
	{
		::Sleep((DWORD)milliseconds);
	}

	void Platform::RetrieveCPUInfo()
	{
		typedef BOOL(WINAPI* LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

		LPFN_GLPI glpi;
		BOOL done = FALSE;
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;

		cpuInfo = {};
		cpuInfo.logicalProcessorCount = 0;
		cpuInfo.physicalCoreCount = 0;
		cpuInfo.l1CacheCount = 0;
		cpuInfo.l2CacheCount = 0;
		cpuInfo.l3CacheCount = 0;

		DWORD returnLength = 0;
		DWORD numaNodeCount = 0;
		DWORD processorPackageCount = 0;
		DWORD byteOffset = 0;
		PCACHE_DESCRIPTOR Cache;

		glpi = (LPFN_GLPI)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "GetLogicalProcessorInformation");
		if (glpi == NULL)
		{
			PrintError("GetLogicalProcessorInformation is not supported.\n");
			return;
		}

		while (!done)
		{
			DWORD rc = glpi(buffer, &returnLength);

			if (rc == FALSE)
			{
				if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
				{
					if (buffer)
					{
						free(buffer);
					}

					buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(returnLength);

					if (buffer == NULL)
					{
						PrintError("Allocation failure in GetLogicalProcessorCount\n");
						return;
					}
				}
				else
				{
					PrintError("Error encountered in GetLogicalProcessorCount: %d\n", GetLastError());
					return;
				}
			}
			else
			{
				done = TRUE;
			}
		}

		ptr = buffer;

		while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength)
		{
			switch (ptr->Relationship)
			{
			case RelationNumaNode:
				// Non-NUMA systems report a single record of this type.
				numaNodeCount++;
				break;

			case RelationProcessorCore:
				cpuInfo.physicalCoreCount++;

				// A hyperthreaded core supplies more than one logical processor.
				cpuInfo.logicalProcessorCount += CountSetBits((u32)ptr->ProcessorMask);
				break;

			case RelationCache:
				// Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache.
				Cache = &ptr->Cache;
				if (Cache->Level == 1)
				{
					cpuInfo.l1CacheCount++;
				}
				else if (Cache->Level == 2)
				{
					cpuInfo.l2CacheCount++;
				}
				else if (Cache->Level == 3)
				{
					cpuInfo.l3CacheCount++;
				}
				break;

			case RelationProcessorPackage:
				// Logical processors share a physical package.
				processorPackageCount++;
				break;

			default:
				PrintError("Unsupported LOGICAL_PROCESSOR_RELATIONSHIP value.\n");
				break;
			}
			byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
			ptr++;
		}

		free(buffer);
	}

	DirectoryWatcher::DirectoryWatcher(const std::string& directory, bool bWatchSubtree) :
		m_Directory(directory),
		m_bWatchSubtree(bWatchSubtree)
	{
		{
			m_ChangeHandle = FindFirstChangeNotification(
				directory.c_str(),
				m_bWatchSubtree ? TRUE : FALSE,
				FILE_NOTIFY_CHANGE_LAST_WRITE);

			if (m_ChangeHandle == INVALID_HANDLE_VALUE)
			{
				PrintError("FindFirstChangeNotification failed! Directory watch not installed for %s\n", directory.c_str());
				m_bInstalled = false;
			}
			else
			{
				m_bInstalled = true;
			}
		}

	}

	DirectoryWatcher::~DirectoryWatcher()
	{
		if (m_bInstalled)
		{
			FindCloseChangeNotification(m_ChangeHandle);
			m_bInstalled = false;
		}
	}

	bool DirectoryWatcher::Update()
	{
		DWORD dwWaitStatus = WaitForSingleObject(m_ChangeHandle, 0);
		switch (dwWaitStatus)
		{
		case WAIT_OBJECT_0:
			// Clear modification flag (needs to be called twice for some reason...)
			if (FindNextChangeNotification(m_ChangeHandle) == FALSE ||
				FindNextChangeNotification(m_ChangeHandle) == FALSE)
			{
				PrintError("Something bad happened with the directory watch on %s\n", m_Directory.c_str());
			}

			return true;
		}

		return false;
	}

	bool DirectoryWatcher::Installed() const
	{
		return m_bInstalled;
	}

} // namespace flex
#endif // _WINDOWS
