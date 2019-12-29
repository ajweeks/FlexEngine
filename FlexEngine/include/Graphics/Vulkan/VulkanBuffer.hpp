#pragma once
#if COMPILE_VULKAN

IGNORE_WARNINGS_PUSH
#include <vulkan/vulkan.h>
IGNORE_WARNINGS_POP

#include "VDeleter.hpp"

namespace flex
{
	namespace vk
	{
		struct VulkanDevice;

		struct VulkanBuffer
		{
			VulkanBuffer(VulkanDevice* device);

			VkResult Create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
			void Destroy();

			VkResult Bind();
			VkResult Map(VkDeviceSize size = VK_WHOLE_SIZE);
			void Unmap();

			VDeleter<VkBuffer> m_Buffer;
			VDeleter<VkDeviceMemory> m_Memory;
			VulkanDevice* m_Device = nullptr;
			VkDeviceSize m_Size = 0;
			VkDeviceSize m_Alignment = 0;
			void* m_Mapped = nullptr;

			VkBufferUsageFlags m_UsageFlags = 0;
			VkMemoryPropertyFlags m_MemoryPropertyFlags = 0;
		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN