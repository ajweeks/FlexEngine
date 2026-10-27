#include "stdafx.hpp"

#include "Profiler.hpp"

#include "Helpers.hpp"
#include "Platform/Platform.hpp"

namespace flex
{
	namespace Profiler
	{
		namespace
		{
			struct Stats
			{
				const char* name = nullptr;
				ns totalTime = 0.0f; // Inclusive of nested scopes
				ns selfTime = 0.0f; // Excluding nested scopes
				ns maxFrameTime = 0.0f;
				ns frameTime = 0.0f;
				ns frameSelfTime = 0.0f;
				u64 calls = 0;
				u32 depth = 0;
			};

			struct OpenScope
			{
				u32 statsIndex;
				std::chrono::high_resolution_clock::time_point start;
				ns childTime;
			};

			std::atomic<bool> s_bEnabled{ false };
			std::thread::id s_MainThreadID;
			// Keyed by name pointer (names are string literals, or at least outlive the profiler)
			std::unordered_map<const char*, u32> s_StatsIndices;
			std::vector<Stats> s_Stats;
			std::vector<OpenScope> s_Stack;
			// Stable storage for names which aren't string literals
			std::set<std::string> s_InternedNames;
			std::chrono::high_resolution_clock::time_point s_FrameStart;
			ns s_TotalFrameTime = 0.0f;
			ns s_MaxFrameTime = 0.0f;
			// Time spent in top level scopes this frame
			ns s_FrameTopLevelTime = 0.0f;
			// Main thread CPU time at the end of the last frame (ns), to tell hitches where the thread was busy from ones
			// where it was waiting (e.g. on the GPU, or preempted by other processes)
			u64 s_LastFrameCPUTime = 0;
			u32 s_FrameCount = 0;

			// Every frame's duration (ms) since the last reset, for the distribution in the report
			std::vector<real> s_FrameTimes;
			// The most recent frame times (ms), whose median is what hitches are compared against
			const u32 RECENT_FRAME_COUNT = 61;
			real s_RecentFrameTimes[RECENT_FRAME_COUNT];
			u32 s_RecentFrameCount = 0;
			u32 s_RecentFrameIndex = 0;
			bool s_bLogHitches = false;
			std::string s_PendingHitchLine;
			u32 s_HitchCount = 0;

			void LogHitch(real frameMs, real medianMs, real cpuMs)
			{
				++s_HitchCount;

				// Scopes sorted by their self time this frame (a parent's self time is code which isn't profiled more finely)
				std::vector<const Stats*> scopes;
				for (const Stats& stats : s_Stats)
				{
					if (stats.frameTime > 0.0f && stats.name[0] != '#' && strncmp(stats.name, "GPU ", 4) != 0)
					{
						scopes.push_back(&stats);
					}
				}
				std::sort(scopes.begin(), scopes.end(), [](const Stats* a, const Stats* b) { return a->frameSelfTime > b->frameSelfTime; });

				// Time outside of any top level scope (e.g. waiting in the OS, or code which isn't profiled at all)
				const real unprofiledMs = glm::max(frameMs - s_FrameTopLevelTime / 1e6f, 0.0f);

				std::string line = "Hitch: frame " + std::to_string(s_FrameCount) + " took " + FloatToString(frameMs, 1) + "ms (median " + FloatToString(medianMs, 1) +
					"ms, main thread ran for " + FloatToString(cpuMs, 1) + "ms, " + FloatToString(unprofiledMs, 1) + "ms outside top level scopes). Self ms:";
				for (u32 i = 0; i < (u32)scopes.size() && i < 8 && scopes[i]->frameSelfTime >= 250'000.0f; ++i)
				{
					line += " " + std::string(scopes[i]->name) + " " + FloatToString(scopes[i]->frameSelfTime / 1e6f, 1);
					if (scopes[i]->frameTime > scopes[i]->frameSelfTime + 250'000.0f)
					{
						line += " (incl " + FloatToString(scopes[i]->frameTime / 1e6f, 1) + ")";
					}
					line += ",";
				}
				// GPU timings are only read back during the next frame, so the line is printed then (see PrintPendingHitch)
				s_PendingHitchLine = line;
			}

			// Prints the pending hitch line with this frame's GPU samples (which were measured for the previous frame)
			void PrintPendingHitch()
			{
				std::vector<const Stats*> gpuPasses;
				for (const Stats& stats : s_Stats)
				{
					if (stats.frameTime > 0.0f && strncmp(stats.name, "GPU ", 4) == 0)
					{
						gpuPasses.push_back(&stats);
					}
				}
				std::sort(gpuPasses.begin(), gpuPasses.end(), [](const Stats* a, const Stats* b) { return a->frameTime > b->frameTime; });

				std::string line = s_PendingHitchLine + " | GPU ms:";
				for (u32 i = 0; i < (u32)gpuPasses.size() && i < 5 && gpuPasses[i]->frameTime >= 500'000.0f; ++i)
				{
					line += " " + std::string(gpuPasses[i]->name + 4) + " " + FloatToString(gpuPasses[i]->frameTime / 1e6f, 1) + ",";
				}
				line += "\n";
				Print("%s", line.c_str());
				s_PendingHitchLine.clear();
			}

			u32 GetStatsIndex(const char* name)
			{
				auto iter = s_StatsIndices.find(name);
				if (iter != s_StatsIndices.end())
				{
					return iter->second;
				}
				const u32 index = (u32)s_Stats.size();
				s_StatsIndices.emplace(name, index);
				Stats stats = {};
				stats.name = name;
				stats.depth = (u32)s_Stack.size();
				s_Stats.push_back(stats);
				return index;
			}
		} // anonymous namespace

		void SetEnabled(bool bEnabled)
		{
			s_MainThreadID = std::this_thread::get_id();
			s_FrameStart = std::chrono::high_resolution_clock::now();
			s_bEnabled = bEnabled;
		}

		bool IsEnabled()
		{
			return s_bEnabled.load(std::memory_order_relaxed);
		}

		void BeginScope(const char* name)
		{
			if (std::this_thread::get_id() != s_MainThreadID)
			{
				return;
			}
			s_Stack.push_back({ GetStatsIndex(name != nullptr ? name : "?"), std::chrono::high_resolution_clock::now(), 0.0f });
		}

		void EndScope()
		{
			if (std::this_thread::get_id() != s_MainThreadID || s_Stack.empty())
			{
				return;
			}

			const OpenScope scope = s_Stack.back();
			s_Stack.pop_back();
			const ns duration = (ns)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - scope.start).count();
			Stats& stats = s_Stats[scope.statsIndex];
			stats.totalTime += duration;
			stats.frameTime += duration;
			stats.selfTime += duration - scope.childTime;
			stats.frameSelfTime += duration - scope.childTime;
			++stats.calls;
			if (!s_Stack.empty())
			{
				s_Stack.back().childTime += duration;
			}
			else
			{
				s_FrameTopLevelTime += duration;
			}
		}

		void AddSample(const char* name, ms duration)
		{
			if (!IsEnabled())
			{
				return;
			}
			const char* internedName = s_InternedNames.insert(name).first->c_str();
			Stats& stats = s_Stats[GetStatsIndex(internedName)];
			const ns durationNs = duration * 1'000'000.0f;
			stats.totalTime += durationNs;
			stats.selfTime += durationNs;
			stats.frameTime += durationNs;
			stats.frameSelfTime += durationNs;
			++stats.calls;
		}

		void SetHitchLogging(bool bEnabled)
		{
			s_bLogHitches = bEnabled;
		}

		void EndFrame()
		{
			if (!IsEnabled())
			{
				return;
			}

			const auto now = std::chrono::high_resolution_clock::now();
			const ns frameTime = (ns)std::chrono::duration_cast<std::chrono::nanoseconds>(now - s_FrameStart).count();
			s_FrameStart = now;
			s_TotalFrameTime += frameTime;
			s_MaxFrameTime = glm::max(s_MaxFrameTime, frameTime);
			++s_FrameCount;

			if (!s_PendingHitchLine.empty())
			{
				PrintPendingHitch();
				// (Logging isn't counted against the next frame)
				s_FrameStart = std::chrono::high_resolution_clock::now();
			}

			const real frameMs = frameTime / 1e6f;
			s_FrameTimes.push_back(frameMs);
			const u64 cpuTime = Platform::GetCurrentThreadCPUTimeNs();
			const real cpuMs = s_LastFrameCPUTime != 0 ? (real)((double)(cpuTime - s_LastFrameCPUTime) / 1e6) : frameMs;
			s_LastFrameCPUTime = cpuTime;
			if (s_bLogHitches && s_RecentFrameCount >= 15)
			{
				real recent[RECENT_FRAME_COUNT];
				memcpy(recent, s_RecentFrameTimes, s_RecentFrameCount * sizeof(real));
				std::nth_element(recent, recent + s_RecentFrameCount / 2, recent + s_RecentFrameCount);
				const real medianMs = recent[s_RecentFrameCount / 2];
				// Ignore tiny absolute differences (e.g. 1ms frames which took 2.5ms)
				if (frameMs > medianMs * 2.0f && frameMs - medianMs > 4.0f)
				{
					LogHitch(frameMs, medianMs, cpuMs);
					// Don't count the time spent logging against the next frame
					s_FrameStart = std::chrono::high_resolution_clock::now();
					s_LastFrameCPUTime = Platform::GetCurrentThreadCPUTimeNs();
				}
			}
			s_RecentFrameTimes[s_RecentFrameIndex] = frameMs;
			s_RecentFrameIndex = (s_RecentFrameIndex + 1) % RECENT_FRAME_COUNT;
			s_RecentFrameCount = glm::min(s_RecentFrameCount + 1, RECENT_FRAME_COUNT);

			for (Stats& stats : s_Stats)
			{
				stats.maxFrameTime = glm::max(stats.maxFrameTime, stats.frameTime);
				stats.frameTime = 0.0f;
				stats.frameSelfTime = 0.0f;
			}

			// Unbalanced begin/end pairs shouldn't leak into the next frame
			s_Stack.clear();
			s_FrameTopLevelTime = 0.0f;
		}

		void Reset()
		{
			s_StatsIndices.clear();
			s_Stats.clear();
			s_Stack.clear();
			s_TotalFrameTime = 0.0f;
			s_MaxFrameTime = 0.0f;
			s_FrameCount = 0;
			s_FrameTimes.clear();
			s_PendingHitchLine.clear();
			s_LastFrameCPUTime = 0;
			s_RecentFrameCount = 0;
			s_RecentFrameIndex = 0;
			s_HitchCount = 0;
			s_FrameStart = std::chrono::high_resolution_clock::now();
		}

		void PrintReport(bool bTotals /* = false */)
		{
			if (s_FrameCount == 0)
			{
				return;
			}

			// The same name may appear with different pointers (string literals in different translation units)
			std::map<std::string, Stats> merged;
			for (const Stats& stats : s_Stats)
			{
				Stats& entry = merged[stats.name];
				entry.name = stats.name;
				entry.totalTime += stats.totalTime;
				entry.selfTime += stats.selfTime;
				entry.maxFrameTime = glm::max(entry.maxFrameTime, stats.maxFrameTime);
				entry.calls += stats.calls;
			}
			std::vector<const Stats*> sorted;
			for (const auto& pair : merged)
			{
				sorted.push_back(&pair.second);
			}
			std::sort(sorted.begin(), sorted.end(), [](const Stats* a, const Stats* b) { return a->totalTime > b->totalTime; });

			const real invFrames = bTotals ? 1.0f : 1.0f / (real)s_FrameCount;
			Print("\n=== Profile (%u frames, avg %.2fms/frame, max %.2fms) ===\n", s_FrameCount, s_TotalFrameTime / (real)s_FrameCount / 1e6f, s_MaxFrameTime / 1e6f);
			Print("  %9s %9s %9s %9s  %s\n", bTotals ? "total ms" : "avg ms", "self ms", "max ms", bTotals ? "calls" : "calls/f", "scope");
			const u32 maxLines = 90;
			for (u32 i = 0; i < (u32)sorted.size() && i < maxLines; ++i)
			{
				const Stats& stats = *sorted[i];
				Print("  %9.3f %9.3f %9.3f %9.1f  %s\n", stats.totalTime * invFrames / 1e6f, stats.selfTime * invFrames / 1e6f,
					stats.maxFrameTime / 1e6f, (real)stats.calls * invFrames, stats.name);
			}

			if (!s_FrameTimes.empty())
			{
				std::vector<real> sorted = s_FrameTimes;
				std::sort(sorted.begin(), sorted.end());
				auto Percentile = [&sorted](real p) { return sorted[glm::min((u32)(p * (real)sorted.size()), (u32)sorted.size() - 1)]; };
				const real median = Percentile(0.5f);
				u32 overTwiceMedian = 0;
				u32 over33ms = 0;
				for (real frameMs : s_FrameTimes)
				{
					overTwiceMedian += frameMs > median * 2.0f ? 1 : 0;
					over33ms += frameMs > 33.3f ? 1 : 0;
				}
				Print("Frame times (ms): p50 %.2f, p95 %.2f, p99 %.2f, p99.9 %.2f, max %.2f | %u frames over 2x median (%.2f%%), %u over 33ms",
					median, Percentile(0.95f), Percentile(0.99f), Percentile(0.999f), sorted.back(), overTwiceMedian,
					100.0f * overTwiceMedian / (real)sorted.size(), over33ms);
				Print(s_bLogHitches ? ", %u hitches logged\n" : "\n", s_HitchCount);
			}
		}
	} // namespace Profiler
} // namespace flex
