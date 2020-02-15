
#pragma once

void* operator new(std::size_t size);
void operator delete(void* ptr) noexcept;

void* malloc_hooked(std::size_t size);
void* aligned_malloc_hooked(std::size_t size, std::size_t alignment);
void free_hooked(void* ptr);
void aligned_free_hooked(void* ptr);
void* realloc_hooked(void* ptr, std::size_t newsz);
