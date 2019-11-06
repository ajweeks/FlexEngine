#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanRenderPass.hpp"

#include "Graphics/Vulkan/VulkanDevice.hpp"
#include "Graphics/Vulkan/VulkanHelpers.hpp"
#include "Graphics/Vulkan/VulkanInitializers.hpp"
#include "Graphics/Vulkan/VulkanRenderer.hpp"

namespace flex
{
	namespace vk
	{
		VulkanRenderPass::VulkanRenderPass()
		{
		}

		VulkanRenderPass::VulkanRenderPass(VulkanDevice* device) :
			m_RenderPass{ device->m_LogicalDevice, vkDestroyRenderPass },
			m_VulkanDevice(device)
		{
		}
		
		VulkanRenderPass::~VulkanRenderPass()
		{
			free_hooked(m_ColorFormats);
		}

		void VulkanRenderPass::Create(
			const char* passName,
			VkRenderPassCreateInfo* inCreateInfo)
		{
			m_Name = passName;
			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, inCreateInfo, nullptr, m_RenderPass.replace()));
			((VulkanRenderer*)g_Renderer)->SetRenderPassName(m_VulkanDevice, m_RenderPass, m_Name);
		}

		void VulkanRenderPass::Create(
			VkImageLayout finalLayout /* = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL */,
			VkImageLayout initialLayout /* = VK_IMAGE_LAYOUT_UNDEFINED */,
			VkImageLayout finalDepthLayout /* = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL */,
			VkImageLayout initialDepthLayout /* = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL */)
		{
			assert(m_bRegistered); // This function can only be called on render passes whose Register[...] function has been called

			m_ColorAttachmentFinalLayout = finalLayout;
			m_ColorAttachmentInitialLayout = initialLayout;
			m_DepthAttachmentFinalLayout = finalDepthLayout;
			m_DepthAttachmentInitialLayout = initialDepthLayout;

			std::vector<VkAttachmentDescription> colorAttachments(m_ColorAttachmentCount);
			for (u32 i = 0; i < m_ColorAttachmentCount; ++i)
			{
				colorAttachments[i] = vks::attachmentDescription(m_ColorFormats[i], m_ColorAttachmentFinalLayout);
			}

			std::vector<VkAttachmentReference> colorAttachmentReferences(m_ColorAttachmentCount);
			for (u32 i = 0; i < m_ColorAttachmentCount; ++i)
			{
				colorAttachmentReferences[i] = VkAttachmentReference{ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			}

			if (m_ColorAttachmentInitialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
			{
				// TODO: Support multiple differing initial layouts
				colorAttachments[0].initialLayout = m_ColorAttachmentInitialLayout;
				colorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			}

			VkAttachmentDescription depthAttachment = vks::attachmentDescription(m_DepthFormat, m_DepthAttachmentFinalLayout);

			VkAttachmentReference depthAttachmentRef = { m_ColorAttachmentCount, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

			if (m_DepthAttachmentInitialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
			{
				depthAttachment.initialLayout = m_DepthAttachmentInitialLayout;
				depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			}

			std::vector<VkAttachmentDescription> attachments(m_ColorAttachmentCount + (m_bDepthAttachmentPresent ? 1 : 0));
			for (u32 i = 0; i < m_ColorAttachmentCount; ++i)
			{
				attachments[i] = colorAttachments[i];
			}

			if (m_bDepthAttachmentPresent)
			{
				attachments[attachments.size() - 1] = depthAttachment;
			}

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = m_ColorAttachmentCount;
			subpass.pColorAttachments = colorAttachmentReferences.data();
			subpass.pDepthStencilAttachment = m_bDepthAttachmentPresent ? &depthAttachmentRef : nullptr;

			std::array<VkSubpassDependency, 2> dependencies;
			dependencies[0] = {};
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1] = {};
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassCreateInfo = vks::renderPassCreateInfo();
			renderPassCreateInfo.attachmentCount = attachments.size();
			renderPassCreateInfo.pAttachments = attachments.data();
			renderPassCreateInfo.subpassCount = 1;
			renderPassCreateInfo.pSubpasses = &subpass;
			renderPassCreateInfo.dependencyCount = dependencies.size();
			renderPassCreateInfo.pDependencies = dependencies.data();

			Create(m_Name, &renderPassCreateInfo);
		}

		void VulkanRenderPass::Register(
			const char* passName,
			const std::vector<FrameBufferID>& inTargetFrameBufferIDs,
			const std::vector<FrameBufferID>& inSampledFrameBufferIDs)
		{
			m_TargetFrameBufferIDs = inTargetFrameBufferIDs;
			m_SampledFrameBufferIDs = inSampledFrameBufferIDs;
			m_Name = passName;
			m_bRegistered = true;
		}

		void VulkanRenderPass::RegisterForColorAndDepth(
			const char* passName,
			VkFormat colorFormat,
			FrameBufferID targetFrameBufferID,
			const std::vector<FrameBufferID>& inSampledFrameBufferIDs,
			VkFormat depthFormat /* = VK_FORMAT_UNDEFINED */)
		{
			m_ColorAttachmentCount = 1;
			m_bDepthAttachmentPresent = true;

			m_ColorFormats = (VkFormat*)malloc_hooked(1 * sizeof(VkFormat));
			memcpy(m_ColorFormats, &colorFormat, 1 * sizeof(VkFormat));

			m_DepthFormat = depthFormat;

			Register(passName, { targetFrameBufferID }, inSampledFrameBufferIDs);
		}

		void VulkanRenderPass::RegisterForMultiColorAndDepth(
			const char* passName,
			FrameBufferID targetFrameBufferID,
			const std::vector<FrameBufferID>& inSampledFrameBufferIDs,
			VkFormat* inColorFormats,
			u32 colorAttachmentCount,
			VkFormat depthFormat)
		{
			assert(colorAttachmentCount >= 1);

			m_ColorAttachmentCount = colorAttachmentCount;
			m_bDepthAttachmentPresent = true;

			m_ColorFormats = (VkFormat*)malloc_hooked(colorAttachmentCount * sizeof(VkFormat));
			memcpy(m_ColorFormats, inColorFormats, colorAttachmentCount * sizeof(VkFormat));

			m_DepthFormat = depthFormat;

			Register(passName, { targetFrameBufferID }, inSampledFrameBufferIDs);
		}

		void VulkanRenderPass::RegisterForDepthOnly(
			const char* passName,
			FrameBufferID targetFrameBufferID,
			const std::vector<FrameBufferID>& inSampledFrameBufferIDs,
			VkFormat depthFormat /* = VK_FORMAT_UNDEFINED */)
		{
			m_ColorAttachmentCount = 0;
			m_bDepthAttachmentPresent = true;

			m_DepthFormat = depthFormat;

			Register(passName, { targetFrameBufferID }, inSampledFrameBufferIDs);
		}

		void VulkanRenderPass::RegisterForColorOnly(
			const char* passName,
			VkFormat colorFormat,
			FrameBufferID targetFrameBufferID,
			const std::vector<FrameBufferID>& inSampledFrameBufferIDs)
		{
			m_ColorAttachmentCount = 1;
			m_bDepthAttachmentPresent = false;

			m_ColorFormats = (VkFormat*)malloc_hooked(1 * sizeof(VkFormat));
			memcpy(m_ColorFormats, &colorFormat, 1 * sizeof(VkFormat));

			Register(passName, { targetFrameBufferID }, inSampledFrameBufferIDs);
		}

		void VulkanRenderPass::Begin_WithFrameBuffer(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount, FrameBufferID targetFrameBufferID)
		{
			Begin(cmdBuf, clearValues, clearValueCount, targetFrameBufferID);
		}

		void VulkanRenderPass::Begin(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount)
		{
			Begin(cmdBuf, clearValues, clearValueCount, m_TargetFrameBufferIDs[0]);
		}

		void VulkanRenderPass::Begin(VkCommandBuffer cmdBuf, VkClearValue* clearValues, u32 clearValueCount, FrameBufferID targetFrameBufferID)
		{
			if (m_ActiveCommandBuffer != VK_NULL_HANDLE)
			{
				PrintError("Attempted to begin render pass (%s) multiple times! Did you forget to call End?\n", m_Name);
				return;
			}

			m_ActiveCommandBuffer = cmdBuf;

			FrameBuffer* frameBuffer = ((VulkanRenderer*)g_Renderer)->GetFrameBuffer(targetFrameBufferID);

			VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(m_RenderPass);

			renderPassBeginInfo.renderPass = m_RenderPass;
			renderPassBeginInfo.framebuffer = frameBuffer->frameBuffer;
			renderPassBeginInfo.renderArea.offset = { 0, 0 };
			renderPassBeginInfo.renderArea.extent = { frameBuffer->width, frameBuffer->height };
			renderPassBeginInfo.clearValueCount = clearValueCount;
			renderPassBeginInfo.pClearValues = clearValues;

			vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		}

		void VulkanRenderPass::End()
		{
			if (m_ActiveCommandBuffer == VK_NULL_HANDLE)
			{
				PrintError("Attempted to end render pass which has invalid m_ActiveCommandBuffer (%s)", m_Name);
				return;
			}

			vkCmdEndRenderPass(m_ActiveCommandBuffer);

			m_ActiveCommandBuffer = VK_NULL_HANDLE;
		}

		VkRenderPass* VulkanRenderPass::Replace()
		{
			return m_RenderPass.replace();
		}

		VulkanRenderPass::operator VkRenderPass()
		{
			return m_RenderPass;
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN