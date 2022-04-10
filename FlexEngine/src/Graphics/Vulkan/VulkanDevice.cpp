#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanDevice.hpp"

#include "Graphics/Vulkan/VulkanHelpers.hpp"
#include "Helpers.hpp"

namespace flex
{
	namespace vk
	{
		VulkanDevice::VulkanDevice(const CreateInfo& createInfo)
		{
			PROFILE_AUTO("VulkanDevice::VulkanDevice");

			// TODO: Move work to Initialize
			CHECK(createInfo.physicalDevice);
			m_PhysicalDevice = createInfo.physicalDevice;

			m_QueueFamilyIndices = FindQueueFamilies(createInfo.surface, m_PhysicalDevice);

			vkGetPhysicalDeviceProperties(m_PhysicalDevice, &m_PhysicalDeviceProperties);
			vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &m_PhysicalDeviceFeatures);
			vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &m_MemoryProperties);

			u32 queueFamilyCount;
			vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, nullptr);
			CHECK_GT(queueFamilyCount, 0u);
			m_QueueFamilyProperties.resize(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, m_QueueFamilyProperties.data());

			m_SupportedExtensions = GetSupportedExtensionsForDevice(m_PhysicalDevice);

			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
			std::set<i32> uniqueQueueFamilies = {
				m_QueueFamilyIndices.graphicsFamily,
				m_QueueFamilyIndices.presentFamily,
				m_QueueFamilyIndices.computeFamily
			};

			real queuePriority = 1.0f;
			for (i32 queueFamily : uniqueQueueFamilies)
			{
				VkDeviceQueueCreateInfo queueCreateInfo = {};
				queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfo.queueFamilyIndex = (u32)queueFamily;
				queueCreateInfo.queueCount = 1;
				queueCreateInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueCreateInfo);
			}

			m_EnabledExtensions.clear();

			for (const char* extName : *createInfo.requiredExtensions)
			{
				CHECK(ExtensionSupported(extName));
				m_EnabledExtensions.push_back(extName);
			}

			for (const char* extName : *createInfo.optionalExtensions)
			{
				if (ExtensionSupported(extName))
				{
					m_EnabledExtensions.push_back(extName);
				}
			}

			VkPhysicalDeviceFeatures2 supportedFeatures2 = {};
			supportedFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			VkPhysicalDeviceRayTracingPipelineFeaturesKHR supportedRayTracingFeatures = {};
			supportedRayTracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
			supportedFeatures2.pNext = &supportedRayTracingFeatures;
			vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &supportedFeatures2);

			if (createInfo.bTryEnableRayTracing)
			{
				m_bRayTracingSupported = supportedRayTracingFeatures.rayTracingPipeline != 0;
			}
			else
			{
				m_bRayTracingSupported = false;
			}

			if (m_bRayTracingSupported)
			{
				for (const char* extName : *createInfo.optionalExtensions)
				{
					if (ExtensionSupported(extName))
					{
						m_EnabledExtensions.push_back(extName);
					}
				}
			}

			VkPhysicalDeviceFeatures deviceFeatures = GetEnabledFeatures();

			VkPhysicalDeviceFeatures2 enabledFeatures2 = {};
			enabledFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			enabledFeatures2.features = deviceFeatures;

			if (m_bRayTracingSupported)
			{
				enabledFeatures2.pNext = &supportedRayTracingFeatures;
			}

			VkDeviceCreateInfo deviceCreateInfo = {};
			deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

			deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
			deviceCreateInfo.queueCreateInfoCount = (u32)queueCreateInfos.size();

			deviceCreateInfo.pEnabledFeatures = nullptr; // Referenced in pNext chain

			deviceCreateInfo.enabledExtensionCount = (u32)m_EnabledExtensions.size();
			deviceCreateInfo.ppEnabledExtensionNames = m_EnabledExtensions.data();

			if (createInfo.bEnableValidationLayers)
			{
				deviceCreateInfo.enabledLayerCount = (u32)createInfo.validationLayers->size();
				deviceCreateInfo.ppEnabledLayerNames = createInfo.validationLayers->data();
			}
			else
			{
				deviceCreateInfo.enabledLayerCount = 0;
			}

			deviceCreateInfo.pNext = &enabledFeatures2;

			{
				// TODO: Call on separate thread? Takes 500ms!
				PROFILE_AUTO("vkCreateDevice");
				VK_CHECK_RESULT(vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, m_LogicalDevice.replace()));
			}

			vkGetPhysicalDeviceProperties(m_PhysicalDevice, &m_PhysicalDeviceProperties);

			{
				PROFILE_AUTO("volkLoadDevice");
				volkLoadDevice(m_LogicalDevice);
			}

			m_CommandPool = { m_LogicalDevice, vkDestroyCommandPool };
		}

		VkResult VulkanDevice::AllocateMemory(const std::string& debugName, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
		{
			VkResult result = ::vkAllocateMemory(m_LogicalDevice, pAllocateInfo, pAllocator, pMemory);

			m_vkAllocations.emplace_back(VkAllocInfo{ debugName, *pMemory, (u64)pAllocateInfo->allocationSize });
			m_vkAllocations.sort([](const VkAllocInfo& a, const VkAllocInfo& b) {
				return a.size > b.size;
			});
			++m_vkAllocCount;
			m_vkAllocAmount += (u64)pAllocateInfo->allocationSize;

			return result;
		}

		void VulkanDevice::FreeMemory(VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
		{
			auto iter = std::find_if(m_vkAllocations.begin(), m_vkAllocations.end(), [memory](const VkAllocInfo& allocInfo)
			{
				return allocInfo.memory == memory;
			});
			if (iter != m_vkAllocations.end())
			{
				m_vkAllocAmount -= (u64)iter->size;
				m_vkAllocations.erase(iter);
			}
			else
			{
				PrintError("Freeing vk allocation which doesn't correspond to a tracked allocation! (memory: %p)\n", (void*)memory);
			}

			::vkFreeMemory(m_LogicalDevice, memory, pAllocator);

			++m_vkFreeCount;
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

		VkPhysicalDeviceFeatures VulkanDevice::GetEnabledFeatures()
		{
			PROFILE_AUTO("GetEnabledFeatures");

			VkPhysicalDeviceFeatures enabledFeatures = {};

			VkPhysicalDeviceFeatures supportedFeatures;
			vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &supportedFeatures);

			if (supportedFeatures.geometryShader)
			{
				enabledFeatures.geometryShader = VK_TRUE;
			}
			else
			{
				PrintError("Selected device does not support geometry shaders! No fallback in place.\n");
			}

			if (supportedFeatures.wideLines)
			{
				enabledFeatures.wideLines = VK_TRUE;
			}

			if (supportedFeatures.multiDrawIndirect)
			{
				enabledFeatures.multiDrawIndirect = VK_TRUE;
			}

			return enabledFeatures;
		}

		bool VulkanDevice::IsRayTracingSupported() const
		{
			return m_bRayTracingSupported;
		}

		void VulkanDevice::DrawImGuiRendererInfo() const
		{
			if (ImGui::TreeNode("Enabled device extensions"))
			{
				for (const char* extension : m_EnabledExtensions)
				{
					ImGui::BulletText("%s", extension);
				}
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Supported device extensions"))
			{
				for (const VkExtensionProperties& extension : m_SupportedExtensions)
				{
					ImGui::BulletText("%s", extension.extensionName);
				}
				ImGui::TreePop();
			}
		}

		bool VulkanDevice::ExtensionSupported(const char* extensionName) const
		{
			for (auto iter = m_SupportedExtensions.begin(); iter != m_SupportedExtensions.end(); ++iter)
			{
				if (strcmp(iter->extensionName, extensionName) == 0)
				{
					return true;
				}
			}
			return false;
		}

		bool VulkanDevice::ExtensionEnabled(const char* extensionName) const
		{
			for (auto iter = m_EnabledExtensions.begin(); iter != m_EnabledExtensions.end(); ++iter)
			{
				if (strcmp(*iter, extensionName) == 0)
				{
					return true;
				}
			}
			return false;
		}

		VulkanDevice::operator VkDevice()
		{
			return m_LogicalDevice;
		}

		std::vector<VkExtensionProperties> VulkanDevice::GetSupportedExtensionsForDevice(VkPhysicalDevice device)
		{
			u32 extensionCount;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

			std::vector<VkExtensionProperties> supportedExtensions(extensionCount);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, supportedExtensions.data());

			return supportedExtensions;
		}

		bool VulkanDevice::CheckDeviceSupportsExtensions(VkPhysicalDevice device, const std::vector<const char*>& inRequiredExtensions)
		{
			std::vector<VkExtensionProperties> supportedExtensions = GetSupportedExtensionsForDevice(device);

			std::set<std::string> requiredExtensions(inRequiredExtensions.begin(), inRequiredExtensions.end());

			for (const VkExtensionProperties& extension : supportedExtensions)
			{
				requiredExtensions.erase(extension.extensionName);
			}

			return requiredExtensions.empty();
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN