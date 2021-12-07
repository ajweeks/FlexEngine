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
			return &arrPair.first[arrPair.second++];
		}

		void ReleaseAll()
		{
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
			data.push_back({ std::array<T, PoolSize>(), 0u });
		}

		// List of pairs of arrays (pools) & usage counts
		std::list<Pair<std::array<T, PoolSize>, u32>> data;
	};
} // namespace flex
