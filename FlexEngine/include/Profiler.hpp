#pragma once

#include <unordered_map>

#include "Helpers.hpp"

namespace flex
{
	class Profiler
	{
	public:

		static void StartFrame();
		static void EndFrame(bool bPrintTimings);

		static void Begin(const char* blockName);
		static void Begin(const std::string& blockName);
		static void End(const char* blockName);
		static void End(const std::string& blockName);

		static void PrintResultsToFile();

		static void PrintBlockDuration(const char* blockName);
		static void PrintBlockDuration(const std::string& blockName);

	private:
		static u64 Hash(const char* str);

		// Second field holds start time of block until block is ended, then it contains block duration
		static std::unordered_map<u64, ms> s_Timings;
		static ms s_FrameStartTime;
		static ms s_FrameEndTime;

		static i32 s_UnendedTimings;

		static std::string s_PendingCSV;

	};
} // namespace flex
