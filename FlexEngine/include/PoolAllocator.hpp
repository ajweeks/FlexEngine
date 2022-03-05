#pragma once

#include <list>

#include "Pair.hpp"

namespace flex
{
	template<typename T, u32 PoolSize>
	class PoolAllocator
	{
	public:
		PoolAllocator() = default;

		PoolAllocator(const PoolAllocator&&) = delete;
		PoolAllocator(const PoolAllocator&) = delete;
		PoolAllocator& operator=(const PoolAllocator&&) = delete;
		PoolAllocator& operator=(const PoolAllocator&) = delete;

		~PoolAllocator()
		{
			ReleaseAll();
		}

		T* Alloc()
		{
			if (data.size() == 0 || data.back().second == PoolSize)
			{
				PushPool();
			}

			auto& arrPair = data.back();
			void* result = (T*)arrPair.first + arrPair.second++;
			T* r = new(result) T();
			return r;
		}

		void ReleaseAll()
		{
			for (auto& pair : data)
			{
				((T*)pair.first)->~T();
				free(pair.first);
			}
			data.clear();
		}

		u32 GetPoolCount() const
		{
			return (u32)data.size();
		}

		constexpr u32 GetPoolSize() const
		{
			return PoolSize;
		}

		u32 MemoryUsed() const
		{
			return (u32)(sizeof(T) * GetPoolCount() * PoolSize);
		}

	private:
		void PushPool()
		{
			void* newAlloc = malloc(sizeof(T) * PoolSize);
			memset(newAlloc, 0, sizeof(T) * PoolSize);
			CHECK_NE(newAlloc, nullptr);
			data.push_back({ newAlloc, 0u });
		}

		// List of pairs of arrays (pools) & usage counts
		std::list<Pair<void*, u32>> data;
	};
} // namespace flex
