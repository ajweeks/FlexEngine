#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanBuffer.hpp"

#include "Graphics/Vulkan/VulkanDevice.hpp"
#include "Graphics/Vulkan/VulkanInitializers.hpp"

namespace flex
{
	namespace vk
	{
		VulkanBuffer::VulkanBuffer(VulkanDevice* device) :
			m_Device(device),
			m_Buffer(VDeleter<VkBuffer>(device->m_LogicalDevice, vkDestroyBuffer)),
			m_Memory(VDeleter<VkDeviceMemory>(device->m_LogicalDevice, vkFreeMemory))
		{
		}

		VkResult VulkanBuffer::Create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
		{
			VkBufferCreateInfo bufferInfo = vks::bufferCreateInfo(usage, size);
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VK_CHECK_RESULT(vkCreateBuffer(m_Device->m_LogicalDevice, &bufferInfo, nullptr, m_Buffer.replace()));

			VkMemoryRequirements memRequirements;
			vkGetBufferMemoryRequirements(m_Device->m_LogicalDevice, m_Buffer, &memRequirements);

			VkMemoryAllocateInfo allocInfo = vks::memoryAllocateInfo(memRequirements.size);
			allocInfo.memoryTypeIndex = FindMemoryType(m_Device, memRequirements.memoryTypeBits, properties);

			VK_CHECK_RESULT(vkAllocateMemory(m_Device->m_LogicalDevice, &allocInfo, nullptr, m_Memory.replace()));

			// Create the memory backing up the buffer handle
			m_Alignment = memRequirements.alignment;
			m_Size = allocInfo.allocationSize;
			m_UsageFlags = usage;
			m_MemoryPropertyFlags = properties;

			return Bind();
		}

		void VulkanBuffer::Destroy()
		{
			m_Buffer.replace();
			m_Memory.replace();
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

		VkDeviceSize VulkanBuffer::Alloc(VkDeviceSize size, bool bCanResize)
		{
			Unmap();

			const VkDeviceSize errorCode = (VkDeviceSize)-1;

			if (size > m_Size && !bCanResize)
			{
				return errorCode;
			}

			VkDeviceSize offset = 0;
			for (u32 i = 0; i < (u32)allocations.size(); /**/)
			{
				if (allocations[i].offset >= offset && allocations[i].offset < (offset + size))
				{
					// Overlaps current guess
					offset = allocations[i].offset + allocations[i].size;
					if (offset + size > m_Size)
					{
						if (!bCanResize)
						{
							return errorCode;
						}
						VkDeviceSize newSize = offset + size; // TODO: Grow by larger amount?
						VkResult result = Create(newSize, m_UsageFlags, m_MemoryPropertyFlags);
						if (result == VK_SUCCESS)
						{
							// TODO: Copy previous contents in to new buffer?
							allocations.push_back({ offset, size });
							return offset;
						}
						else
						{
							VK_CHECK_RESULT(result);
							return errorCode;
						}
					}
					i = 0;
					continue;
				}

				++i;
			}

			if (offset + size < m_Size)
			{
				allocations.push_back({ offset, size });
				return offset;
			}
			else
			{
				VkDeviceSize newSize = offset + size; // TODO: Grow by larger amount?
				VkResult result = Create(newSize, m_UsageFlags, m_MemoryPropertyFlags);
				if (result == VK_SUCCESS)
				{
					// TODO: Copy previous contents in to new buffer?
					allocations.push_back({ offset, size });
					return offset;
				}
				else
				{
					VK_CHECK_RESULT(result);
					return errorCode;
				}
			}
		}

		VkDeviceSize VulkanBuffer::Realloc(VkDeviceSize offset, VkDeviceSize size, bool bCanResize)
		{
			Unmap();

			const VkDeviceSize errorCode = (VkDeviceSize)-1;

			bool bCanResizeInPlace = true;
			for (u32 i = 0; i < (u32)allocations.size(); ++i)
			{
				if (allocations[i].offset != offset &&
					((allocations[i].offset < offset && (allocations[i].offset + allocations[i].size) > offset) ||
					(allocations[i].offset < (offset + size))))
				{
					bCanResizeInPlace = false;
				}
			}

			if (bCanResizeInPlace)
			{
				if ((offset + size) <= m_Size)
				{
					UpdateAllocationSize(offset, size);
					return offset;
				}
				else if (bCanResize)
				{
					// TODO: Use smarter growth algorithm?
					VkDeviceSize newSize = offset + size;
					VkResult result = Create(newSize, m_UsageFlags, m_MemoryPropertyFlags);

					if (result == VK_SUCCESS)
					{
						// TODO: Copy previous contents in to new buffer?
						UpdateAllocationSize(offset, newSize);
						return offset;
					}
					else
					{
						VK_CHECK_RESULT(result);
						return errorCode;
					}
				}
			}

			return Alloc(size, bCanResize);
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
			for (u32 i = 0; i < (u32)allocations.size(); ++i)
			{
				usedSize = glm::max(allocations[i].offset + allocations[i].size, usedSize);
			}

			VkDeviceSize excessBytes = m_Size - usedSize;
			real unused = (real)excessBytes / m_Size;
			if (unused >= minUnused)
			{
				VkDeviceSize newSize = usedSize;
				VkResult result = Create(newSize, m_UsageFlags, m_MemoryPropertyFlags);

				VK_CHECK_RESULT(result);
			}
		}

		VkDeviceSize VulkanBuffer::SizeOf(VkDeviceSize offset)
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
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN