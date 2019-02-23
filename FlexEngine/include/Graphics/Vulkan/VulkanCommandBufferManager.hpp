#pragma once

#include <vector>

IGNORE_WARNINGS_PUSH
//#include <vulkan/vulkan.hpp>
IGNORE_WARNINGS_POP

namespace flex
{
	namespace vk
	{
		struct VulkanDevice;

		class VulkanCommandBufferManager
		{
		public:
			VulkanCommandBufferManager();
			VulkanCommandBufferManager(VulkanDevice* device);
			~VulkanCommandBufferManager();


			/* Creates an individual command buffer, and optionally "begin"s it */
			static VkCommandBuffer CreateCommandBuffer(VulkanDevice* device, VkCommandBufferLevel level, bool begin);

			/* Ends commandBuffer and optionally frees its memory */
			static void FlushCommandBuffer(VulkanDevice* device, VkCommandBuffer commandBuffer, VkQueue queue, bool free);


			/* Creates a command pool used to generate command buffers */
			void CreatePool(VkSurfaceKHR surface);

			/* Creates count command buffers (should be equal to the number of backbuffers) */
			void CreateCommandBuffers(u32 count);

			/* @return true if all command buffers are valid */
			bool CheckCommandBuffers() const;

			/* Frees all command buffers */
			void DestroyCommandBuffers();


			/* Shortcut to overloaded function declared above */
			VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, bool begin);

			/* Shortcut to overloaded function declared above */
			void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free);

		private:
			friend class VulkanRenderer;

			std::vector<VkCommandBuffer> m_CommandBuffers;
			VulkanDevice* m_VulkanDevice = nullptr;

		};
	} // namespace vk
} // namespace flex