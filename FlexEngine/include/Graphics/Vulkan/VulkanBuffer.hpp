#pragma once
#if COMPILE_VULKAN

IGNORE_WARNINGS_PUSH
#include "volk/volk.h"
IGNORE_WARNINGS_POP

#include "VDeleter.hpp"

namespace flex
{
	namespace vk
	{
		struct VulkanDevice;

		struct Allocation
		{
			VkDeviceSize offset;
			VkDeviceSize size;
		};

		struct VulkanBuffer
		{
			VulkanBuffer(VulkanDevice* device);

			VkResult Create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
			void Destroy();

			VkResult Bind();
			VkResult Map(VkDeviceSize size = VK_WHOLE_SIZE);
			VkResult Map(VkDeviceSize offset, VkDeviceSize size);
			void Unmap();

			// Reserves size bytes in buffer and returns offset to that range, returns (VkDeviceSize)-1 if bCanResize is false and allocation won't fit, or if resize failed
			// TODO: Add tests
			VkDeviceSize Alloc(VkDeviceSize size, bool bCanResize);
			// TODO: Add tests
			VkDeviceSize Realloc(VkDeviceSize offset, VkDeviceSize size, bool bCanResize);

			VulkanDevice* m_Device = nullptr;
			VDeleter<VkBuffer> m_Buffer;
			VDeleter<VkDeviceMemory> m_Memory;
			VkDeviceSize m_Size = 0;
			VkDeviceSize m_Alignment = 0;
			void* m_Mapped = nullptr;
			std::vector<Allocation> allocations;

			VkBufferUsageFlags m_UsageFlags = 0;
			VkMemoryPropertyFlags m_MemoryPropertyFlags = 0;

		private:
			void UpdateAllocationSize(VkDeviceSize offset, VkDeviceSize newSize);

		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN