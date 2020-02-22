#include "memory.hpp"

#include <iostream>

namespace flex
{
	ImVec4 g_WarningTextColor(1.0f, 0.25f, 0.25f, 1.0f);
	ImVec4 g_WarningButtonColor(0.65f, 0.12f, 0.09f, 1.0f);
	ImVec4 g_WarningButtonHoveredColor(0.45f, 0.04f, 0.01f, 1.0f);
	ImVec4 g_WarningButtonActiveColor(0.35f, 0.0f, 0.0f, 1.0f);
}

void* operator new(size_t size)
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


void* malloc_hooked(size_t size)
{
	if (size == 0)
	{
		std::cerr << "Attempted to alloc with a size of 0!\n";
		return nullptr;
	}
	size += sizeof(size_t);
	flex::g_TotalTrackedAllocatedMemory += size;
	++flex::g_TrackedAllocationCount;
	void* ptr = malloc(size);
	*((size_t*)ptr) = size;
	ptr = ((size_t*)ptr) + 1;
	return ptr;
}

void* aligned_malloc_hooked(size_t size, size_t alignment)
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
	ptr = ((size_t*)ptr) - 1;
	size_t size = *((size_t*)ptr);
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
	ptr = ((size_t*)ptr) - 1;
	size_t stored_size = *((size_t*)ptr);
	assert(stored_size == size); // Only full deallocations are permitted
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

void* realloc_hooked(void* ptr, size_t newsz)
{
	if (!ptr)
	{
		return malloc_hooked(newsz);
	}
	if (newsz == 0)
	{
		std::cerr << "Attempted to realloc to a size of 0!\n";
		free_hooked(ptr);
		return nullptr;
	}
	ptr = ((size_t*)ptr) - 1;
	size_t size = *((size_t*)ptr);
	flex::g_TotalTrackedAllocatedMemory -= size;

	newsz += sizeof(size_t);
	flex::g_TotalTrackedAllocatedMemory += newsz;
	ptr = realloc(ptr, newsz);
	*((size_t*)ptr) = newsz;
	ptr = ((size_t*)ptr) + 1;
	return ptr;
}

void* flex_aligned_malloc(std::size_t size, std::size_t alignment)
{
#ifdef _WIN32
	void* ptr = _aligned_malloc(size, alignment);
	return ptr;
#else
	void* ptr;
	posix_memalign(&ptr, alignment, size);
	return ptr;
#endif
}

void flex_aligned_free(void* ptr)
{
#ifdef _WIN32
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}
