
#pragma once

#if FLEX_OVERRIDE_NEW_DELETE

void* operator new(std::size_t size);
void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, std::size_t size) noexcept;

#endif // FLEX_OVERRIDE_NEW_DELETE

namespace flex
{
	namespace mem
	{

#if FLEX_OVERRIDE_MALLOC
		// TODO: Look into using LD_PRELOAD to hook in custom malloc/free/etc.

		// TODO: Re-enable:
		//#define STBI_MALLOC(size)		malloc_hooked(size)
		//#define STBI_REALLOC(p, newsz)	realloc_hooked(p, newsz)
		//#define STBI_FREE(ptr)			free_hooked(ptr)

		void* malloc_hooked(std::size_t size);
		void* aligned_malloc_hooked(std::size_t size, std::size_t alignment);
		void free_hooked(void* ptr);
		void free_hooked(void* ptr, std::size_t size);
		void aligned_free_hooked(void* ptr);
		void* realloc_hooked(void* ptr, std::size_t newsz);
#endif // FLEX_OVERRIDE_MALLOC

	} // namespace mem

	void* flex_aligned_malloc(std::size_t size, std::size_t alignment);
	void flex_aligned_free(void* ptr);
} // namespace flex