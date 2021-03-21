#pragma once

#include <unordered_map>

#include "JSONTypes.hpp"

namespace flex
{
	struct AutoProfilerBlock
	{
		explicit AutoProfilerBlock(const char* blockName);
		~AutoProfilerBlock();

		const char* m_BlockName;
	};

	class Profiler
	{
	public:

		static void StartFrame();
		static void Update();
		static void EndFrame(bool bUpdateDisplayedFrame);

		static void Begin(const char* blockName);
		static void Begin(const std::string& blockName);
		static void End(const char* blockName);
		static void End(const std::string& blockName);

		static void PrintResultsToFile();

		static void PrintBlockDuration(const char* blockName);
		static void PrintBlockDuration(const std::string& blockName);

		static ms GetBlockDuration(const char* blockName);
		static ms GetBlockDuration(const std::string& blockName);

		static void DrawDisplayedFrame();

		static bool s_bDisplayingFrame;
		static bool s_bRecordingTrace;

	private:
		static u64 Hash(const char* str);

		struct Timing
		{
			static const u32 MAX_NAME_LEN = 128;

			ms start = real_max;
			ms end = real_min;
			char blockName[MAX_NAME_LEN];
		};

		static std::unordered_map<u64, Timing> s_Timings;
		static ms s_FrameStartTime;
		static ms s_FrameEndTime;

		static i32 s_UnendedTimings;

		static std::string s_PendingCSV;

		static std::vector<JSONObject> s_PendingTraceEvents;

		// Stores a single frame's timings to be displayed visually
		static std::vector<Timing> s_DisplayedFrameTimings;

		struct DisplayedFrameOptions
		{
			real screenWidthPercent = 0.8f;
			real screenHeightPercent = 0.4f;
			real xOffPercent = 0.0f;
			real yOffPercent = 0.0f;
			real opacity = 0.8f;
			real hZoom = 1.0f;
			real hScroll = 0.0f;
			real hO = 0.0f;
		};

		static const real s_ScrollSpeed;

		static DisplayedFrameOptions s_DisplayedFrameOptions;

		static glm::vec4 blockColours[];

	};
} // namespace flex
