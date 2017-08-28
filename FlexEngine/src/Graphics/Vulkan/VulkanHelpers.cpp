#include "stdafx.h"

#include "Graphics/Vulkan/VulkanHelpers.h"

#include "Logger.h"
#include "VertexBufferData.h"

namespace flex
{
	namespace Vulkan
	{
		VkVertexInputBindingDescription GetVertexBindingDescription(VertexBufferData* vertexBufferData)
		{
			VkVertexInputBindingDescription bindingDesc = {};
			bindingDesc.binding = 0;
			bindingDesc.stride = vertexBufferData->VertexStride;
			bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			return bindingDesc;
		}

		void GetVertexAttributeDescriptions(
			VertexBufferData* vertexBufferData,
			std::vector<VkVertexInputAttributeDescription>& attributeDescriptions)
		{
			attributeDescriptions.clear();

			uint32_t offset = 0;
			uint32_t location = 0;

			// TODO: Roll into iteration over array

			if (vertexBufferData->HasAttribute(VertexBufferData::Attribute::POSITION))
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec3);
				++location;
			}

			if (vertexBufferData->HasAttribute(VertexBufferData::Attribute::POSITION_2D))
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec2);
				++location;
			}

			if (vertexBufferData->HasAttribute(VertexBufferData::Attribute::UV))
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec2);
				++location;
			}

			if (vertexBufferData->HasAttribute(VertexBufferData::Attribute::UVW))
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec3);
				++location;
			}
			if (vertexBufferData->HasAttribute(VertexBufferData::Attribute::COLOR_R8G8B8A8_UNORM))
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R8G8B8A8_UNORM;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::int32);
				++location;
			}

			if (vertexBufferData->HasAttribute(VertexBufferData::Attribute::COLOR_R32G32B32A32_SFLOAT))
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32A32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec4);
				++location;
			}

			if (vertexBufferData->HasAttribute(VertexBufferData::Attribute::TANGENT))
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec3);
				++location;
			}

			if (vertexBufferData->HasAttribute(VertexBufferData::Attribute::BITANGENT))
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec3);
				++location;
			}

			if (vertexBufferData->HasAttribute(VertexBufferData::Attribute::NORMAL))
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec3);
				++location;
			}
		}

	} // namespace Vulkan

	VulkanUniformBuffer::VulkanUniformBuffer(const VDeleter<VkDevice>& device) :
		constantBuffer(device),
		dynamicBuffer(device)
	{
	}

	VulkanTexture::VulkanTexture(const VDeleter<VkDevice>& device) :
		image(device, vkDestroyImage),
		imageMemory(device, vkFreeMemory),
		imageView(device, vkDestroyImageView),
		sampler(device, vkDestroySampler)
	{
	}

	RenderObject::RenderObject(const VDeleter<VkDevice>& device, RenderID renderID) :
		pipelineLayout(device, vkDestroyPipelineLayout),
		graphicsPipeline(device, vkDestroyPipeline),
		renderID(renderID)
	{
	}

	std::string VulkanErrorString(VkResult errorCode)
	{
		{
			switch (errorCode)
			{
#define STR(r) case VK_ ##r: return #r
				STR(NOT_READY);
				STR(TIMEOUT);
				STR(EVENT_SET);
				STR(EVENT_RESET);
				STR(INCOMPLETE);
				STR(ERROR_OUT_OF_HOST_MEMORY);
				STR(ERROR_OUT_OF_DEVICE_MEMORY);
				STR(ERROR_INITIALIZATION_FAILED);
				STR(ERROR_DEVICE_LOST);
				STR(ERROR_MEMORY_MAP_FAILED);
				STR(ERROR_LAYER_NOT_PRESENT);
				STR(ERROR_EXTENSION_NOT_PRESENT);
				STR(ERROR_FEATURE_NOT_PRESENT);
				STR(ERROR_INCOMPATIBLE_DRIVER);
				STR(ERROR_TOO_MANY_OBJECTS);
				STR(ERROR_FORMAT_NOT_SUPPORTED);
				STR(ERROR_SURFACE_LOST_KHR);
				STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
				STR(SUBOPTIMAL_KHR);
				STR(ERROR_OUT_OF_DATE_KHR);
				STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
				STR(ERROR_VALIDATION_FAILED_EXT);
				STR(ERROR_INVALID_SHADER_NV);
				STR(ERROR_OUT_OF_POOL_MEMORY_KHR);
				STR(ERROR_INVALID_EXTERNAL_HANDLE_KHR);
#undef STR
			case VK_SUCCESS:
				// No error to print
				return "";
			case VK_RESULT_RANGE_SIZE:
			case VK_RESULT_MAX_ENUM:
			case VK_RESULT_BEGIN_RANGE:
				return "INVALID_ENUM";
			default:
				return "UNKNOWN_ERROR";
			}
		}
	}

	void SetImageLayout(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkImageSubresourceRange subresourceRange,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask)
	{
		// Create an image barrier object
		VkImageMemoryBarrier imageMemoryBarrier = {};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.oldLayout = oldImageLayout;
		imageMemoryBarrier.newLayout = newImageLayout;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;

		// Source layouts (old)
		// Source access mask controls actions that have to be finished on the old layout
		// before it will be transitioned to the new layout
		switch (oldImageLayout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			// Image layout is undefined (or does not matter)
			// Only valid as initial layout
			// No flags required, listed only for completeness
			imageMemoryBarrier.srcAccessMask = 0;
			break;

		case VK_IMAGE_LAYOUT_PREINITIALIZED:
			// Image is preinitialized
			// Only valid as initial layout for linear images, preserves memory contents
			// Make sure host writes have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			// Image is a color attachment
			// Make sure any writes to the color buffer have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			// Image is a depth/stencil attachment
			// Make sure any writes to the depth/stencil buffer have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			// Image is a transfer source 
			// Make sure any reads from the image have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			// Image is a transfer destination
			// Make sure any writes to the image have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			// Image is read by a shader
			// Make sure any shader reads from the image have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		}

		// Target layouts (new)
		// Destination access mask controls the dependency for the new image layout
		switch (newImageLayout)
		{
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			// Image will be used as a transfer destination
			// Make sure any writes to the image have been finished
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			// Image will be used as a transfer source
			// Make sure any reads from and writes to the image have been finished
			imageMemoryBarrier.srcAccessMask = imageMemoryBarrier.srcAccessMask | VK_ACCESS_TRANSFER_READ_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			// Image will be used as a color attachment
			// Make sure any writes to the color buffer have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			// Image layout will be used as a depth/stencil attachment
			// Make sure any writes to depth/stencil buffer have been finished
			imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			// Image will be read in a shader (sampler, input attachment)
			// Make sure any writes to the image have been finished
			if (imageMemoryBarrier.srcAccessMask == 0)
			{
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		}

		// Put barrier inside setup command buffer
		vkCmdPipelineBarrier(
			cmdbuffer,
			srcStageMask,
			dstStageMask,
			0,
			0, nullptr,
			0, nullptr,
			1, &imageMemoryBarrier);
	}

	// Fixed sub resource on first mip level and layer
	void SetImageLayout(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkImageAspectFlags aspectMask,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask)
	{
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = aspectMask;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		SetImageLayout(cmdbuffer, image, oldImageLayout, newImageLayout, subresourceRange, srcStageMask, dstStageMask);
	}

	VkPrimitiveTopology TopologyModeToVkPrimitiveTopology(Renderer::TopologyMode mode)
	{
		switch (mode)
		{
		case Renderer::TopologyMode::POINT_LIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case Renderer::TopologyMode::LINE_LIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case Renderer::TopologyMode::LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		case Renderer::TopologyMode::TRIANGLE_LIST: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case Renderer::TopologyMode::TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case Renderer::TopologyMode::TRIANGLE_FAN: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
		case Renderer::TopologyMode::LINE_LOOP:
		{
			Logger::LogWarning("Unsupported TopologyMode passed to Vulkan Renderer: LINE_LOOP");
			return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
		}
		default: return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
		}
	}

	VkCullModeFlagBits CullFaceToVkCullMode(Renderer::CullFace cullFace)
	{
		switch (cullFace)
		{
		case Renderer::CullFace::BACK: return VK_CULL_MODE_BACK_BIT;
		case Renderer::CullFace::FRONT: return VK_CULL_MODE_FRONT_BIT;
		case Renderer::CullFace::NONE: // Fallthrough
		default:
			return VK_CULL_MODE_NONE;
		}
	}

	VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback)
	{
		auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
		if (func != nullptr)
		{
			return func(instance, pCreateInfo, pAllocator, pCallback);
		}
		else
		{
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}
	}

	void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator)
	{
		auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
		if (func != nullptr)
		{
			func(instance, callback, pAllocator);
		}
	}
} // namespace flex
