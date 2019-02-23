#pragma once
#if COMPILE_VULKAN

IGNORE_WARNINGS_PUSH
#include <vulkan/vulkan.hpp>
IGNORE_WARNINGS_POP

#include "VDeleter.hpp"

namespace flex
{
	namespace vk
	{
		struct VulkanBuffer
		{
			VulkanBuffer(const VDeleter<VkDevice>& device);

			VkResult Map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
			void Unmap();
			void Destroy();

			VkResult Bind(VkDeviceSize offset = 0);
			void SetupDescriptor(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
			void CopyTo(void* data, VkDeviceSize size);
			VkResult Flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

			VDeleter<VkBuffer> m_Buffer;
			VDeleter<VkDeviceMemory> m_Memory;
			VkDescriptorBufferInfo m_DescriptorInfo;
			VkDevice m_Device = VK_NULL_HANDLE;
			VkDeviceSize m_Size = 0;
			VkDeviceSize m_Alignment = 0;
			void* m_Mapped = nullptr;

			VkBufferUsageFlags m_UsageFlags = 0;
			VkMemoryPropertyFlags m_MemoryPropertyFlags = 0;
		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN