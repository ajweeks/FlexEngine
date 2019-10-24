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

		void VulkanRenderPass::Create(
			const char* passName,
			VkFormat colorFormat,
			FrameBuffer* inColorAttachmentFrameBuffer,
			VkImageLayout finalLayout /* = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL */,
			VkImageLayout initialLayout /* = VK_IMAGE_LAYOUT_UNDEFINED */,
			bool bDepth /* = false */,
			VkFormat depthFormat /* = VK_FORMAT_UNDEFINED */,
			VkImageLayout finalDepthLayout /* = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL */,
			VkImageLayout initialDepthLayout /* = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL */)
		{
			colorAttachmentFrameBuffer = inColorAttachmentFrameBuffer;

			// Color attachment
			VkAttachmentDescription colorAttachment = vks::attachmentDescription(colorFormat, finalLayout);
			VkAttachmentDescription depthAttachment = vks::attachmentDescription(depthFormat, finalDepthLayout);

			if (initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
			{
				colorAttachment.initialLayout = initialLayout;
				colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

				if (bDepth)
				{
					depthAttachment.initialLayout = initialDepthLayout;
					depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				}
			}

			VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			VkAttachmentReference depthAttachmentRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

			std::array<VkSubpassDescription, 1> subpasses;
			subpasses[0] = {};
			subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpasses[0].colorAttachmentCount = 1;
			subpasses[0].pColorAttachments = &colorReference;
			subpasses[0].pDepthStencilAttachment = bDepth ? &depthAttachmentRef : nullptr;

			// Use subpass dependencies for layout transitions
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

			std::vector<VkAttachmentDescription> attachments;
			attachments.push_back(colorAttachment);

			if (bDepth)
			{
				attachments.push_back(depthAttachment);
			}

			// Render pass
			VkRenderPassCreateInfo renderPassCreateInfo = vks::renderPassCreateInfo();
			renderPassCreateInfo.attachmentCount = attachments.size();
			renderPassCreateInfo.pAttachments = attachments.data();
			renderPassCreateInfo.subpassCount = subpasses.size();
			renderPassCreateInfo.pSubpasses = subpasses.data();
			renderPassCreateInfo.dependencyCount = dependencies.size();
			renderPassCreateInfo.pDependencies = dependencies.data();
			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassCreateInfo, nullptr, m_RenderPass.replace()));

			((VulkanRenderer*)g_Renderer)->SetRenderPassName(m_VulkanDevice, m_RenderPass, passName);
		}

		void VulkanRenderPass::Create(VkRenderPassCreateInfo* createInfo, const char* passName)
		{
			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, createInfo, nullptr, m_RenderPass.replace()));
			((VulkanRenderer*)g_Renderer)->SetRenderPassName(m_VulkanDevice, m_RenderPass, passName);
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