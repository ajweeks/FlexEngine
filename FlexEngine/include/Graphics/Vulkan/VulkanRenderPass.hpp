#pragma once
#if COMPILE_VULKAN

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
			VulkanRenderPass(VulkanDevice* device);
			~VulkanRenderPass();

			VulkanRenderPass(const VulkanRenderPass&) = delete;
			VulkanRenderPass(VulkanRenderPass&&) = delete;
			VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;
			VulkanRenderPass& operator=(VulkanRenderPass&&) = delete;

			void Create();

			void Register(
				const char* passName,
				const std::vector<FrameBufferAttachmentID>& targtColourAttachmentIDs,
				FrameBufferAttachmentID targtDepthAttachmentID,
				const std::vector<FrameBufferAttachmentID>& sampledAttachmentIDs);

			void RegisterForColourAndDepth(
				const char* passName,
				FrameBufferAttachmentID targetColourAttachmentID,
				FrameBufferAttachmentID targetDepthAttachmentID,
				const std::vector<FrameBufferAttachmentID>& sampledAttachmentIDs);

			void RegisterForMultiColourAndDepth(
				const char* passName,
				const std::vector<FrameBufferAttachmentID>& targetColourAttachmentIDs,
				FrameBufferAttachmentID targetDepthAttachmentID,
				const std::vector<FrameBufferAttachmentID>& sampledAttachmentIDs);

			void RegisterForDepthOnly(
				const char* passName,
				FrameBufferAttachmentID targetDepthAttachmentID,
				const std::vector<FrameBufferAttachmentID>& sampledAttachmentIDs);

			void RegisterForColourOnly(
				const char* passName,
				FrameBufferAttachmentID targetColourAttachmentID,
				const std::vector<FrameBufferAttachmentID>& sampledAttachmentIDs);

			// This function should ideally not need to be called, instead register render passes in
			// VulkanRenderer::m_AutoTransitionedRenderPasses and these values will be worked out automatically
			void ManuallySpecifyLayouts(
				const std::vector<VkImageLayout>& finalLayouts = {}, // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				const std::vector<VkImageLayout>& initialLayouts = {}, // VK_IMAGE_LAYOUT_UNDEFINED
				VkImageLayout finalDepthLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VkImageLayout initialDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			VkRenderPass* Replace();

			operator VkRenderPass();

			void Begin(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount);
			void Begin_WithFrameBuffer(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount, FrameBuffer* targetFrameBuffer);

			void End();

			bool bCreateFrameBuffer = true;

		private:
			friend class VulkanRenderer;

			void Create(
				const char* passName,
				VkRenderPassCreateInfo* createInfo,
				const std::vector<VkImageView>& attachmentImageViews,
				u32 frameBufferWidth,
				u32 frameBufferHeight);

			void Begin(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount, FrameBuffer* targetFrameBuffer);

			VulkanDevice* m_VulkanDevice = nullptr;

			bool m_bRegistered = false;
			std::vector<VkImageLayout> m_TargetColourAttachmentInitialLayouts;
			std::vector<VkImageLayout> m_TargetColourAttachmentFinalLayouts;
			VkImageLayout m_TargetDepthAttachmentInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImageLayout m_TargetDepthAttachmentFinalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			std::vector<FrameBufferAttachmentID> m_TargetColourAttachmentIDs;
			FrameBufferAttachmentID m_TargetDepthAttachmentID = InvalidFrameBufferAttachmentID;
			std::vector<FrameBufferAttachmentID> m_SampledAttachmentIDs;

			VkFormat m_ColourAttachmentFormat = VK_FORMAT_UNDEFINED;
			VkFormat m_DepthAttachmentFormat = VK_FORMAT_UNDEFINED;

			VDeleter<VkRenderPass> m_RenderPass;
			FrameBuffer* m_FrameBuffer = nullptr;
			const char* m_Name = nullptr;

			// Points at a valid command buffer while this render pass is being recorded into (Between calls to Begin/End)
			VkCommandBuffer m_ActiveCommandBuffer = VK_NULL_HANDLE;

		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN