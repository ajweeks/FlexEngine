#pragma once
#if COMPILE_VULKAN

IGNORE_WARNINGS_PUSH
#include <vulkan/vulkan.hpp>
IGNORE_WARNINGS_POP

#include "VDeleter.hpp"

namespace flex
{
	namespace vk
	{
		struct VulkanDevice;
		struct FrameBuffer;

		class VulkanRenderPass
		{
		public:
			VulkanRenderPass();
			VulkanRenderPass(VulkanDevice* device);

			// TODO: Make work for multiple attachments & depth-only
			void Create(
				const char* passName,
				VkFormat colorFormat,
				FrameBuffer* inColorAttachmentFrameBuffer,
				VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				bool bDepth = false,
				VkFormat depthFormat = VK_FORMAT_UNDEFINED,
				VkImageLayout finalDepthLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VkImageLayout initialDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			void Create(VkRenderPassCreateInfo* createInfo, const char* passName);

			VkRenderPass* Replace();
			operator VkRenderPass();

			FrameBuffer* colorAttachmentFrameBuffer = nullptr;

		private:
			VulkanDevice* m_VulkanDevice = nullptr;

			VDeleter<VkRenderPass> m_RenderPass;

		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN