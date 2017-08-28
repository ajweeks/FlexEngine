#include "stdafx.h"

#include "Graphics/Vulkan/VulkanBuffer.h"

namespace flex
{
	namespace vk
	{
		Buffer::Buffer(const VDeleter<VkDevice>& device) :
			m_Device(device),
			m_Buffer(VDeleter<VkBuffer>(device, vkDestroyBuffer)),
			m_Memory(VDeleter<VkDeviceMemory>(device, vkFreeMemory))
		{
		}

		VkResult Buffer::Map(VkDeviceSize size, VkDeviceSize offset)
		{
			return vkMapMemory(m_Device, m_Memory, offset, size, 0, &m_Mapped);
		}

		void Buffer::Unmap()
		{
			if (m_Mapped)
			{
				vkUnmapMemory(m_Device, m_Memory);
				m_Mapped = nullptr;
			}
		}

		void Buffer::Destroy()
		{
			m_Buffer.replace();
			m_Memory.replace();
		}

		VkResult Buffer::Bind(VkDeviceSize offset)
		{
			return vkBindBufferMemory(m_Device, m_Buffer, m_Memory, offset);
		}

		void Buffer::SetupDescriptor(VkDeviceSize size, VkDeviceSize offset)
		{
			m_DescriptorInfo.offset = offset;
			m_DescriptorInfo.buffer = m_Buffer;
			m_DescriptorInfo.range = size;
		}

		void Buffer::CopyTo(void* data, VkDeviceSize size)
		{
			assert(m_Mapped);
			memcpy(m_Mapped, data, (size_t)size);
		}

		VkResult Buffer::Flush(VkDeviceSize size, VkDeviceSize offset)
		{
			VkMappedMemoryRange mappedRange = {};
			mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
			mappedRange.memory = m_Memory;
			mappedRange.offset = offset;
			mappedRange.size = size;
			return vkFlushMappedMemoryRanges(m_Device, 1, &mappedRange);
		}
	} // namespace vk
} // namespace flex
