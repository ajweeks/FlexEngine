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

			void Create(const char* passName, VkRenderPassCreateInfo* createInfo,
				const std::vector<FrameBufferID>& inFrameBufferIDs);

			void CreateColorAndDepth(
				const char* passName,
				VkFormat colorFormat,
				FrameBufferID frameBufferID,
				VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				VkFormat depthFormat = VK_FORMAT_UNDEFINED,
				VkImageLayout finalDepthLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VkImageLayout initialDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			void CreateMultiColorAndDepth(
				const char* passName,
				FrameBufferID frameBufferID,
				VkFormat* colorFormats,
				u32 colorAttachmentCount,
				VkFormat depthFormat);

			void CreateDepthOnly(
				const char* passName,
				FrameBufferID frameBufferID,
				VkFormat depthFormat = VK_FORMAT_UNDEFINED,
				VkImageLayout finalDepthLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VkImageLayout initialDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED);

			void CreateColorOnly(
				const char* passName,
				VkFormat colorFormat,
				FrameBufferID frameBufferID,
				VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED);

			VkRenderPass* Replace();
			operator VkRenderPass();

			void Begin(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount);
			void Begin_WithFrameBuffer(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount, FrameBufferID frameBufferID);

			void End();

		private:
			void Create(
				const char* passName,
				const std::vector<FrameBufferID>& inFrameBufferIDs,
				VkAttachmentDescription* colorAttachments,
				VkAttachmentReference* colorAttachmentReferences,
				u32 colorAttachmentCount,
				VkAttachmentDescription* depthAttachment,
				VkAttachmentReference* depthAttachmentRef);

			void Begin(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount, FrameBufferID frameBufferID);

			VulkanDevice* m_VulkanDevice = nullptr;

			std::vector<FrameBufferID> frameBufferIDs;

			VDeleter<VkRenderPass> m_RenderPass;
			const char* m_Name = nullptr;

			// Points at a valid command buffer while this render pass is being recorded into (Between calls to Begin/End)
			VkCommandBuffer m_ActiveCommandBuffer = VK_NULL_HANDLE;

		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN