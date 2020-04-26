#pragma once

namespace flex
{
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
			if (data.size() == 0 || data[data.size() - 1].second == PoolSize - 1)
			{
				Resize();
			}

			auto& arrPair = data[data.size() - 1];
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
		std::vector<Pair<std::array<T, PoolSize>, u32>> data;
	};
} // namespace flex
