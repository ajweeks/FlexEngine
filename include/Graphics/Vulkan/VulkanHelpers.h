#pragma once

#include <string>

#include <vulkan\vulkan.h>

std::string VulkanErrorString(VkResult errorCode);

#ifndef VK_CHECK_RESULT
#define VK_CHECK_RESULT(f)																				\
{																										\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS)																				\
	{																									\
		std::cerr << "Vulkan fatal error: VkResult is \"" << VulkanErrorString(res) << "\" in " << __FILE__ << " at line " << __LINE__ << std::endl; \
		assert(res == VK_SUCCESS);																		\
	}																									\
}
#endif // VK_CHECK_RESULT

struct VertexBufferData;

struct QueueFamilyIndices
{
	int graphicsFamily = -1;
	int presentFamily = -1;

	bool isComplete()
	{
		return graphicsFamily >= 0 && presentFamily >= 0;
	}
};

struct SwapChainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

struct VulkanVertex
{
	static VkVertexInputBindingDescription GetVertexBindingDescription(VertexBufferData* vertexBufferData);
	static void GetVertexAttributeDescriptions(VertexBufferData* vertexBufferData, std::vector<VkVertexInputAttributeDescription>& vec);
};

struct UniformBuffers
{
	UniformBuffers(const VDeleter<VkDevice>& device);

	VulkanBuffer viewBuffer;
	VulkanBuffer dynamicBuffer;
};

struct UniformBufferObjectData
{
	glm::mat4 projection;
	glm::mat4 view;
	glm::vec4 camPos;
	glm::vec4 lightDir;
	glm::vec4 ambientColor;
	glm::vec4 specularColor;
	glm::int32 useDiffuseTexture;
	glm::int32 useNormalTexture;
	glm::int32 useSpecularTexture;
};

struct UniformBufferObjectDynamic
{
	struct Data 
	{
		glm::mat4 model;
		glm::mat4 modelInvTranspose;
	};

	constexpr static size_t size = sizeof(Data);
	Data* data;
};

VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback);

void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator);
