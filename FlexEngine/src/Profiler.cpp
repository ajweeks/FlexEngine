#include "stdafx.hpp"

#include "Profiler.hpp"

#include "Helpers.hpp"

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
			u32 s_FrameCount = 0;

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
			++stats.calls;
			if (!s_Stack.empty())
			{
				s_Stack.back().childTime += duration;
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
			++stats.calls;
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

			for (Stats& stats : s_Stats)
			{
				stats.maxFrameTime = glm::max(stats.maxFrameTime, stats.frameTime);
				stats.frameTime = 0.0f;
			}

			// Unbalanced begin/end pairs shouldn't leak into the next frame
			s_Stack.clear();
		}

		void Reset()
		{
			s_StatsIndices.clear();
			s_Stats.clear();
			s_Stack.clear();
			s_TotalFrameTime = 0.0f;
			s_MaxFrameTime = 0.0f;
			s_FrameCount = 0;
			s_FrameStart = std::chrono::high_resolution_clock::now();
		}

		void PrintReport()
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

			const real invFrames = 1.0f / (real)s_FrameCount;
			Print("\n=== Profile (%u frames, avg %.2fms/frame, max %.2fms) ===\n", s_FrameCount, s_TotalFrameTime * invFrames / 1e6f, s_MaxFrameTime / 1e6f);
			Print("  %9s %9s %9s %9s  %s\n", "avg ms", "self ms", "max ms", "calls/f", "scope");
			const u32 maxLines = 60;
			for (u32 i = 0; i < (u32)sorted.size() && i < maxLines; ++i)
			{
				const Stats& stats = *sorted[i];
				Print("  %9.3f %9.3f %9.3f %9.1f  %s\n", stats.totalTime * invFrames / 1e6f, stats.selfTime * invFrames / 1e6f,
					stats.maxFrameTime / 1e6f, (real)stats.calls * invFrames, stats.name);
			}
		}
	} // namespace Profiler
} // namespace flex
