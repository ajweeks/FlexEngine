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
			PrintError("Uneven number of profile blocks! (%i)\n", s_UnendedTimings);
		}

		if (bPrintTimings)
		{
			ms frameTime = glm::min(s_FrameEndTime - s_FrameStartTime, MAX_FRAME_TIME);
			s_PendingCSV.append(std::to_string(frameTime) + ",");

			//Print("Profiler results:");
			//Print("Whole frame: " + std::to_string(s_FrameEndTime - s_FrameStartTime) + "ms");
			//Print("---");
			//for (auto& element : s_Timings)
			//{
				//s_PendingCSV.append(std::string(element.first) + "," +
				//					std::to_string(element.second) + '\n');

				//Print(std::string(element.first) + ": " + 
				//				std::to_string(element.second) + "ms");
			//}
		}
	}

	void Profiler::Begin(const char* blockName)
	{
		u64 hash = Hash(blockName);

		if (s_Timings.find(hash) != s_Timings.end())
		{
			PrintError("Profiler::Begin called more than once! Block name: %s (hash: %i)\n",
					   blockName, hash);
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

		auto iter = s_Timings.find(hash);
		if (iter == s_Timings.end())
		{
			PrintError("Profiler::End called before Begin was called! Block name: %s (hash: %i)\n", 
					   blockName, hash);
			return;
		}

		ms now = Time::CurrentMilliseconds();
		ms start = iter->second;
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
			PrintWarn("Attempted to print profiler results to file before any results were generated!"
					  "Did you set bPrintTimings when calling EndFrame?\n");
			return;
		}

		std::string directory = RESOURCE_LOCATION + "profiles/";
		std::string absoluteDirectory = RelativePathToAbsolute(directory);
		CreateDirectoryRecursive(absoluteDirectory);

		std::string filePath = absoluteDirectory + "frame_times.csv";

		if (WriteFile(filePath, s_PendingCSV, false))
		{
			Print("Wrote profiling results to %s\n", filePath.c_str());
		}
		else
		{
			Print("Failed to write profiling results to %s\n", filePath.c_str());
		}
	}

	void Profiler::PrintBlockDuration(const char* blockName)
	{
		u64 hash = Hash(blockName);

		auto iter = s_Timings.find(hash);
		if (iter != s_Timings.end())
		{
			Print("    Block duration \"%s\": %.2fms\n", blockName, iter->second);
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
