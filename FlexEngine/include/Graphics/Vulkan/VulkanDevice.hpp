#pragma once
#if COMPILE_VULKAN

#include "VDeleter.hpp"

namespace flex
{
	namespace vk
	{
		struct VulkanDevice
		{
			VulkanDevice(VkPhysicalDevice physicalDevice);

			u32 GetMemoryType(u32 typeBits, VkMemoryPropertyFlags properties, VkBool32* outMemTypeFound = nullptr) const;

			operator VkDevice();

			VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
			VDeleter<VkDevice> m_LogicalDevice{ vkDestroyDevice };
			VDeleter<VkCommandPool> m_CommandPool;

			VkPhysicalDeviceProperties m_PhysicalDeviceProperties;
			VkPhysicalDeviceFeatures m_PhysicalDeviceFeatures;
			VkPhysicalDeviceMemoryProperties m_MemoryProperties;
			std::vector<VkQueueFamilyProperties> m_QueueFamilyProperties;
			std::vector<std::string> m_SupportedExtensions;
		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN