#pragma once

#include <string>
#include <vector>

#include <vulkan\vulkan.h>

#include "ShaderUtils.h"
#include "VulkanBuffer.h"
#include "VertexBufferData.h"
#include "VDeleter.h"

namespace flex
{
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

	namespace Vulkan
	{
		VkVertexInputBindingDescription GetVertexBindingDescription(VertexBufferData* vertexBufferData);

		void GetVertexAttributeDescriptions(VertexBufferData* vertexBufferData,
			std::vector<VkVertexInputAttributeDescription>& attributeDescriptions);

	} // namespace Vulkan

	struct QueueFamilyIndices
	{
		int graphicsFamily = -1;
		int presentFamily = -1;

		bool IsComplete()
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

	struct UniformBufferObjectData
	{
		Uniform::Type elements;
		float* data = nullptr;
		glm::uint size;
	};

	struct UniformBuffer
	{
		UniformBuffer(const VDeleter<VkDevice>& device);

		VulkanBuffer constantBuffer;
		VulkanBuffer dynamicBuffer;
		UniformBufferObjectData constantData;
		UniformBufferObjectData dynamicData;
	};

	struct VulkanTexture
	{
		VulkanTexture(const VDeleter<VkDevice>& device);

		VDeleter<VkImage> image;
		VkImageLayout imageLayout;
		VDeleter<VkDeviceMemory> imageMemory;
		VDeleter<VkImageView> imageView;
		VDeleter<VkSampler> sampler;
		glm::uint width;
		glm::uint height;
		std::string filePath;
	};

	void SetImageLayout(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkImageAspectFlags aspectMask,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkImageSubresourceRange subresourceRange,
		VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

	struct RenderObject
	{
		RenderObject(const VDeleter<VkDevice>& device, RenderID renderID);

		VkPrimitiveTopology topology = VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		RenderID renderID;

		glm::uint VAO;
		glm::uint VBO;
		glm::uint IBO;

		VertexBufferData* vertexBufferData = nullptr;
		glm::uint vertexOffset = 0;

		bool indexed = false;
		std::vector<glm::uint>* indices = nullptr;
		glm::uint indexOffset = 0;

		glm::uint shaderIndex;

		std::string diffuseTexturePath;
		VulkanTexture* diffuseTexture = nullptr;

		std::string normalTexturePath;
		VulkanTexture* normalTexture = nullptr;

		std::string specularTexturePath;
		VulkanTexture* specularTexture = nullptr;

		bool useCubemapTexture;

		glm::uint descriptorSetLayoutIndex;
		VkDescriptorSet descriptorSet;

		VkCullModeFlagBits cullMode = VK_CULL_MODE_BACK_BIT;

		VDeleter<VkPipelineLayout> pipelineLayout;
		VDeleter<VkPipeline> graphicsPipeline;
	};

	typedef std::vector<RenderObject*>::iterator RenderObjectIter;

	VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback);

	void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator);
} // namespace flex