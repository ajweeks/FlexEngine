
#pragma once

void* operator new(std::size_t size);
void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, std::size_t size) noexcept;

void* malloc_hooked(std::size_t size);
void* aligned_malloc_hooked(std::size_t size, std::size_t alignment);
void free_hooked(void* ptr);
void free_hooked(void* ptr, std::size_t size);
void aligned_free_hooked(void* ptr);
void* realloc_hooked(void* ptr, std::size_t newsz);

void* flex_aligned_malloc(std::size_t size, std::size_t alignment);
void flex_aligned_free(void* ptr);
