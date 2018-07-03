#pragma once

namespace flex
{
	class Profiler
	{
	public:

		static void StartFrame();
		static void EndFrame(bool bPrintTimings);

		static void Begin(const std::string& blockName);
		static void End(const std::string& blockName);

		static void PrintResultsToFile();

		static void PrintBlockDuration(const std::string& blockName);

	private:
		// Second field holds start time of block until block is ended, then it contains block duration
		static std::map<std::string, ms> s_Timings;
		static ms s_FrameStartTime;
		static ms s_FrameEndTime;

		static i32 s_UnendedTimings;

		static std::string s_PendingCSV;

	};
} // namespace flex
