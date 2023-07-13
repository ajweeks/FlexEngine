#pragma once
#if COMPILE_VULKAN

#include "VDeleter.hpp"
#include "VulkanHelpers.hpp"

#include <list>

namespace flex
{
	namespace vk
	{
		struct VulkanDevice
		{
			struct CreateInfo
			{
				VkPhysicalDevice physicalDevice;
				VkSurfaceKHR surface;
				const std::vector<const char*>* requiredExtensions;
				const std::vector<const char*>* optionalExtensions;
				const std::vector<const char*>* rayTracingExtensions;
				bool bEnableValidationLayers;
				bool bTryEnableRayTracing;
				const std::vector<const char*>* validationLayers;
			};

			VulkanDevice(const CreateInfo& createInfo);

			VkResult AllocateMemory(const std::string& debugName, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory);
			void FreeMemory(VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator);

			u32 GetMemoryType(u32 typeBits, VkMemoryPropertyFlags properties, VkBool32* outMemTypeFound = nullptr) const;

			bool ExtensionSupported(const char* extensionName) const;
			bool ExtensionEnabled(const char* extensionName) const;

			bool IsRayTracingSupported() const;

			VkPhysicalDeviceFeatures GetEnabledFeatures();

			void DrawImGuiRendererInfo() const;

			operator VkDevice();

			static std::vector<VkExtensionProperties> GetSupportedExtensionsForDevice(VkPhysicalDevice device);
			static bool CheckDeviceSupportsExtensions(VkPhysicalDevice device, const std::vector<const char*>& inRequiredExtensions);

			VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
			VDeleter<VkDevice> m_LogicalDevice{ vkDestroyDevice };
			VDeleter<VkCommandPool> m_CommandPool;

			VkPhysicalDeviceProperties m_PhysicalDeviceProperties;
			VkPhysicalDeviceFeatures m_PhysicalDeviceFeatures;
			VkPhysicalDeviceMemoryProperties m_MemoryProperties;
			VkPhysicalDeviceMemoryProperties2 m_MemoryProperties2;
			std::vector<VkQueueFamilyProperties> m_QueueFamilyProperties;
			VulkanQueueFamilyIndices m_QueueFamilyIndices;
			std::vector<VkExtensionProperties> m_SupportedExtensions;
			std::vector<const char*> m_EnabledExtensions;

			std::list<VkAllocInfo> m_vkAllocations;
			u64 m_vkAllocAmount = 0;
			u64 m_vkAllocCount = 0;
			u64 m_vkFreeCount = 0;
			bool m_bRayTracingSupported = false;

		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN