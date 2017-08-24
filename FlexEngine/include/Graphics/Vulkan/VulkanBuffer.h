#pragma once

#include <vulkan/vulkan.h>

#include "VDeleter.h"

namespace flex
{
	struct VulkanBuffer
	{
		VulkanBuffer(const VDeleter<VkDevice>& device);

		VkResult Map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
		void Unmap();

		VkResult Bind(VkDeviceSize offset = 0);
		void SetupDescriptor(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
		void CopyTo(void* data, VkDeviceSize size);
		VkResult Flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

		VDeleter<VkBuffer> m_Buffer;
		VDeleter<VkDeviceMemory> m_Memory;
		VkDescriptorBufferInfo m_DescriptorInfo;
		VkDevice m_Device;
		VkDeviceSize m_Size = 0;
		VkDeviceSize m_Alignment = 0;
		void* m_Mapped = nullptr;

		VkBufferUsageFlags m_UsageFlags;
		VkMemoryPropertyFlags m_MemoryPropertyFlags;
	};
} // namespace flex