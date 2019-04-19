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
	totalAllocated += size;
	++allocationCount;
	if (allocationCount % 1000 == 0)
	{
	//	std::cout << "allocations: " << allocationCount << ", " << (totalAllocated / 1024 / 1024) << " MB\n";
	}
	void* ptr = malloc(size);
	return ptr;
}

void* aligned_malloc_hooked(size_t size, size_t alignment)
{
	totalAllocated += size;
	++allocationCount;
	if (allocationCount % 1000 == 0)
	{
	//	std::cout << "allocations: " << allocationCount << ", " << (totalAllocated / 1024 / 1024) << " MB\n";
	}
	void* ptr = _aligned_malloc(size, alignment);
	return ptr;
}

void free_hooked(void* ptr)
{
	++deallocationCount;
	if (deallocationCount % 1000 == 0)
	{
	//	std::cout << "deallocations: " << deallocationCount << ", " << (totalAllocated / 1024 / 1024) << " MB\n";
	}
	free(ptr);
}

void aligned_free_hooked(void* ptr)
{
	++deallocationCount;
	if (deallocationCount % 1000 == 0)
	{
	//	std::cout << "deallocations: " << deallocationCount << ", " << (totalAllocated / 1024 / 1024) << " MB\n";
	}
	_aligned_free(ptr);
}
