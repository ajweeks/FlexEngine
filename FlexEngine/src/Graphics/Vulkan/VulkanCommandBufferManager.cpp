#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanCommandBufferManager.hpp"

#include "Graphics/Vulkan/VulkanDevice.hpp"
#include "Graphics/Vulkan/VulkanHelpers.hpp"
#include "Graphics/Vulkan/VulkanInitializers.hpp"

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

		VkCommandBuffer VulkanCommandBufferManager::CreateCommandBuffer(VulkanDevice* device, VkCommandBufferLevel level, bool bBegin)
		{
			VkCommandBuffer commandBuffer;

			VkCommandBufferAllocateInfo commandBuffferAllocateInfo = vks::commandBufferAllocateInfo(device->m_CommandPool, level, 1);
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device->m_LogicalDevice, &commandBuffferAllocateInfo, &commandBuffer));

			if (bBegin)
			{
				VkCommandBufferBeginInfo commandBufferBeginInfo = vks::commandBufferBeginInfo();
				VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));
			}

			return commandBuffer;
		}

		void VulkanCommandBufferManager::FlushCommandBuffer(VulkanDevice* device, VkCommandBuffer commandBuffer, VkQueue queue, bool bFree)
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

			if (bFree)
			{
				vkFreeCommandBuffers(device->m_LogicalDevice, device->m_CommandPool, 1, &commandBuffer);
			}
		}

		void VulkanCommandBufferManager::CreatePool()
		{
			VkCommandPoolCreateInfo poolInfo = vks::commandPoolCreateInfo();
			poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			poolInfo.queueFamilyIndex = (u32)m_VulkanDevice->m_QueueFamilyIndices.graphicsFamily;

			VK_CHECK_RESULT(vkCreateCommandPool(m_VulkanDevice->m_LogicalDevice, &poolInfo, nullptr, m_VulkanDevice->m_CommandPool.replace()));
		}

		void VulkanCommandBufferManager::CreateCommandBuffers(u32 count)
		{
			if (!m_CommandBuffers.empty())
			{
				vkFreeCommandBuffers(m_VulkanDevice->m_LogicalDevice, m_VulkanDevice->m_CommandPool, m_CommandBuffers.size(), m_CommandBuffers.data());
				m_CommandBuffers.clear();
			}

			m_CommandBuffers.resize(count);

			VkCommandBufferAllocateInfo allocInfo = vks::commandBufferAllocateInfo(m_VulkanDevice->m_CommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, (u32)m_CommandBuffers.size());
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

		VkCommandBuffer VulkanCommandBufferManager::CreateCommandBuffer(VkCommandBufferLevel level, bool bBegin)
		{
			return CreateCommandBuffer(m_VulkanDevice, level, bBegin);
		}

		void VulkanCommandBufferManager::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool bFree)
		{
			FlushCommandBuffer(m_VulkanDevice, commandBuffer, queue, bFree);
		}

	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN