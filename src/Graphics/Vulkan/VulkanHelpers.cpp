
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

void VulkanVertex::GetVertexAttributeDescriptions(
	VertexBufferData* vertexBufferData, 
	std::vector<VkVertexInputAttributeDescription>& attributeDescriptions)
{
	attributeDescriptions.clear();

	uint32_t offset = 0;
	uint32_t location = 0;

	if (vertexBufferData->HasAttribute(VertexBufferData::VertexAttribute::POSITION))
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

	if (vertexBufferData->HasAttribute(VertexBufferData::VertexAttribute::COLOR))
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

	if (vertexBufferData->HasAttribute(VertexBufferData::VertexAttribute::TANGENT))
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

	if (vertexBufferData->HasAttribute(VertexBufferData::VertexAttribute::BITANGENT))
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

	if (vertexBufferData->HasAttribute(VertexBufferData::VertexAttribute::NORMAL))
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

	if (vertexBufferData->HasAttribute(VertexBufferData::VertexAttribute::TEXCOORD))
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
}

bool Uniform::HasUniform(Uniform::Type elements, Uniform::Type uniform)
{
	return (elements & (glm::uint)uniform);
}

glm::uint Uniform::CalculateSize(Type elements)
{
	glm::uint size = 0;

	if (HasUniform(elements, Uniform::Type::PROJECTION_MAT4)) size += sizeof(glm::mat4);
	if (HasUniform(elements, Uniform::Type::VIEW_MAT4)) size += sizeof(glm::mat4);
	if (HasUniform(elements, Uniform::Type::VIEW_INV_MAT4)) size += sizeof(glm::mat4);
	if (HasUniform(elements, Uniform::Type::VIEW_PROJECTION_MAT4)) size += sizeof(glm::mat4);
	if (HasUniform(elements, Uniform::Type::MODEL_MAT4)) size += sizeof(glm::mat4);
	if (HasUniform(elements, Uniform::Type::MODEL_INV_TRANSPOSE_MAT4)) size += sizeof(glm::mat4);
	if (HasUniform(elements, Uniform::Type::MODEL_VIEW_PROJECTION_MAT4)) size += sizeof(glm::mat4);
	if (HasUniform(elements, Uniform::Type::CAM_POS_VEC4)) size += sizeof(glm::vec4);
	if (HasUniform(elements, Uniform::Type::VIEW_DIR_VEC4)) size += sizeof(glm::vec4);
	if (HasUniform(elements, Uniform::Type::LIGHT_DIR_VEC4)) size += sizeof(glm::vec4);
	if (HasUniform(elements, Uniform::Type::AMBIENT_COLOR_VEC4)) size += sizeof(glm::vec4);
	if (HasUniform(elements, Uniform::Type::SPECULAR_COLOR_VEC4)) size += sizeof(glm::vec4);
	if (HasUniform(elements, Uniform::Type::USE_DIFFUSE_TEXTURE_INT)) size += sizeof(glm::int32);
	if (HasUniform(elements, Uniform::Type::USE_NORMAL_TEXTURE_INT)) size += sizeof(glm::int32);
	if (HasUniform(elements, Uniform::Type::USE_SPECULAR_TEXTURE_INT)) size += sizeof(glm::int32);

	return size;
}

UniformBuffer::UniformBuffer(const VDeleter<VkDevice>& device) :
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
