#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanBuffer.hpp"

#include "Graphics/Vulkan/VulkanDevice.hpp"
#include "Graphics/Vulkan/VulkanInitializers.hpp"
#include "Graphics/Vulkan/VulkanRenderer.hpp"

namespace flex
{
	namespace vk
	{
		VulkanBuffer::VulkanBuffer(VulkanDevice* device) :
			m_Device(device),
			m_Buffer(VDeleter<VkBuffer>(device->m_LogicalDevice, vkDestroyBuffer)),
			m_Memory(VDeleter<VkDeviceMemory>(device->m_LogicalDevice, deviceFreeMemory))
		{
		}

		VkResult VulkanBuffer::Create(VkDeviceSize size,
			VkBufferUsageFlags usage,
			VkMemoryPropertyFlags properties,
			const char* DEBUG_name /* = nullptr */)
		{
			CHECK_NE(size, 0);

			VkBufferCreateInfo bufferInfo = vks::bufferCreateInfo(usage, size);
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VK_CHECK_RESULT(vkCreateBuffer(m_Device->m_LogicalDevice, &bufferInfo, nullptr, m_Buffer.replace()));

			VkMemoryRequirements memRequirements;
			vkGetBufferMemoryRequirements(m_Device->m_LogicalDevice, m_Buffer, &memRequirements);

			if (memRequirements.size > size)
			{
				// Create the buffer again with the full size needed so we can potentially use it without reallocating
				// Other option is to set m_Size to requested size, since we know we have at least that much
				bufferInfo.size = memRequirements.size;
				VK_CHECK_RESULT(vkCreateBuffer(m_Device->m_LogicalDevice, &bufferInfo, nullptr, m_Buffer.replace()));
			}

			VkMemoryAllocateInfo allocInfo = vks::memoryAllocateInfo(memRequirements.size);
			allocInfo.memoryTypeIndex = FindMemoryType(m_Device, memRequirements.memoryTypeBits, properties);

			std::string debugName = DEBUG_name != nullptr ? DEBUG_name : "";
			VK_CHECK_RESULT(m_Device->AllocateMemory(debugName, &allocInfo, nullptr, m_Memory.replace()));

			// Create the memory backing up the buffer handle
			m_Alignment = memRequirements.alignment;
			m_Size = allocInfo.allocationSize;
			m_UsageFlags = usage;
			m_MemoryPropertyFlags = properties;

			if (DEBUG_name != nullptr)
			{
				m_DEBUG_Name = std::string(DEBUG_name);
				((VulkanRenderer*)g_Renderer)->SetBufferName(m_Device, m_Buffer, m_DEBUG_Name.c_str());
			}

			return Bind();
		}

		void VulkanBuffer::Destroy()
		{
			m_Buffer.replace();
			m_Memory.replace();
			allocations.clear();
			m_Size = 0;
			m_Alignment = 0;
			m_Mapped = nullptr;
		}

		VkResult VulkanBuffer::Bind()
		{
			return vkBindBufferMemory(m_Device->m_LogicalDevice, m_Buffer, m_Memory, 0);
		}

		VkResult VulkanBuffer::Map(VkDeviceSize size)
		{
			return vkMapMemory(m_Device->m_LogicalDevice, m_Memory, 0, size, 0, &m_Mapped);
		}

		VkResult VulkanBuffer::Map(VkDeviceSize offset, VkDeviceSize size)
		{
			return vkMapMemory(m_Device->m_LogicalDevice, m_Memory, offset, size, 0, &m_Mapped);
		}

		void VulkanBuffer::Unmap()
		{
			if (m_Mapped)
			{
				vkUnmapMemory(m_Device->m_LogicalDevice, m_Memory);
				m_Mapped = nullptr;
			}
		}

		void VulkanBuffer::Reset()
		{
			allocations.clear();
		}

		VkDeviceSize VulkanBuffer::Alloc(VkDeviceSize size)
		{
			Unmap();

			const VkDeviceSize errorCode = (VkDeviceSize)-1;

			// Allocations aren't stored in order (Free & Realloc shuffle them), so sort before searching for a gap
			std::sort(allocations.begin(), allocations.end(), [](const Allocation& a, const Allocation& b) { return a.offset < b.offset; });

			// First fit
			VkDeviceSize offset = 0;
			for (const Allocation& allocation : allocations)
			{
				if (allocation.offset >= offset + size)
				{
					break;
				}
				offset = glm::max(offset, allocation.offset + allocation.size);
			}

			if (offset + size > m_Size)
			{
				// Grow by more than required so consecutive allocations don't each resize (which stalls the GPU)
				VkDeviceSize newSize = glm::max(offset + size, m_Size + m_Size / 2);
				VkResult result = Resize(newSize);
				if (result != VK_SUCCESS)
				{
					VK_CHECK_RESULT(result);
					return errorCode;
				}
			}

			allocations.push_back({ offset, size });
			return offset;
		}

		VkDeviceSize VulkanBuffer::Realloc(VkDeviceSize offset, VkDeviceSize size)
		{
			Unmap();

			bool bCanResizeInPlace = (offset + size) < m_Size;
			if (bCanResizeInPlace)
			{
				// Check for collisions with other allocations
				for (u32 i = 0; i < (u32)allocations.size(); ++i)
				{
					VkDeviceSize allocStart = allocations[i].offset;
					if (allocStart != offset && (allocStart > offset && (offset + size) >= allocStart))
					{
						bCanResizeInPlace = false;
					}
				}
			}

			if (bCanResizeInPlace)
			{
				UpdateAllocationSize(offset, size);
				return offset;

				//VkDeviceSize newBufferSize = offset + size;
				//VkResult result = Create(newBufferSize, m_UsageFlags, m_MemoryPropertyFlags);

				//if (result == VK_SUCCESS)
				//{
				//	// TODO: Copy previous contents in to new buffer?
				//	UpdateAllocationSize(offset, size);
				//	return offset;
				//}
				//else
				//{
				//	VK_CHECK_RESULT(result);
				//	return errorCode;
				//}
			}

			Free(offset);
			return Alloc(size);
		}

		void VulkanBuffer::Free(VkDeviceSize offset)
		{
			Unmap();

			for (u32 i = 0; i < (u32)allocations.size(); ++i)
			{
				if (allocations[i].offset == offset)
				{
					allocations.erase(allocations.begin() + i);
					break;
				}
			}
		}

		void VulkanBuffer::Shrink(real minUnused /* = 0.0f */)
		{
			VkDeviceSize usedSize = 0;
			// Find last allocated byte
			for (u32 i = 0; i < (u32)allocations.size(); ++i)
			{
				usedSize = glm::max(allocations[i].offset + allocations[i].size, usedSize);
			}

			VkDeviceSize excessBytes = m_Size - usedSize;
			real unused = (real)excessBytes / m_Size;
			if (unused >= minUnused)
			{
				VkDeviceSize newSize = glm::max(usedSize, (VkDeviceSize)1); // Size must be greater than zero
				VkResult result = Resize(newSize);

				VK_CHECK_RESULT(result);
			}
		}

		VkResult VulkanBuffer::Resize(VkDeviceSize newSize)
		{
			// Other allocations' data must survive the resize since their owners may not upload it again (e.g. static track meshes)
			std::vector<u8> contents;
			const bool bHostVisible = (m_MemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
			if (bHostVisible && m_Buffer != VK_NULL_HANDLE && m_Size > 0)
			{
				VkDeviceSize copySize = glm::min(m_Size, newSize);
				contents.resize((size_t)copySize);
				VK_CHECK_RESULT(Map(0, copySize));
				memcpy(contents.data(), m_Mapped, (size_t)copySize);
				Unmap();
			}

			if (m_Buffer != VK_NULL_HANDLE)
			{
				// Old buffer is destroyed in Create, ensure no in-flight frames still reference it
				vkDeviceWaitIdle(m_Device->m_LogicalDevice);
			}

			VkResult result = Create(newSize, m_UsageFlags, m_MemoryPropertyFlags, m_DEBUG_Name.empty() ? nullptr : m_DEBUG_Name.c_str());
			if (result == VK_SUCCESS && !contents.empty())
			{
				VK_CHECK_RESULT(Map(0, (VkDeviceSize)contents.size()));
				memcpy(m_Mapped, contents.data(), contents.size());
				Unmap();
			}

			return result;
		}

		void VulkanBuffer::UpdateAllocationSize(VkDeviceSize offset, VkDeviceSize newSize)
		{
			for (u32 i = 0; i < (u32)allocations.size(); ++i)
			{
				if (allocations[i].offset == offset)
				{
					allocations[i].size = newSize;
					break;
				}
			}
		}

		VkDeviceSize VulkanBuffer::GetAllocationSize(VkDeviceSize offset) const
		{
			for (u32 i = 0; i < (u32)allocations.size(); ++i)
			{
				if (allocations[i].offset == offset)
				{
					return allocations[i].size;
				}
			}

			return (VkDeviceSize)-1;
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN