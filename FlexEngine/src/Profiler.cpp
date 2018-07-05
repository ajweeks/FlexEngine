#include "stdafx.hpp"

#include "Profiler.hpp"

#include "Time.hpp"

namespace flex
{
	i32 Profiler::s_UnendedTimings = 0;
	ms Profiler::s_FrameStartTime = 0;
	ms Profiler::s_FrameEndTime = 0;
	std::unordered_map<u64, ms> Profiler::s_Timings;
	std::string Profiler::s_PendingCSV;

	// Any frame time longer than this will be clipped to this value
	const ms MAX_FRAME_TIME = 100;

	void Profiler::StartFrame()
	{
		s_Timings.clear();
		s_UnendedTimings = 0;
		s_FrameStartTime = Time::CurrentMilliseconds();
	}

	void Profiler::EndFrame(bool bPrintTimings)
	{
		s_FrameEndTime = Time::CurrentMilliseconds();

		if (s_UnendedTimings != 0)
		{
			Logger::LogError("Uneven number of profile blocks! (" +
							 std::to_string(s_UnendedTimings) + ')');
		}

		if (bPrintTimings)
		{
			ms frameTime = glm::min(s_FrameEndTime - s_FrameStartTime, MAX_FRAME_TIME);
			s_PendingCSV.append(std::to_string(frameTime) + ",");

			//Logger::LogInfo("Profiler results:");
			//Logger::LogInfo("Whole frame: " + std::to_string(s_FrameEndTime - s_FrameStartTime) + "ms");
			//Logger::LogInfo("---");
			for (auto element : s_Timings)
			{
				//s_PendingCSV.append(std::string(element.first) + "," +
				//					std::to_string(element.second) + '\n');

				//Logger::LogInfo(std::string(element.first) + ": " + 
				//				std::to_string(element.second) + "ms");
			}
		}
	}

	void Profiler::Begin(const char* blockName)
	{
		u64 hash = Hash(blockName);

		if (s_Timings.find(hash) != s_Timings.end())
		{
			Logger::LogError("Profiler::Begin called more than once! Block name: " + std::string(blockName) +
							 " (hash: " + std::to_string(hash) + ')');
		}

		ms now = Time::CurrentMilliseconds();
		s_Timings.insert({ hash, now });

		++s_UnendedTimings;
	}

	void Profiler::Begin(const std::string& blockName)
	{
		Begin(blockName.c_str());
	}

	void Profiler::End(const char* blockName)
	{
		u64 hash = Hash(blockName);

		auto result = s_Timings.find(hash);

		if (result == s_Timings.end())
		{
			Logger::LogError("Profiler::End called before Begin was called! Block name: " + std::string(blockName) +
							 " (hash: " + std::to_string(hash) + ')');
			return;
		}

		ms now = Time::CurrentMilliseconds();
		ms start = result->second;
		ms elapsed = now - start;
		s_Timings.at(hash) = elapsed;

		--s_UnendedTimings;
	}

	void Profiler::End(const std::string& blockName)
	{
		End(blockName.c_str());
	}

	void Profiler::PrintResultsToFile()
	{
		if (s_PendingCSV.empty())
		{
			Logger::LogWarning("Attempted to print profiler results to file before any results were generated! Did you set bPrintTimings when calling EndFrame?");
			return;
		}

		std::string directory = RESOURCE_LOCATION + "profiles/";
		std::string absoluteDirectory = RelativePathToAbsolute(directory);
		CreateDirectoryRecursive(absoluteDirectory);

		std::string filePath = absoluteDirectory + "frame_times.csv";

		if (WriteFile(filePath, s_PendingCSV, false))
		{
			Logger::LogInfo("Wrote profiling results to " + filePath);
		}
		else
		{
			Logger::LogInfo("Failed to write profiling results to " + filePath);
		}
	}

	void Profiler::PrintBlockDuration(const char* blockName)
	{
		u64 hash = Hash(blockName);

		auto result = s_Timings.find(hash);
		if (result != s_Timings.end())
		{
			Logger::LogInfo("    Block duration \"" + std::string(blockName) + "\": " + FloatToString(result->second, 2) + "ms");
		}
	}

	void Profiler::PrintBlockDuration(const std::string& blockName)
	{
		PrintBlockDuration(blockName.c_str());
	}

	u64 Profiler::Hash(const char* str)
	{
		size_t result = std::hash<std::string>{}(str);
		
		return (u64)result;
	}
} // namespace flex
