
#include "stdafx.h"

#include "Graphics/Vulkan/VulkanHelpers.h"
#include "VertexBufferData.h"
#include "Logger.h"

VkVertexInputBindingDescription VulkanVertex::GetVertexBindingDescription(VertexBufferData* vertexBufferData)
{
	VkVertexInputBindingDescription bindingDesc = {};
	bindingDesc.binding = 0;
	bindingDesc.stride = vertexBufferData->VertexStride;
	bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	return bindingDesc;
}

// TODO: Use m_HasElement rather than this shit
void VulkanVertex::GetVertexAttributeDescriptions(
	VertexBufferData* vertexBufferData, 
	std::vector<VkVertexInputAttributeDescription>& attributeDescriptions,
	glm::uint shaderIndex)
{
	attributeDescriptions.clear();

	if (shaderIndex == 1)
	{
		attributeDescriptions.resize(6);

		uint32_t offset = 0;

		// Position
		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = offset;
		offset += sizeof(glm::vec3);

		// Color
		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[1].offset = offset;
		offset += sizeof(glm::vec4);

		// Tangent
		attributeDescriptions[2].binding = 0;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[2].offset = offset;
		offset += sizeof(glm::vec3);

		// Bitangent
		attributeDescriptions[3].binding = 0;
		attributeDescriptions[3].location = 3;
		attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[3].offset = offset;
		offset += sizeof(glm::vec3);

		// Normal
		attributeDescriptions[4].binding = 0;
		attributeDescriptions[4].location = 4;
		attributeDescriptions[4].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[4].offset = offset;
		offset += sizeof(glm::vec3);

		// Tex coord
		attributeDescriptions[5].binding = 0;
		attributeDescriptions[5].location = 5;
		attributeDescriptions[5].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[5].offset = offset;
		offset += sizeof(glm::vec2);
	}
	else if (shaderIndex == 2)
	{
		attributeDescriptions.resize(2);

		uint32_t offset = 0;

		// Position
		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = offset;
		offset += sizeof(glm::vec3);

		// Color
		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[1].offset = offset;
		offset += sizeof(glm::vec4);
	}
	else 
	{
		Logger::LogError("Unhandled shader type passed to GetVertexAttrivuteDescriptions " + std::to_string(shaderIndex));
	}
}

UniformBuffers_Simple::UniformBuffers_Simple(const VDeleter<VkDevice>& device) :
	viewBuffer(device),
	dynamicBuffer(device)
{
}

UniformBuffers_Color::UniformBuffers_Color(const VDeleter<VkDevice>& device) :
	viewBuffer(device),
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

RenderObject::RenderObject(const VDeleter<VkDevice>& device) :
	pipelineLayout(device, vkDestroyPipelineLayout),
	graphicsPipeline(device, vkDestroyPipeline)
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
#undef STR
		default:
			return "UNKNOWN_ERROR";
		}
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
