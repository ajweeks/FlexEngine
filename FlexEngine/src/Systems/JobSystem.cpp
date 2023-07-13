#include "stdafx.hpp"

#include "Platform/Platform.hpp"
#include "StringBuilder.hpp"
#include "Systems/JobSystem.hpp"

#include <mutex>
#include <memory>
#include <algorithm>
#include <deque>
#include <string>
#include <thread>
#include <condition_variable>

namespace flex
{
	namespace JobSystem
	{
		struct Job
		{
			std::function<void(JobArgs)> task;
			Context* ctx;
			u32 groupID;
			u32 groupJobOffset;
			u32 groupJobEnd;
			u32 sharedMemorySize;
			u64 payload;
		};
		struct JobQueue
		{
			std::deque<Job> queue;
			std::mutex locker;

			void push_back(const Job& item)
			{
				const std::lock_guard<std::mutex> lock(locker);

				queue.push_back(item);
			}

			bool pop_front(Job& item)
			{
				const std::lock_guard<std::mutex> lock(locker);

				if (queue.empty())
				{
					return false;
				}
				item = std::move(queue.front());
				queue.pop_front();
				return true;
			}

		};

		// This structure is responsible to stop worker thread loops.
		// Once this is destroyed, worker threads will be woken up and end their loops.
		struct InternalState
		{
			~InternalState()
			{
				alive.store(false); // Indicate that new jobs cannot be started from this point
				bool wake_loop = true;
				std::thread waker([&] {
					while (wake_loop)
					{
						wakeCondition.notify_all(); // Wake up sleeping worker threads
					}
				});
				for (auto& th : threads)
				{
					th.join();
				}
				wake_loop = false;
				waker.join();
			}

			u32 numCores = 0;
			u32 numThreads = 0;
			std::unique_ptr<JobQueue[]> jobQueuePerThread;
			std::atomic_bool alive{ true };
			std::condition_variable wakeCondition;
			std::mutex wakeMutex;
			std::atomic<u32> nextQueue{ 0 };
			std::vector<std::thread> threads;

		};
		static InternalState s_InternalState;

		// Start working on a job queue
		// After the job queue is finished, it can switch to an other queue and steal jobs from there
		inline void RunJobs(u32 startingQueue)
		{
			Job job;
			for (u32 i = 0; i < s_InternalState.numThreads; ++i)
			{
				JobQueue& job_queue = s_InternalState.jobQueuePerThread[startingQueue % s_InternalState.numThreads];
				while (job_queue.pop_front(job))
				{
					JobArgs args;
					args.payload = job.payload;
					if (job.sharedMemorySize > 0)
					{
						thread_local static std::vector<u8> shared_allocation_data;
						shared_allocation_data.reserve(job.sharedMemorySize);
						args.sharedMemory = shared_allocation_data.data();
					}
					else
					{
						args.sharedMemory = nullptr;
					}

					for (u32 j = job.groupJobOffset; j < job.groupJobEnd; ++j)
					{
						args.jobIndex = j;
						// Execute job
						job.task(args);
					}

					job.ctx->counter.fetch_sub(1);
				}
				startingQueue++; // go to next queue
			}
		}

		void Initialize(u32 maxThreadCount)
		{
			if (s_InternalState.numThreads > 0)
				return;
			maxThreadCount = std::max(1u, maxThreadCount);

			// Retrieve the number of hardware threads in this system:
			s_InternalState.numCores = std::thread::hardware_concurrency();

			// Calculate the actual number of worker threads we want (-1 main thread):
			s_InternalState.numThreads = std::min(maxThreadCount, std::max(1u, s_InternalState.numCores - 1));
			s_InternalState.jobQueuePerThread = std::make_unique<JobQueue[]>(s_InternalState.numThreads);
			s_InternalState.threads.reserve(s_InternalState.numThreads);

			StringBuilder stringBuilder(24);
			for (u32 threadID = 0; threadID < s_InternalState.numThreads; ++threadID)
			{
				s_InternalState.threads.emplace_back([threadID]
				{
					while (s_InternalState.alive.load())
					{
						RunJobs(threadID);

						// Finished with jobs, put to sleep
						std::unique_lock<std::mutex> lock(s_InternalState.wakeMutex);
						s_InternalState.wakeCondition.wait(lock);
					}
				});

				std::thread& worker = s_InternalState.threads.back();

				// Place each thread on a separate core
				bool bResult = Platform::SetFlexThreadAffinityMask((void*)worker.native_handle(), threadID);
				CHECK(bResult);

				stringBuilder.Clear();
				stringBuilder.Append("jobsystem_");
				stringBuilder.Append(threadID);
				bResult = Platform::SetFlexThreadName((void*)worker.native_handle(), stringBuilder.ToCString());
				CHECK(bResult);
			}

			Print("Initialized JobSystem with %u threads (%u cores)", s_InternalState.numThreads, s_InternalState.numCores);
		}

		void Destroy()
		{
			s_InternalState.alive = false;
			// TODO: Join all threads?
		}

		u32 GetThreadCount()
		{
			return s_InternalState.numThreads;
		}

		void Execute(Context& ctx, const std::function<void(JobArgs)>& task, u64 payload)
		{
			ctx.counter.fetch_add(1);

			Job job;
			job.ctx = &ctx;
			job.task = task;
			job.groupID = 0;
			job.groupJobOffset = 0;
			job.groupJobEnd = 1;
			job.sharedMemorySize = 0;
			job.payload = payload;

			s_InternalState.jobQueuePerThread[s_InternalState.nextQueue.fetch_add(1) % s_InternalState.numThreads].push_back(job);
			s_InternalState.wakeCondition.notify_one();
		}

		bool IsBusy(const Context& ctx)
		{
			// Whenever the context label is greater than zero, it means that there is still work that needs to be done
			return ctx.counter.load() > 0;
		}

		void Wait(const Context& ctx)
		{
			if (IsBusy(ctx))
			{
				// Wake any threads that might be sleeping:
				s_InternalState.wakeCondition.notify_all();

				// work() will pick up any jobs that are on stand by and execute them on this thread:
				RunJobs(s_InternalState.nextQueue.fetch_add(1) % s_InternalState.numThreads);

				while (IsBusy(ctx))
				{
					// If we are here, then there are still remaining jobs that work() couldn't pick up.
					// In this case those jobs are not standing by on a queue but currently executing
					// on other threads, so they cannot be picked up by this thread.
					// Allow to swap out this thread by OS to not spin endlessly for nothing
					std::this_thread::yield();
				}
			}
		}
	} // namespace JobSystem
} // namespace flex
