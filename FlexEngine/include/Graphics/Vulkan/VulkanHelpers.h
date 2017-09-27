#pragma once

#include <string>
#include <vector>
#include <iostream>

#include <vulkan/vulkan.h>

#include "Graphics/Renderer.h"
#include "VulkanBuffer.h"
#include "VertexBufferData.h"
#include "VDeleter.h"

namespace flex
{
	namespace vk
	{
		struct VulkanDevice;

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

		VkVertexInputBindingDescription GetVertexBindingDescription(glm::uint vertexStride);

		void GetVertexAttributeDescriptions(VertexAttributes vertexAttributes,
			std::vector<VkVertexInputAttributeDescription>& attributeDescriptions);

		// Framebuffer for offscreen rendering
		struct FrameBufferAttachment
		{
			FrameBufferAttachment(const VDeleter<VkDevice>& device);

			VDeleter<VkImage> image;
			VDeleter<VkDeviceMemory> mem;
			VDeleter<VkImageView> view;
			VkFormat format;
		};

		struct FrameBuffer
		{
			FrameBuffer(const VDeleter<VkDevice>& device);

			uint32_t width, height;
			VkFramebuffer frameBuffer;
			FrameBufferAttachment position, normal, albedo;
			VkRenderPass renderPass;
		};

		struct VulkanQueueFamilyIndices
		{
			int graphicsFamily = -1;
			int presentFamily = -1;

			bool IsComplete()
			{
				return graphicsFamily >= 0 && presentFamily >= 0;
			}
		};

		struct VulkanSwapChainSupportDetails
		{
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR> presentModes;
		};

		struct VulkanUniformBufferObjectData
		{
			Renderer::Uniform::Type elements;
			float* data = nullptr;
			glm::uint size;
		};

		struct UniformBuffer
		{
			UniformBuffer(const VDeleter<VkDevice>& device);

			Buffer constantBuffer;
			Buffer dynamicBuffer;
			VulkanUniformBufferObjectData constantData;
			VulkanUniformBufferObjectData dynamicData;
		};

		struct VertexIndexBufferPair
		{
			Buffer* vertexBuffer = nullptr;
			Buffer* indexBuffer = nullptr;
			glm::uint vertexCount;
			glm::uint indexCount;
			bool useStagingBuffer = true; // Set to false for vertex buffers that need to be updated very frequently (eg. ImGui vertex buffer)
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
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			VkImageSubresourceRange subresourceRange,
			VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

		void SetImageLayout(
			VkCommandBuffer cmdbuffer,
			VkImage image,
			VkImageAspectFlags aspectMask,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

		void CreateAttachment(
			VulkanDevice* device,
			VkFormat format,
			VkImageUsageFlagBits usage,
			glm::uint width,
			glm::uint height,
			FrameBufferAttachment *attachment);

		VkBool32 GetSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat* depthFormat);

		struct Material
		{
			std::string name;

			glm::uint shaderIndex;

			struct UniformIDs
			{
				int modelID;
				int modelInvTranspose;
				int modelViewProjection;
				int camPos;
				int viewDir;
				int lightDir;
				int ambientColor;
				int specularColor;
				int useDiffuseTexture;
				int useNormalTexture;
				int useSpecularTexture;
				int useCubemapTexture;
			};
			UniformIDs uniformIDs;

			bool useDiffuseTexture = false;
			std::string diffuseTexturePath;
			VulkanTexture* diffuseTexture = nullptr;

			bool useNormalTexture = false;
			std::string normalTexturePath;
			VulkanTexture* normalTexture = nullptr;

			bool useSpecularTexture = false;
			std::string specularTexturePath;
			VulkanTexture* specularTexture = nullptr;

			std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT
			bool useCubemapTexture = false;
			VulkanTexture* cubemapTexture = nullptr;

			glm::uint descriptorSetLayoutIndex;
		};

		struct RenderObject
		{
			RenderObject(const VDeleter<VkDevice>& device, RenderID renderID);

			VkPrimitiveTopology topology = VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			RenderID renderID;
			MaterialID materialID;

			Renderer::RenderObjectInfo info;

			glm::uint VAO;
			glm::uint VBO;
			glm::uint IBO;

			VertexBufferData* vertexBufferData = nullptr;
			glm::uint vertexOffset = 0;

			bool indexed = false;
			std::vector<glm::uint>* indices = nullptr;
			glm::uint indexOffset = 0;

			VkDescriptorSet descriptorSet;

			VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;

			VDeleter<VkPipelineLayout> pipelineLayout;
			VDeleter<VkPipeline> graphicsPipeline;
		};

		struct GraphicsPipelineCreateInfo
		{
			glm::uint shaderIndex;
			VertexAttributes vertexAttributes;

			VkPrimitiveTopology topology;
			VkCullModeFlags cullMode;

			VkRenderPass renderPass;
			glm::uint subpass = 0;

			VkPushConstantRange* pushConstants = nullptr;
			glm::uint pushConstantRangeCount = 0;

			glm::uint descriptorSetLayoutIndex;

			bool setDynamicStates = false;
			bool enabledColorBlending = false;

			VkBool32 depthTestEnable = VK_TRUE;
			VkBool32 depthWriteEnable = VK_TRUE;
			VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			VkBool32 stencilTestEnable = VK_FALSE;

			// Out variables
			VkPipelineCache* pipelineCache = nullptr;
			VkPipelineLayout* pipelineLayout = nullptr;
			VkPipeline* grahpicsPipeline = nullptr;
		};

		struct DescriptorSetCreateInfo
		{
			glm::uint descriptorSetLayoutIndex;
			glm::uint uniformBufferIndex;
			VkDescriptorSet* descriptorSet;

			VulkanTexture* diffuseTexture = nullptr;
			VulkanTexture* normalTexture = nullptr;
			VulkanTexture* specularTexture = nullptr;
			VulkanTexture* cubemapTexture = nullptr;

			VkImageView* positionFrameBufferView = nullptr;
			VkImageView* normalFrameBufferView = nullptr;
			VkImageView* albedoFrameBufferView = nullptr;
		};

		struct PushConstBlock
		{
			glm::vec2 scale;
			glm::vec2 translate;
		};

		typedef std::vector<RenderObject*>::iterator RenderObjectIter;

		VkPrimitiveTopology TopologyModeToVkPrimitiveTopology(Renderer::TopologyMode mode);
		VkCullModeFlagBits CullFaceToVkCullMode(Renderer::CullFace cullFace);

		VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
			const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback);

		void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator);
	} // namespace vk
} // namespace flex