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

			~VulkanRenderPass();

			void Create(
				const char* passName,
				VkRenderPassCreateInfo* createInfo);

			void Create(
				VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				VkImageLayout finalDepthLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VkImageLayout initialDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			void Register(
				const char* passName,
				const std::vector<FrameBufferID>& inTargetFrameBufferIDs,
				const std::vector<FrameBufferID>& inSampledFrameBufferIDs);

			void RegisterForColorAndDepth(
				const char* passName,
				VkFormat colorFormat,
				FrameBufferID targetFrameBufferID,
				const std::vector<FrameBufferID>& inSampledFrameBufferIDs,
				VkFormat depthFormat = VK_FORMAT_UNDEFINED);

			void RegisterForMultiColorAndDepth(
				const char* passName,
				FrameBufferID targetFrameBufferID,
				const std::vector<FrameBufferID>& inSampledFrameBufferIDs,
				VkFormat* inColorFormats,
				u32 colorAttachmentCount,
				VkFormat depthFormat);

			void RegisterForDepthOnly(
				const char* passName,
				FrameBufferID targetFrameBufferID,
				const std::vector<FrameBufferID>& inSampledFrameBufferIDs,
				VkFormat depthFormat = VK_FORMAT_UNDEFINED);

			void RegisterForColorOnly(
				const char* passName,
				VkFormat colorFormat,
				FrameBufferID targetFrameBufferID,
				const std::vector<FrameBufferID>& inSampledFrameBufferIDs);

			VkRenderPass* Replace();
			operator VkRenderPass();

			void Begin(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount);
			void Begin_WithFrameBuffer(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount, FrameBufferID targetFrameBufferID);

			void End();

		private:
			friend class VulkanRenderer;

			void Begin(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount, FrameBufferID targetFrameBufferID);

			VulkanDevice* m_VulkanDevice = nullptr;

			// Registered values
			bool m_bRegistered = false;
			u32 m_ColorAttachmentCount = 0;
			bool m_bDepthAttachmentPresent = false;
			VkFormat* m_ColorFormats = nullptr;
			VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;
			VkImageLayout m_ColorAttachmentInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImageLayout m_ColorAttachmentFinalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImageLayout m_DepthAttachmentInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImageLayout m_DepthAttachmentFinalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			std::vector<FrameBufferID> m_TargetFrameBufferIDs;
			std::vector<FrameBufferID> m_SampledFrameBufferIDs;

			VDeleter<VkRenderPass> m_RenderPass;
			const char* m_Name = nullptr;

			// Points at a valid command buffer while this render pass is being recorded into (Between calls to Begin/End)
			VkCommandBuffer m_ActiveCommandBuffer = VK_NULL_HANDLE;

		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN