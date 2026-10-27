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
			// Jobs queued but not yet started, so sleeping workers know when to wake
			std::atomic<u32> pendingJobCount{ 0 };
			// See ExecuteBackground
			JobQueue backgroundQueue;
			std::atomic<u32> pendingBackgroundJobCount{ 0 };
			std::atomic<u32> runningBackgroundJobCount{ 0 };
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
					s_InternalState.pendingJobCount.fetch_sub(1);
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

		static u32 MaxRunningBackgroundJobs()
		{
			return std::max(1u, s_InternalState.numThreads / 2);
		}

		// Runs one background job if there's one to run (& not too many are running already)
		static bool RunBackgroundJob()
		{
			if (s_InternalState.pendingBackgroundJobCount.load() == 0)
			{
				return false;
			}
			if (s_InternalState.runningBackgroundJobCount.fetch_add(1) >= MaxRunningBackgroundJobs())
			{
				s_InternalState.runningBackgroundJobCount.fetch_sub(1);
				return false;
			}

			bool bRan = false;
			Job job;
			if (s_InternalState.backgroundQueue.pop_front(job))
			{
				s_InternalState.pendingBackgroundJobCount.fetch_sub(1);
				JobArgs args = {};
				job.task(args);
				job.ctx->counter.fetch_sub(1);
				bRan = true;
			}
			s_InternalState.runningBackgroundJobCount.fetch_sub(1);
			return bRan;
		}

		static void WakeWorkers()
		{
			// Taking the mutex ensures workers aren't between checking for work & going to sleep (which would miss this)
			{
				std::lock_guard<std::mutex> lock(s_InternalState.wakeMutex);
			}
			s_InternalState.wakeCondition.notify_all();
		}

		void Initialize(u32 maxThreadCount)
		{
			if (s_InternalState.numThreads > 0)
				return;

			s_InternalState.alive.store(true);
			s_InternalState.nextQueue.store(0);
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

						if (RunBackgroundJob())
						{
							continue;
						}

						// Finished with jobs, sleep until more are queued (background jobs which can't start yet because
						// enough are running are picked up when one of those finishes, since its thread loops around)
						std::unique_lock<std::mutex> lock(s_InternalState.wakeMutex);
						s_InternalState.wakeCondition.wait(lock, []
						{
							return s_InternalState.pendingJobCount.load() > 0 || !s_InternalState.alive.load() ||
								(s_InternalState.pendingBackgroundJobCount.load() > 0 && s_InternalState.runningBackgroundJobCount.load() < MaxRunningBackgroundJobs());
						});
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
			WakeWorkers();

			for (std::thread& thread : s_InternalState.threads)
			{
				if (thread.joinable())
				{
					thread.join();
				}
			}

			s_InternalState.threads.clear();
			s_InternalState.jobQueuePerThread.reset();
			s_InternalState.numThreads = 0;
			s_InternalState.numCores = 0;
			s_InternalState.nextQueue.store(0);

			// Finish background work which hadn't started (e.g. memory waiting to be freed) on this thread
			FlushBackgroundFrees();
			Job job;
			while (s_InternalState.backgroundQueue.pop_front(job))
			{
				s_InternalState.pendingBackgroundJobCount.fetch_sub(1);
				JobArgs args = {};
				job.task(args);
				job.ctx->counter.fetch_sub(1);
			}
		}

		u32 GetThreadCount()
		{
			return s_InternalState.numThreads;
		}

		void Dispatch(Context& ctx, u32 jobCount, u32 groupSize, const std::function<void(JobArgs)>& task)
		{
			if (jobCount == 0 || groupSize == 0)
			{
				return;
			}

			const u32 groupCount = (jobCount + groupSize - 1) / groupSize;
			ctx.counter.fetch_add(groupCount);
			s_InternalState.pendingJobCount.fetch_add(groupCount);

			Job job;
			job.ctx = &ctx;
			job.task = task;
			job.sharedMemorySize = 0;
			job.payload = 0;
			for (u32 groupID = 0; groupID < groupCount; ++groupID)
			{
				job.groupID = groupID;
				job.groupJobOffset = groupID * groupSize;
				job.groupJobEnd = std::min(job.groupJobOffset + groupSize, jobCount);
				s_InternalState.jobQueuePerThread[s_InternalState.nextQueue.fetch_add(1) % s_InternalState.numThreads].push_back(job);
			}

			WakeWorkers();
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

			s_InternalState.pendingJobCount.fetch_add(1);
			s_InternalState.jobQueuePerThread[s_InternalState.nextQueue.fetch_add(1) % s_InternalState.numThreads].push_back(job);
			WakeWorkers();
		}

		void ExecuteBackground(Context& ctx, const std::function<void()>& task)
		{
			if (s_InternalState.numThreads == 0)
			{
				task();
				return;
			}

			ctx.counter.fetch_add(1);

			Job job;
			job.ctx = &ctx;
			job.task = [task](JobArgs) { task(); };
			job.groupID = 0;
			job.groupJobOffset = 0;
			job.groupJobEnd = 1;
			job.sharedMemorySize = 0;
			job.payload = 0;

			s_InternalState.pendingBackgroundJobCount.fetch_add(1);
			s_InternalState.backgroundQueue.push_back(job);
			WakeWorkers();
		}

		struct Garbage
		{
			void* ptr;
			void (*deleter)(void*);
		};
		static std::mutex s_PendingGarbageMutex;
		static std::vector<Garbage> s_PendingGarbage;

		void DestroyInBackground(void* ptr, void (*deleter)(void*))
		{
			if (ptr == nullptr)
			{
				return;
			}
			if (s_InternalState.numThreads == 0)
			{
				deleter(ptr);
				return;
			}
			std::lock_guard<std::mutex> lock(s_PendingGarbageMutex);
			s_PendingGarbage.push_back({ ptr, deleter });
		}

		void FreeInBackground(void* ptr)
		{
			DestroyInBackground(ptr, [](void* p) { free(p); });
		}

		void FlushBackgroundFrees()
		{
			std::vector<Garbage>* garbage = nullptr;
			{
				std::lock_guard<std::mutex> lock(s_PendingGarbageMutex);
				if (s_PendingGarbage.empty())
				{
					return;
				}
				garbage = new std::vector<Garbage>(std::move(s_PendingGarbage));
				s_PendingGarbage.clear();
			}
			// One job per frame (queuing a job wakes the workers, which adds up when many things are freed)
			static Context s_GarbageContext;
			ExecuteBackground(s_GarbageContext, [garbage]()
			{
				for (const Garbage& item : *garbage)
				{
					item.deleter(item.ptr);
				}
				delete garbage;
			});
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
				WakeWorkers();

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
