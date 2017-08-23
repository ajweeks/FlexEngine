#include "stdafx.h"

#include "Graphics/Vulkan/VulkanDevice.h"

#include "Logger.h"

namespace flex
{
	VulkanDevice::VulkanDevice(VkPhysicalDevice physicalDevice) :
		m_CommandPool({ m_LogicalDevice, vkDestroyCommandPool })
	{
		assert(physicalDevice);
		m_PhysicalDevice = physicalDevice;

		vkGetPhysicalDeviceProperties(physicalDevice, &m_PhysicalDeviceProperties);
		vkGetPhysicalDeviceFeatures(physicalDevice, &m_PhysicalDeviceFeatures);
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &m_MemoryProperties);

		uint32_t queueFamilyCount;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
		assert(queueFamilyCount > 0);
		m_QueueFamilyProperties.resize(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, m_QueueFamilyProperties.data());

		uint32_t extensionCount = 0;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
		if (extensionCount > 0)
		{
			std::vector<VkExtensionProperties> extensions(extensionCount);
			if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, &extensions.front()) == VK_SUCCESS)
			{
				for (auto ext : extensions)
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

	glm::uint VulkanDevice::GetMemoryType(glm::uint typeBits, VkMemoryPropertyFlags properties, VkBool32* memTypeFound)
	{
		for (uint32_t i = 0; i < m_MemoryProperties.memoryTypeCount; i++)
		{
			if ((typeBits & 1) == 1)
			{
				if ((m_MemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
				{
					if (memTypeFound)
					{
						*memTypeFound = true;
					}
					return i;
				}
			}
			typeBits >>= 1;
		}

		if (memTypeFound)
		{
			*memTypeFound = false;
			return 0;
		}
		else
		{
			Logger::LogError("GetMemoryType could not find specified memory type");
			return 0;
		}
	}
} // namespace flex
