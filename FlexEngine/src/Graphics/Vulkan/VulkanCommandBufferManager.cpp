#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanCommandBufferManager.hpp"

#include "Graphics/Vulkan/VulkanDevice.hpp"
#include "Graphics/Vulkan/VulkanHelpers.hpp"

namespace flex
{
	namespace vk
	{
		VulkanCommandBufferManager::VulkanCommandBufferManager()
		{
		}

		VulkanCommandBufferManager::VulkanCommandBufferManager(VulkanDevice* device) :
			m_VulkanDevice(device)
		{
		}

		VulkanCommandBufferManager::~VulkanCommandBufferManager()
		{
		}

		VkCommandBuffer VulkanCommandBufferManager::CreateCommandBuffer(VulkanDevice* device, VkCommandBufferLevel level, bool begin)
		{
			VkCommandBuffer commandBuffer;

			VkCommandBufferAllocateInfo commandBuffferAllocateInfo = {};
			commandBuffferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			commandBuffferAllocateInfo.commandPool = device->m_CommandPool;
			commandBuffferAllocateInfo.level = level;
			commandBuffferAllocateInfo.commandBufferCount = 1;

			VK_CHECK_RESULT(vkAllocateCommandBuffers(device->m_LogicalDevice, &commandBuffferAllocateInfo, &commandBuffer));

			if (begin)
			{
				VkCommandBufferBeginInfo commandBufferBeginInfo = {};
				commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));
			}

			return commandBuffer;
		}

		void VulkanCommandBufferManager::FlushCommandBuffer(VulkanDevice* device, VkCommandBuffer commandBuffer, VkQueue queue, bool free)
		{
			if (commandBuffer == VK_NULL_HANDLE)
			{
				return;
			}

			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
			VK_CHECK_RESULT(vkQueueWaitIdle(queue));

			if (free)
			{
				vkFreeCommandBuffers(device->m_LogicalDevice, device->m_CommandPool, 1, &commandBuffer);
			}
		}

		void VulkanCommandBufferManager::CreatePool(VkSurfaceKHR surface)
		{
			VulkanQueueFamilyIndices queueFamilyIndices = FindQueueFamilies(surface, m_VulkanDevice->m_PhysicalDevice);

			VkCommandPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			poolInfo.queueFamilyIndex = (u32)queueFamilyIndices.graphicsFamily;

			VK_CHECK_RESULT(vkCreateCommandPool(m_VulkanDevice->m_LogicalDevice, &poolInfo, nullptr, m_VulkanDevice->m_CommandPool.replace()));
		}

		void VulkanCommandBufferManager::CreateCommandBuffers(u32 count)
		{
			m_CommandBuffers.resize(count);

			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = m_VulkanDevice->m_CommandPool;
			allocInfo.commandBufferCount = (u32)m_CommandBuffers.size();

			VK_CHECK_RESULT(vkAllocateCommandBuffers(m_VulkanDevice->m_LogicalDevice, &allocInfo, m_CommandBuffers.data()));
		}

		bool VulkanCommandBufferManager::CheckCommandBuffers() const
		{
			for (const VkCommandBuffer& cmdBuffer : m_CommandBuffers)
			{
				if (cmdBuffer == VK_NULL_HANDLE)
				{
					return false;
				}
			}
			return true;
		}

		void VulkanCommandBufferManager::DestroyCommandBuffers()
		{
			vkFreeCommandBuffers(m_VulkanDevice->m_LogicalDevice, m_VulkanDevice->m_CommandPool, static_cast<u32>(m_CommandBuffers.size()), m_CommandBuffers.data());
		}

		VkCommandBuffer VulkanCommandBufferManager::CreateCommandBuffer(VkCommandBufferLevel level, bool begin)
		{
			return CreateCommandBuffer(m_VulkanDevice, level, begin);
		}

		void VulkanCommandBufferManager::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free)
		{
			FlushCommandBuffer(m_VulkanDevice, commandBuffer, queue, free);
		}

	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN