#pragma once

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


			/* Creates an individual command buffer, and optionally begins it */
			static VkCommandBuffer CreateCommandBuffer(VulkanDevice* device, VkCommandBufferLevel level, bool bBegin);

			/* Ends commandBuffer and optionally frees its memory */
			static void FlushCommandBuffer(VulkanDevice* device, VkCommandBuffer commandBuffer, VkQueue queue, bool bFree);


			/* Creates a command pool used to generate command buffers */
			void CreatePool();

			/* Creates count command buffers (should be equal to the number of backbuffers) */
			void CreateCommandBuffers(u32 count);

			/* Returns true if all command buffers are valid */
			bool CheckCommandBuffers() const;

			/* Frees all command buffers */
			void DestroyCommandBuffers();


			/* Shortcut to overloaded function declared above */
			VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, bool bBegin);

			/* Shortcut to overloaded function declared above */
			void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool bFree);

		private:
			friend class VulkanRenderer;

			// TODO: CLEANUP: Make single command buffer
			std::vector<VkCommandBuffer> m_CommandBuffers;
			VulkanDevice* m_VulkanDevice = nullptr;

		};
	} // namespace vk
} // namespace flex