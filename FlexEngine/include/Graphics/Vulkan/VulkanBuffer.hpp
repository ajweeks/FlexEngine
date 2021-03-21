#pragma once
#if COMPILE_VULKAN

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

			VulkanBuffer(const VulkanBuffer& other)
			{
				if (this != &other)
				{
					m_Device = other.m_Device;
					m_Buffer = other.m_Buffer;
					m_Memory = other.m_Memory;
					m_Size = other.m_Size;
					m_Alignment = other.m_Alignment;
					m_Mapped = other.m_Mapped;
					allocations = other.allocations;
					m_UsageFlags = other.m_UsageFlags;
					m_MemoryPropertyFlags = other.m_MemoryPropertyFlags;
				}
			}

			VulkanBuffer(const VulkanBuffer&& other) = delete;
			VulkanBuffer& operator=(const VulkanBuffer& other) = delete;
			VulkanBuffer& operator=(const VulkanBuffer&& other) = delete;

			VkResult Create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
			void Destroy();

			VkResult Bind();
			VkResult Map(VkDeviceSize size = VK_WHOLE_SIZE);
			VkResult Map(VkDeviceSize offset, VkDeviceSize size);
			void Unmap();

			void Reset();

			// Reserves size bytes in buffer and returns offset to that range, returns (VkDeviceSize)-1 if bCanResize is false and allocation won't fit, or if resize failed
			// TODO: Add tests
			FLEX_NO_DISCARD VkDeviceSize Alloc(VkDeviceSize size, bool bCanResize);
			// TODO: Add tests
			FLEX_NO_DISCARD VkDeviceSize Realloc(VkDeviceSize offset, VkDeviceSize size, bool bCanResize);
			// TODO: Add tests
			void Free(VkDeviceSize offset);
			// TODO: Add tests
			void Shrink(real minUnused = 0.0f);
			// TODO: Add defragment helper

			// Returns size of allocation, or (VkDeviceSize)-1 if not found
			FLEX_NO_DISCARD VkDeviceSize GetAllocationSize(VkDeviceSize offset) const;

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