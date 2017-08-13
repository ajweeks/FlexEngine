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
	static void GetVertexAttributeDescriptions(VertexBufferData* vertexBufferData, 
		std::vector<VkVertexInputAttributeDescription>& vec, glm::uint type);
};

struct UniformBuffers_Simple
{
	UniformBuffers_Simple(const VDeleter<VkDevice>& device);

	VulkanBuffer viewBuffer;
	VulkanBuffer dynamicBuffer;
};

struct UniformBuffers_Color
{
	UniformBuffers_Color(const VDeleter<VkDevice>& device);

	VulkanBuffer viewBuffer;
	VulkanBuffer dynamicBuffer;
};

struct UniformBufferObjectData_Simple
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

struct UniformBufferObjectData_Color
{
	glm::mat4 viewProjection;
};

struct UniformBufferObjectDataDynamic_Simple
{
	struct Data
	{
		glm::mat4 model;
		glm::mat4 modelInvTranspose;
	};

	constexpr static size_t size = sizeof(Data);
	Data* data;
};

struct UniformBufferObjectDataDynamic_Color
{
	struct Data
	{
		glm::mat4 model;
	};

	constexpr static size_t size = sizeof(Data);
	Data* data;
};

struct VulkanTexture
{
	VulkanTexture(const VDeleter<VkDevice>& device);

	VDeleter<VkImage> Image;
	VDeleter<VkDeviceMemory> ImageMemory;
	VDeleter<VkImageView> ImageView;
	VDeleter<VkSampler> Sampler;
};

struct RenderObject
{
	RenderObject(const VDeleter<VkDevice>& device);

	VkPrimitiveTopology topology = VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	glm::uint renderID;

	glm::uint VAO;
	glm::uint VBO;
	glm::uint IBO;

	VertexBufferData* vertexBufferData = nullptr;
	glm::uint vertexOffset = 0;

	bool indexed = false;
	std::vector<glm::uint>* indices = nullptr;
	glm::uint indexOffset = 0;

	std::string fragShaderFilePath;
	std::string vertShaderFilePath;

	glm::uint descriptorSetLayoutIndex;

	glm::uint shaderIndex;

	VulkanTexture* m_DiffuseTexture = nullptr;
	VulkanTexture* m_NormalTexture = nullptr;
	VulkanTexture* m_SpecularTexture = nullptr;

	VkDescriptorSet m_DescriptorSet;

	VDeleter<VkPipelineLayout> pipelineLayout;
	VDeleter<VkPipeline> graphicsPipeline;
};

typedef std::vector<RenderObject*>::iterator RenderObjectIter;


VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback);

void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator);
