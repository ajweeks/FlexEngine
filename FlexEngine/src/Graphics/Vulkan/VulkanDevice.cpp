#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanDevice.hpp"

namespace flex
{
	namespace vk
	{
		VulkanDevice::VulkanDevice(VkPhysicalDevice physicalDevice) :
			m_CommandPool({ m_LogicalDevice, vkDestroyCommandPool })
		{
			assert(physicalDevice);
			m_PhysicalDevice = physicalDevice;

			vkGetPhysicalDeviceProperties(physicalDevice, &m_PhysicalDeviceProperties);
			vkGetPhysicalDeviceFeatures(physicalDevice, &m_PhysicalDeviceFeatures);
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &m_MemoryProperties);

			u32 queueFamilyCount;
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
			assert(queueFamilyCount > 0);
			m_QueueFamilyProperties.resize(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, m_QueueFamilyProperties.data());

			u32 extensionCount = 0;
			vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
			if (extensionCount > 0)
			{
				std::vector<VkExtensionProperties> extensions(extensionCount);
				if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, &extensions.front()) == VK_SUCCESS)
				{
					for (VkExtensionProperties& ext : extensions)
					{
						m_SupportedExtensions.push_back(ext.extensionName);
					}
				}
			}
		}

		VulkanDevice::operator VkDevice()
		{
			return m_LogicalDevice;
		}

		u32 VulkanDevice::GetMemoryType(u32 typeBits, VkMemoryPropertyFlags properties, VkBool32* outMemTypeFound) const
		{
			for (u32 i = 0; i < m_MemoryProperties.memoryTypeCount; i++)
			{
				if ((typeBits & 1) == 1)
				{
					if ((m_MemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
					{
						if (outMemTypeFound)
						{
							*outMemTypeFound = true;
						}
						return i;
					}
				}
				typeBits >>= 1;
			}

			if (outMemTypeFound)
			{
				*outMemTypeFound = false;
				return 0;
			}
			else
			{
				PrintError("GetMemoryType could not find specified memory type\n");
				return 0;
			}
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN