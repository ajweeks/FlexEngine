#include "stdafx.hpp"

#include "memory.hpp"

#include <iostream>

#if FLEX_OVERRIDE_NEW_DELETE

void* operator new(std::size_t size)
{
	return malloc_hooked(size);
}

void operator delete(void* ptr) noexcept
{
	free_hooked(ptr);
}

void operator delete(void* ptr, std::size_t size) noexcept
{
	free_hooked(ptr, size);
}

#endif // FLEX_OVERRIDE_NEW_DELETE

namespace flex
{
	namespace mem
	{
#if FLEX_OVERRIDE_MALLOC
		void* malloc_hooked(size_t size)
		{
			if (size == 0)
			{
				std::cerr << "Attempted to alloc with a size of 0!\n";
				return nullptr;
			}
			size += sizeof(std::size_t);
			flex::g_TotalTrackedAllocatedMemory += size;
			++flex::g_TrackedAllocationCount;
			void* ptr = malloc(size);
			*((std::size_t*)ptr) = size;
			ptr = ((std::size_t*)ptr) + 1;
			return ptr;
		}

		void* aligned_malloc_hooked(std::size_t size, std::size_t alignment)
		{
			if (size == 0)
			{
				std::cerr << "Attempted to aligned alloc with a size of 0!\n";
				return nullptr;
			}
			flex::g_TotalTrackedAllocatedMemory += size;
			++flex::g_TrackedAllocationCount;
			void* ptr = flex_aligned_malloc(size, alignment);
			return ptr;
		}

		void free_hooked(void* ptr)
		{
			if (!ptr)
			{
				return;
			}
			ptr = ((std::size_t*)ptr) - 1;
			std::size_t size = *((std::size_t*)ptr);
			flex::g_TotalTrackedAllocatedMemory -= size;
			++flex::g_TrackedDeallocationCount;
			free(ptr);
		}

		void free_hooked(void* ptr, std::size_t size)
		{
			if (!ptr)
			{
				return;
			}
			ptr = ((std::size_t*)ptr) - 1;
			std::size_t stored_size = *((std::size_t*)ptr);
			assert(stored_size == (size + sizeof(std::size_t))); // Only full deallocations are permitted
			flex::g_TotalTrackedAllocatedMemory -= size;
			++flex::g_TrackedDeallocationCount;
			free(ptr);
		}

		void aligned_free_hooked(void* ptr)
		{
			if (!ptr)
			{
				return;
			}
			++flex::g_TrackedDeallocationCount;
			flex_aligned_free(ptr);
		}

		void* realloc_hooked(void* ptr, std::size_t newsz)
		{
			if (!ptr)
			{
				return malloc(newsz);
			}
			if (newsz == 0)
			{
				std::cerr << "Attempted to realloc to a size of 0!\n";
				free(ptr);
				return nullptr;
			}
			ptr = ((std::size_t*)ptr) - 1;
			std::size_t size = *((std::size_t*)ptr);
			flex::g_TotalTrackedAllocatedMemory -= size;

			newsz += sizeof(std::size_t);
			flex::g_TotalTrackedAllocatedMemory += newsz;
			ptr = realloc(ptr, newsz);
			*((std::size_t*)ptr) = newsz;
			ptr = ((std::size_t*)ptr) + 1;
			return ptr;
		}
#endif // FLEX_OVERRIDE_MALLOC
	} // namespace mem

	void* flex_aligned_malloc(std::size_t size, std::size_t alignment)
	{
#ifdef _WINDOWS
#ifdef DEBUG
		void* ptr = _aligned_malloc_dbg(size, alignment, __FILE__, __LINE__);
#else
		void* ptr = _aligned_malloc(size, alignment);
#endif
		return ptr;
#else
		void* ptr;
		if (posix_memalign(&ptr, alignment, size) != 0)
		{
			PrintError("Failed to allocate aligned memory for %lu bytes\n", (u64)size);
			return nullptr;
		}
		return ptr;
#endif
	}

	void flex_aligned_free(void* ptr)
	{
#ifdef _WINDOWS
#ifdef DEBUG
		_aligned_free_dbg(ptr);
#else
		_aligned_free(ptr);
#endif
#else
		free(ptr);
#endif
	}
} // namespace flex
