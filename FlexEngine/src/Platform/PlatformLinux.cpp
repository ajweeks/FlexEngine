#include "stdafx.hpp"

#ifdef __linux__

#include "Platform/Platform.hpp"

IGNORE_WARNINGS_PUSH
#include <iostream>
#include <fstream>
#include <ctime>
#include <locale>

#include <sys/types.h> // For kill
#include <sys/stat.h> // For stat
#include <sys/time.h> // fortimeofday
#include <signal.h> // For kill
#include <unistd.h> // For getcwd, unlink, getpid, sysconf, readlink, chdir
#include <errno.h> // For errno
#include <dirent.h> // For readdir
#include <sched.h> // For sched_yield
#include <pthread.h>
#include <libgen.h> // for dirname
#include <linux/limits.h> // for PATH_MAX
#include <uuid/uuid.h>

#include <cxxabi.h> // for __cxa_demangle
#include <execinfo.h> // for backtrace

#include <stdlib.h>
IGNORE_WARNINGS_POP

#include "FlexEngine.hpp"
#include "Helpers.hpp"
#include "Time.hpp"

#ifndef MAX_PATH
// Match Windows behaviour
#define MAX_PATH 260
#endif

// Taken from https://stackoverflow.com/a/51846880/2317956
// These codes set the actual text to the specified colour
#define RESETTEXT  "\x1B[0m" // Set all colours back to normal.
#define FOREBLK  "\x1B[30m" // Black
#define FORERED  "\x1B[31m" // Red
#define FOREGRN  "\x1B[32m" // Green
#define FOREYEL  "\x1B[33m" // Yellow
#define FOREBLU  "\x1B[34m" // Blue
#define FOREMAG  "\x1B[35m" // Magenta
#define FORECYN  "\x1B[36m" // Cyan
#define FOREWHT  "\x1B[37m" // White

/* BACKGROUND */
// These codes set the background colour behind the text.
#define BACKBLK "\x1B[40m"
#define BACKRED "\x1B[41m"
#define BACKGRN "\x1B[42m"
#define BACKYEL "\x1B[43m"
#define BACKBLU "\x1B[44m"
#define BACKMAG "\x1B[45m"
#define BACKCYN "\x1B[46m"
#define BACKWHT "\x1B[47m"

#define BOLD(x) "\x1B[1m" x RESETTEXT // Embolden text then reset it.
#define BRIGHT(x) "\x1B[1m" x RESETTEXT // Brighten text then reset it. (Same as bold but is available for program clarity)
#define UNDL(x) "\x1B[4m" x RESETTEXT // Underline text then reset it.

namespace flex
{
	std::vector<pthread_t> ThreadHandles;
	std::vector<pthread_mutex_t> mutexes;

	CPUInfo Platform::cpuInfo;

	void Platform::Init()
	{
		RetrieveCPUInfo();
	}

	void Platform::GetConsoleHandle()
	{
	}

	void Platform::SetConsoleTextColour(ConsoleColour colour)
	{
#if ENABLE_CONSOLE_COLOURS
		static const char* const w_colours[] = { FOREWHT, FOREYEL, FORERED };

		std::cout << w_colours[(u32)colour];
#else
		FLEX_UNUSED(colour);
#endif
	}

	bool Platform::IsProcessRunning(u32 PID)
	{
		// Send blank signal to process
		i32 result = kill(PID, 0);
		// i32 err = errno;
		return result != 0;
	}

	u32 Platform::GetCurrentProcessID()
	{
		return (u32)getpid();
	}

	void Platform::MoveConsole(i32 width /* = 800 */, i32 height /* = 800 */)
	{
		/* TODO

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
		*/
	}

	void Platform::PrintStringToDebuggerConsole(const char* str)
	{
		// Linux doesn't have an equivalent to Windows' OutputDebugString
		FLEX_UNUSED(str);
	}

	void Platform::RetrieveCurrentWorkingDirectory()
	{
		char cwdBuffer[MAX_PATH];
		char* str = getcwd(cwdBuffer, sizeof(cwdBuffer));
		if (str)
		{
			FlexEngine::s_CurrentWorkingDirectory = ReplaceBackSlashesWithForward(str);
		}
	}

	void Platform::RetrievePathToExecutable()
	{
		char result[PATH_MAX];
		ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
		if (count != -1)
		{
			const char* path = dirname(result);
			FlexEngine::s_ExecutablePath = RelativePathToAbsolute(ReplaceBackSlashesWithForward(std::string(path) + "/"));

			// Change current directory so relative paths work
			std::string appDir = ExtractDirectoryString(FlexEngine::s_ExecutablePath);
			if (chdir(appDir.c_str()) != 0)
			{
				PrintError("Failed to change directory to %s (errorno: %u)\n", appDir.c_str(), errno);
			}
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

		i32 status = 0;
		mode_t mode = S_IRWXU; // RWX for owner (see https://jameshfisher.com/2017/02/24/what-is-mode_t/)
		struct stat st;
		if (stat(absoluteDirectoryPath.c_str(), &st) != 0)
		{
			if (mkdir(absoluteDirectoryPath.c_str(), mode) != 0 && errno != EEXIST)
			{
				status = -1;
			}
		}
		else if (!S_ISDIR(st.st_mode))
		{
			errno = ENOTDIR;
			status = -1;
		}

		return status != -1;
	}

	void Platform::OpenFileExplorer(const char* absoluteDirectory)
	{
		// TODO: Strip from release builds
		// Linux doesn't quite have the same guaranteed idea of a "file explorer" as Windows
		char buf[256];
		// This works on Ubuntu at least (TODO: Support more distros)
		snprintf(buf, 256, "nautilus --browser %s", absoluteDirectory);
		system(buf);
	}

	bool Platform::DirectoryExists(const std::string& absoluteDirectoryPath)
	{
		if (absoluteDirectoryPath.find("..") != std::string::npos)
		{
			PrintError("Attempted to create directory using relative path! Must specify absolute path!\n");
			return false;
		}

		struct stat st;
		if (stat(absoluteDirectoryPath.c_str(), &st) != 0)
		{
			return S_ISDIR(st.st_mode);
		}

		return false;
	}

	bool Platform::DeleteFile(const std::string& filePath, bool bPrintErrorOnFailure /* = true */)
	{
		i32 result = unlink(filePath.c_str());
		if (!result)
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

	bool Platform::GetFileModifcationTime(const char* filePath, Date& outModificationDate)
	{
		struct stat st;
		int result = -1;
		// For some reason it sometimes takes a few attempts to stat a file
		int i = 0;
		while (i < 3 && result != 0)
		{
			result = stat(filePath, &st);
			++i;
		}

		if (result == 0)
		{
			tm* timeUTC = gmtime(&st.st_mtime);

			outModificationDate.year = timeUTC->tm_year;
			outModificationDate.month = timeUTC->tm_mon;
			outModificationDate.day = timeUTC->tm_mday;
			outModificationDate.hour = timeUTC->tm_hour;
			outModificationDate.minute = timeUTC->tm_min;
			outModificationDate.second = timeUTC->tm_sec;
			outModificationDate.millisecond = 0;

			return true;
		}

		PrintError("Failed to stat %s, result: %i, errorno: %u\n", filePath, result, errno);

		return false;
	}

	// TODO: Test!
	bool Platform::FindFilesInDirectory(const std::string& directoryPath, std::vector<std::string>& filePaths, const std::string& fileTypeFilter, bool bRecurse /* = false */)
	{
		std::string cleanedDirPath = directoryPath;
		if (cleanedDirPath[cleanedDirPath.size() - 1] != '/')
		{
			cleanedDirPath += '/';
		}

		std::string cleanedFileTypeFilter = fileTypeFilter;
		{
			size_t dotPos = cleanedFileTypeFilter.find('.');
			if (dotPos != std::string::npos)
			{
				cleanedFileTypeFilter.erase(dotPos, 1);
			}
		}

		FindFilesInDirectoryInternal(cleanedDirPath, filePaths, [&](const std::string& fileNameStr)
		{
			if (cleanedFileTypeFilter == "*")
			{
				return true;
			}
			else
			{
				std::string fileType = ExtractFileType(fileNameStr);
				return fileType == cleanedFileTypeFilter;
			}
		}, bRecurse);

		return !filePaths.empty();
	}

	// TODO: Test!
	bool Platform::FindFilesInDirectory(const std::string& directoryPath, std::vector<std::string>& filePaths, const char* fileTypes[], u32 fileTypesLen, bool bRecurse /* = false */)
	{
		std::string cleanedDirPath = directoryPath;
		if (cleanedDirPath[cleanedDirPath.size() - 1] != '/')
		{
			cleanedDirPath += '/';
		}

		FindFilesInDirectoryInternal(cleanedDirPath, filePaths, [&](const std::string& fileNameStr)
		{
			std::string fileType = ExtractFileType(fileNameStr);
			return Contains(fileTypes, fileTypesLen, fileType.c_str());
		}, bRecurse);

		return !filePaths.empty();
	}

	// Returns true if search succeeded
	bool Platform::FindFilesInDirectoryInternal(const std::string& directoryPath, std::vector<std::string>& filePaths, std::function<bool(const std::string&)> fileTypeFilter, bool bRecurse)
	{
		struct dirent* ent;
		DIR* dir = opendir(directoryPath.c_str());
		if (dir != NULL)
		{
			// Iterate over all files and directories within directory
			while ((ent = readdir(dir)) != NULL)
			{
				std::string fileNameStr(ent->d_name);
				if (fileNameStr != "." && fileNameStr != "..")
				{
					struct stat path_stat;
					stat(fileNameStr.c_str(), &path_stat);
					bool bIsDir = S_ISREG(path_stat.st_mode) != 0;
					if (bIsDir)
					{
						if (bRecurse)
						{
							// Recurse into directory
							FindFilesInDirectoryInternal(directoryPath + fileNameStr + "/", filePaths, fileTypeFilter, bRecurse);
						}
					}
					else
					{
						if (fileTypeFilter(fileNameStr))
						{
							filePaths.push_back(directoryPath + fileNameStr);
						}
					}
				}
			}

			closedir(dir);
		}
		else
		{
			// Could not open directory
			PrintError("Error encountered while finding files in directory %s\n", directoryPath.c_str());
			return false;
		}

		return true;
	}

	bool Platform::OpenFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath, char filter[] /* = nullptr */)
	{
		FLEX_UNUSED(windowTitle);
		FLEX_UNUSED(absoluteDirectory);
		FLEX_UNUSED(outSelectedAbsFilePath);
		FLEX_UNUSED(filter);

		// TODO: https://github.com/mlabbe/nativefiledialog ?
		// OPENFILENAME openFileName = {};
		// openFileName.lStructSize = sizeof(OPENFILENAME);
		// openFileName.lpstrInitialDir = absoluteDirectory.c_str();
		// openFileName.nMaxFile = (filter == nullptr ? 0 : strlen(filter));
		// if (openFileName.nMaxFile && filter)
		// {
			// openFileName.lpstrFilter = filter;
		// }
		// openFileName.nFilterIndex = 0;
		// const i32 MAX_FILE_PATH_LEN = 512;
		// char fileBuf[MAX_FILE_PATH_LEN];
		// memset(fileBuf, '\0', MAX_FILE_PATH_LEN - 1);
		// openFileName.lpstrFile = fileBuf;
		// openFileName.nMaxFile = MAX_FILE_PATH_LEN;
		// openFileName.lpstrTitle = windowTitle.c_str();
		// openFileName.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
		// bool bSuccess = GetOpenFileName(&openFileName) == 1;

		// if (openFileName.lpstrFile)
		// {
		// 	outSelectedAbsFilePath = ReplaceBackSlashesWithForward(openFileName.lpstrFile);
		// }

		// return bSuccess;
		return false;
	}

	void Platform::OpenFileWithDefaultApplication(const std::string& absoluteDirectory)
	{
		std::string pString = "xdg-open " + absoluteDirectory;
		popen(pString.c_str(), "w");
	}

	void Platform::LaunchApplication(const std::string& applicationName, const std::string& param0)
	{
		// TODO: Strip out of release builds!
		std::string s = "./" + applicationName + " " + param0.c_str();
		system(s.c_str());
	}

	std::string Platform::GetDateString_YMD()
	{
		std::stringstream result;

		char timeStrBuf[256];
		std::time_t t = std::time(nullptr);
		if (std::strftime(timeStrBuf, 256, "%Y %m %d", std::localtime(&t))) // In format "YYYY MM DD"
		{
			std::string timeStr(timeStrBuf);
			result << timeStr.substr(0, 4) << '-' <<
				timeStr.substr(5, 2) << '-' <<
				timeStr.substr(8, 2);
		}

		return result.str();
	}

	std::string Platform::GetDateString_YMDHMS()
	{
		std::stringstream result;

		char timeStrBuf[256];
		std::time_t t = std::time(nullptr);
		if (std::strftime(timeStrBuf, 256, "%Y %m %d %I %M %S", std::localtime(&t))) // In format "YYYY MM DD HH MM SS"
		{
			std::string timeStr(timeStrBuf);
			result << timeStr.substr(0, 4) << '-' <<
				timeStr.substr(5, 2) << '-' <<
				timeStr.substr(8, 2) << '-' <<
				timeStr.substr(11, 2) << '-' <<
				timeStr.substr(14, 2) << '-' <<
				timeStr.substr(17, 2);
		}

		return result.str();
	}

	u64 Platform::GetUSSinceEpoch()
	{
		struct timeval tv;

		gettimeofday(&tv, NULL);

		u64 result = tv.tv_usec;

		// Convert from micro seconds (10^-6) to milliseconds (10^-3)
		//result /= 1000;

		// Adds the seconds (10^0) after converting them to milliseconds (10^-3)
		//result += (tv.tv_sec * 1000);

		// Adds the seconds (10^0)
		result += (tv.tv_sec * 1000000);

		return result;
	}

	GameObjectID Platform::GenerateGUID()
	{
		uuid_t uuid;
		uuid_generate_random(uuid);

		GameObjectID result(*(u64*)uuid, *((u64*)uuid + 1));
		return result;
	}

	u32 Platform::AtomicIncrement(volatile u32* value)
	{
		return __sync_fetch_and_add(value, 1);
	}

	u32 Platform::AtomicDecrement(volatile u32* value)
	{
		return __sync_fetch_and_sub(value, 1);
	}

	u32 Platform::AtomicCompareExchange(volatile u32* value, u32 exchange, u32 comparand)
	{
		return __sync_val_compare_and_swap(value, comparand, exchange);
	}

	u32 Platform::AtomicExchange(volatile u32* value, u32 exchange)
	{
		u32 res = __sync_lock_test_and_set(value, exchange);
		// __sync_lock_release(value); // TODO: ?
		return res;
	}

	void Platform::JoinThreads()
	{
		for (u32 i = 0; i < (u32)ThreadHandles.size(); ++i)
		{
			pthread_join(ThreadHandles[i], NULL);
		}
		ThreadHandles.clear();
	}

	void Platform::SpawnThreads(u32 threadCount, void* (entryPoint)(void*), void* userData)
	{
		ThreadHandles.resize(threadCount);

		for (u32 i = 0; i < (u32)ThreadHandles.size(); ++i)
		{
			i32 result = pthread_create(&ThreadHandles[i], 0, entryPoint, userData);
			if (result)
			{
				const char* errorStr = result == EAGAIN ? "Insufficient resources" : result == EINVAL ? "Invalid settings" : result == EPERM ? "No permission to set scheduling policy requested" : "unknown";
				PrintError("Failed to create thread, error: %s\n", errorStr);
			}
		}
	}

	void Platform::YieldProcessor()
	{
		sched_yield();
	}

	bool Platform::SetFlexThreadAffinityMask(void* threadHandle, u64 threadID)
	{
		int ret;
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		size_t cpusetsize = sizeof(cpuset);

		CPU_SET(threadID, &cpuset);
		ret = pthread_setaffinity_np((pthread_t)threadHandle, cpusetsize, &cpuset);
		return ret != 0;
	}

	bool Platform::SetFlexThreadName(void* threadHandle, const char* threadName)
	{
		ret = pthread_setname_np(threadHandle, threadName);
		return ret != 0;
	}

	void* Platform::InitCriticalSection()
	{
		u32 index = mutexes.size();

		for (u32 i = 0; i < (u32)mutexes.size(); ++i)
		{
			if (mutexes[i].__data.__owner == 0)
			{
				index = i;
				break;
			}
		}

		if (index >= (u32)mutexes.size())
		{
			mutexes.resize((u32)((index + 1) * 1.5f));
		}

		mutexes[index] = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

		return (void*)(u64)index;
	}

	void Platform::FreeCriticalSection(void* criticalSection)
	{
		mutexes[(u64)criticalSection] = {};
	}

	void Platform::EnterCriticalSection(void* criticalSection)
	{
		pthread_mutex_lock(&mutexes[(u64)criticalSection]);
	}

	void Platform::LeaveCriticalSection(void* criticalSection)
	{
		pthread_mutex_unlock(&mutexes[(u64)criticalSection]);
	}

	void Platform::Sleep(ms milliseconds)
	{
		usleep((useconds_t)Time::ConvertFormats(milliseconds, Time::Format::MILLISECOND, Time::Format::MICROSECOND));
	}

	void Platform::PrintStackTrace()
	{
		void* callstack[32];
		i32 frames = backtrace(callstack, ARRAY_LENGTH(callstack));
		char** strs = backtrace_symbols(callstack, frames);
		for (i32 i = 0; i < frames; ++i)
		{
			char moduleName[1024] = {};
			char functionSymbol[1024] = {};
			char offset[64] = {};
			i32 validCppName = 0;
			char* demangledFunctionSymbol = nullptr;

			const char* ptr = strs[i];
			char* mptr = moduleName;
			while (*ptr && *ptr != '(')
			{
				*mptr++ = *ptr++;
			}
			*mptr = '\0';
			++ptr;
			if (*ptr == '+')
			{
				strcpy(functionSymbol, "(unknown)");
				++ptr;
			}
			else
			{
				// Copy mangled function name
				char* fptr = functionSymbol;
				while (*ptr && *ptr != '+')
				{
					*fptr++ = *ptr++;
				}
				*fptr = '\0';

				if (*ptr != '+')
				{
					fprintf(stderr, "Unable to decode frame: %s\n", strs[i]);
					continue;
				}
				++ptr;
			}

			char* optr = offset;
			while (*ptr && *ptr != ')')
			{
				*optr++ = *ptr++;
			}
			*optr = '\0';

			validCppName = 0;
			demangledFunctionSymbol = abi::__cxa_demangle(functionSymbol, NULL, 0, &validCppName);

			if (validCppName == 0)
			{
				fprintf(stderr, "(%-40s)\t0x%p - %s + %s\n", moduleName, callstack[i],
					demangledFunctionSymbol, offset);
			}
			else
			{
				fprintf(stderr, "(%-40s)\t0x%p - %s + %s\n", moduleName, callstack[i],
					functionSymbol, offset);
			}
		}
		free(strs);
	}

	void Platform::RetrieveCPUInfo()
	{
		cpuInfo = {};
		cpuInfo.logicalProcessorCount = sysconf(_SC_NPROCESSORS_ONLN);
		cpuInfo.physicalCoreCount = sysconf(_SC_NPROCESSORS_CONF);
		// TODO: Store these in appropriate fields
		cpuInfo.l1CacheCount = sysconf(_SC_LEVEL1_ICACHE_SIZE);
		cpuInfo.l2CacheCount = sysconf(_SC_LEVEL2_CACHE_SIZE);
		cpuInfo.l3CacheCount = sysconf(_SC_LEVEL3_CACHE_SIZE);
	}

	DirectoryWatcher::DirectoryWatcher(const std::string& directory, bool bWatchSubtree) :
		directory(directory),
		m_bWatchSubtree(bWatchSubtree)
	{
		// TODO: Add support for directory watching on linux
		m_bInstalled = false;
	}

	DirectoryWatcher::~DirectoryWatcher()
	{
	}

	bool DirectoryWatcher::Update()
	{
		// TODO: Unimplemented
		return false;
	}

	bool DirectoryWatcher::Installed() const
	{
		return m_bInstalled;
	}
} // namespace flex
#endif // linux
