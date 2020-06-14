#pragma once

namespace flex
{
	// TODO: Test
	template<typename T, int PoolSize>
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
			if (data.size() == 0 || data.back().second == PoolSize - 1)
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

	private:
		void Resize()
		{
			data.push_back({ std::array<T, PoolSize>(), 0u });
		}

		u32 m_PoolSize;
		std::list<Pair<std::array<T, PoolSize>, u32>> data;
	};
} // namespace flex
