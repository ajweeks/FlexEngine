#pragma once

#include <list>

namespace flex
{
	template<typename T, u32 PoolSize>
	class PoolAllocator
	{
	public:
		PoolAllocator()
		{
		}

		~PoolAllocator()
		{
			ReleaseAll();
		}

		T* Alloc()
		{
			if (data.size() == 0 || data.back().second == PoolSize)
			{
				Resize();
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

		PoolAllocator(const PoolAllocator&&) = delete;
		PoolAllocator(const PoolAllocator&) = delete;
		PoolAllocator& operator=(const PoolAllocator&&) = delete;
		PoolAllocator& operator=(const PoolAllocator&) = delete;

	private:
		void Resize()
		{
			data.push_back({ std::array<T, PoolSize>(), 0u });
		}

		std::list<Pair<std::array<T, PoolSize>, u32>> data;
	};
} // namespace flex
