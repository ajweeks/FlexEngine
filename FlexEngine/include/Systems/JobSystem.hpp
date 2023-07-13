#pragma once

#include <functional>
#include <atomic>

namespace flex
{
	// Initially inspired by turanszkij/WickedEngine
	namespace JobSystem
	{
		void Initialize(u32 maxThreadCount = ~0u);
		void Destroy();

		struct JobArgs
		{
			u32 jobIndex;
			void* sharedMemory;
			u64 payload;
		};

		u32 GetThreadCount();

		// Defines a state of execution, can be waited on
		struct Context
		{
			std::atomic<u32> counter{ 0 };
		};

		// Add a task to execute asynchronously. Any idle thread will execute this.
		void Execute(Context& ctx, const std::function<void(JobArgs)>& task, u64 payload);

		// Check if any threads are working currently or not
		bool IsBusy(const Context& ctx);

		// Wait until all threads become idle
		// Current thread will become a worker thread, executing jobs
		void Wait(const Context& ctx);
	} // namespace JobSystem
} // namespace flex
