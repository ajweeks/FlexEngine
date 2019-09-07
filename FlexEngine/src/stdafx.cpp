#include "stdafx.hpp"

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

static size_t totalAllocated = 0;
static size_t allocationCount = 0;
static size_t deallocationCount = 0;

void* malloc_hooked(size_t size)
{
	if (size == 0)
	{
		std::cerr << "Attempted to alloc with a size of 0!\n";
		return nullptr;
	}
	size += sizeof(size_t);
	totalAllocated += size;
	++allocationCount;
	if (allocationCount % 10000 == 0)
	{
		//std::cout << "Ka: " << allocationCount / 1000 << ", Kd: " << deallocationCount / 1000 << ", delta: " << (allocationCount - deallocationCount) << ", " << (totalAllocated / 1024) << " KB\n";
	}
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
	totalAllocated += size;
	++allocationCount;
	if (allocationCount % 10000 == 0)
	{
		//std::cout << "Ka: " << allocationCount / 1000 << ", Kd: " << deallocationCount / 1000 << ", delta: " << (allocationCount - deallocationCount) << ", " << (totalAllocated / 1024) << " KB\n";
	}
	void* ptr = _aligned_malloc(size, alignment);
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
	totalAllocated -= size;
	++deallocationCount;
	if (deallocationCount % 10000 == 0)
	{
		//std::cout << "Ka: " << allocationCount / 1000 << ", Kd: " << deallocationCount / 1000 << ", delta: " << (allocationCount - deallocationCount) << ", " << (totalAllocated / 1024) << " KB\n";
	}
	free(ptr);
}

void aligned_free_hooked(void* ptr)
{
	if (!ptr)
	{
		return;
	}
	++deallocationCount;
	if (deallocationCount % 10000 == 0)
	{
		//std::cout << "Ka: " << allocationCount / 1000 << ", Kd: " << deallocationCount / 1000 << ", delta: " << (allocationCount - deallocationCount) << ", " << (totalAllocated / 1024) << " KB\n";
	}
	_aligned_free(ptr);
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
	totalAllocated -= size;

	newsz += sizeof(size_t);
	totalAllocated += newsz;
	ptr = realloc(ptr, newsz);
	*((size_t*)ptr) = newsz;
	ptr = ((size_t*)ptr) + 1;
	return ptr;
}
