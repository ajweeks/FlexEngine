#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanBuffer.hpp"

#include "Graphics/Vulkan/VulkanInitializers.hpp"

namespace flex
{
	namespace vk
	{
		VulkanBuffer::VulkanBuffer(const VDeleter<VkDevice>& device) :
			m_Device(device),
			m_Buffer(VDeleter<VkBuffer>(device, vkDestroyBuffer)),
			m_Memory(VDeleter<VkDeviceMemory>(device, vkFreeMemory))
		{
		}

		VkResult VulkanBuffer::Map(VkDeviceSize size)
		{
			return vkMapMemory(m_Device, m_Memory, 0, size, 0, &m_Mapped);
		}

		void VulkanBuffer::Unmap()
		{
			if (m_Mapped)
			{
				vkUnmapMemory(m_Device, m_Memory);
				m_Mapped = nullptr;
			}
		}

		void VulkanBuffer::Destroy()
		{
			m_Buffer.replace();
			m_Memory.replace();
		}

		VkResult VulkanBuffer::Bind()
		{
			return vkBindBufferMemory(m_Device, m_Buffer, m_Memory, 0);
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN