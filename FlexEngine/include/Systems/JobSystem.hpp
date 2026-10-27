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

		// Parallel for: runs task jobCount times (JobArgs::jobIndex in [0, jobCount)) spread across the worker threads,
		// in groups of groupSize jobs. Call Wait to block until they've all finished.
		void Dispatch(Context& ctx, u32 jobCount, u32 groupSize, const std::function<void(JobArgs)>& task);

		// For work which takes a while but isn't needed this frame (e.g. building collision for streamed terrain). Only
		// worker threads run these (never a thread in Wait, so they can't stall the main thread), at most half of them
		// at a time, & only once they have no regular jobs. Poll IsBusy to find out when they've finished.
		void ExecuteBackground(Context& ctx, const std::function<void()>& task);

		// free()s memory on a worker thread (as a background job, batched until FlushBackgroundFrees, which is called
		// once per frame). Freeing large blocks can take a while (the OS unmaps their pages, & the heap is contended
		// by the terrain generation threads), which adds up when e.g. many terrain chunks are unloaded at once.
		void FreeInBackground(void* ptr);
		// Calls deleter(ptr) on a worker thread (batched like FreeInBackground)
		void DestroyInBackground(void* ptr, void (*deleter)(void*));
		// Deletes an object on a worker thread (e.g. a large std::vector, moved into a new one)
		template<typename T>
		void DeleteInBackground(T* object)
		{
			DestroyInBackground(object, [](void* ptr) { delete (T*)ptr; });
		}
		void FlushBackgroundFrees();

		// Check if any threads are working currently or not
		bool IsBusy(const Context& ctx);

		// Wait until all threads become idle
		// Current thread will become a worker thread, executing jobs
		void Wait(const Context& ctx);
	} // namespace JobSystem
} // namespace flex
