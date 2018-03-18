#pragma once
#if COMPILE_VULKAN

#include <string>
#include <vector>
#include <iostream>
#include <sstream>

#pragma warning(push, 0) // Don't generate warnings for 3rd party code    
#include <vulkan/vulkan.hpp>
#pragma warning(pop)

#include "Graphics/Renderer.hpp"
#include "VulkanBuffer.hpp"
#include "VertexBufferData.hpp"
#include "VDeleter.hpp"


namespace flex
{
	namespace vk
	{
		struct VulkanDevice;

		std::string VulkanErrorString(VkResult errorCode);

		static std::stringstream VkErrorSS;

		inline void VK_CHECK_RESULT(VkResult result);

		VkVertexInputBindingDescription GetVertexBindingDescription(u32 vertexStride);

		void GetVertexAttributeDescriptions(VertexAttributes vertexAttributes,
			std::vector<VkVertexInputAttributeDescription>& attributeDescriptions);

		// Framebuffer for offscreen rendering
		struct FrameBufferAttachment
		{
			FrameBufferAttachment(const VDeleter<VkDevice>& device);
			FrameBufferAttachment(const VDeleter<VkDevice>& device, VkFormat format);

			VDeleter<VkImage> image;
			VDeleter<VkDeviceMemory> mem;
			VDeleter<VkImageView> view;
			VkFormat format;
		};

		struct FrameBuffer
		{
			FrameBuffer(const VDeleter<VkDevice>& device);

			u32 width, height;
			VDeleter<VkFramebuffer> frameBuffer;
			std::vector<std::pair<std::string, FrameBufferAttachment>> frameBufferAttachments;
			VDeleter<VkRenderPass> renderPass;
		};

		struct VulkanQueueFamilyIndices
		{
			i32 graphicsFamily = -1;
			i32 presentFamily = -1;

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
			real* data = nullptr;
			u32 size = 0;
		};

		struct UniformBuffer
		{
			UniformBuffer(const VDeleter<VkDevice>& device);
			~UniformBuffer();

			VulkanBuffer constantBuffer;
			VulkanBuffer dynamicBuffer;
			VulkanUniformBufferObjectData constantData;
			VulkanUniformBufferObjectData dynamicData;
		};

		struct VertexIndexBufferPair
		{
			VertexIndexBufferPair(VulkanBuffer* vertexBuffer, VulkanBuffer* indexBuffer) :
				vertexBuffer(vertexBuffer),
				indexBuffer(indexBuffer)
			{}

			VulkanBuffer* vertexBuffer = nullptr;
			VulkanBuffer* indexBuffer = nullptr;
			u32 vertexCount = 0;
			u32 indexCount = 0;
			bool useStagingBuffer = true; // Set to false for vertex buffers that need to be updated very frequently (eg. ImGui vertex buffer)
		};

		struct VulkanTexture
		{
			VulkanTexture(const VDeleter<VkDevice>& device);
			void UpdateImageDescriptor();

			VDeleter<VkImage> image;
			VkImageLayout imageLayout;
			VDeleter<VkDeviceMemory> imageMemory;
			VDeleter<VkImageView> imageView;
			VDeleter<VkSampler> sampler;
			VkDescriptorImageInfo imageInfoDescriptor;
			u32 width = 0;
			u32 height = 0;
			std::string filePath = "";
			u32 mipLevels = 1;
		};

		void SetImageLayout(
			VkCommandBuffer cmdbuffer,
			VkImage image,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			const VkImageSubresourceRange& subresourceRange,
			VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

		void SetImageLayout(
			VkCommandBuffer cmdbuffer,
			VulkanTexture* texture,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			const VkImageSubresourceRange& subresourceRange,
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

		void SetImageLayout(
			VkCommandBuffer cmdbuffer,
			VulkanTexture* texture,
			VkImageAspectFlags aspectMask,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

		void CreateAttachment(
			VulkanDevice* device,
			VkFormat format,
			VkImageUsageFlagBits usage,
			u32 width,
			u32 height,
			u32 arrayLayers,
			VkImageViewType imageViewType,
			VkImageCreateFlags imageFlags,
			FrameBufferAttachment *attachment);

		VkBool32 GetSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat* depthFormat);

		struct VulkanCubemapGBuffer
		{
			VulkanCubemapGBuffer(u32 id, const char* name, VkFormat internalFormat);

			u32 id = 0;
			const char* name = "";
			VkFormat internalFormat = VK_FORMAT_UNDEFINED;
		};

		struct VulkanShader
		{
			VulkanShader(const std::string& name, const std::string& vertexShaderFilePath, const std::string& fragmentShaderFilePath, const VDeleter<VkDevice>& device);

			Renderer::Shader shader;

			UniformBuffer uniformBuffer;
		};

		struct VulkanMaterial
		{
			Renderer::Material material = {}; // More info is stored in the generic material struct

			VulkanTexture* diffuseTexture = nullptr;
			VulkanTexture* normalTexture = nullptr;
			VulkanTexture* cubemapTexture = nullptr;
			VulkanTexture* albedoTexture = nullptr;
			VulkanTexture* metallicTexture = nullptr;
			VulkanTexture* roughnessTexture = nullptr;
			VulkanTexture* aoTexture = nullptr;
			VulkanTexture* hdrEquirectangularTexture = nullptr;
			VulkanTexture* irradianceTexture = nullptr;
			VulkanTexture* brdfLUT = nullptr;
			VulkanTexture* prefilterTexture = nullptr;
			VkFramebuffer hdrCubemapFramebuffer = VK_NULL_HANDLE;

			u32 cubemapSamplerID = 0;
			std::vector<VulkanCubemapGBuffer> cubemapSamplerGBuffersIDs;
			u32 cubemapDepthSamplerID = 0;

			u32 descriptorSetLayoutIndex = 0;
		};

		struct VulkanRenderObject
		{
			VulkanRenderObject(const VDeleter<VkDevice>& device, RenderID renderID);

			VkPrimitiveTopology topology = VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			RenderID renderID = InvalidRenderID;
			MaterialID materialID = InvalidMaterialID;

			bool visible = true;
			bool visibleInSceneExplorer = true;

			std::string name = "";
			std::string materialName = "";
			Transform* transform = nullptr;

			u32 VAO = 0;
			u32 VBO = 0;
			u32 IBO = 0;

			VertexBufferData* vertexBufferData = nullptr;
			u32 vertexOffset = 0;

			bool indexed = false;
			std::vector<u32>* indices = nullptr;
			u32 indexOffset = 0;

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

			VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
			// TODO: Rename to enableBackfaceCulling
			bool enableCulling = true;

			VDeleter<VkPipelineLayout> pipelineLayout;
			VDeleter<VkPipeline> graphicsPipeline;
		};

		struct GraphicsPipelineCreateInfo
		{
			ShaderID shaderID = InvalidShaderID;
			VertexAttributes vertexAttributes = 0;

			VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
			// TODO: Rename to enableBackfaceCulling
			bool enableCulling = true;

			VkRenderPass renderPass;
			u32 subpass = 0;

			VkPushConstantRange* pushConstants = nullptr;
			u32 pushConstantRangeCount = 0;

			u32 descriptorSetLayoutIndex = 0;

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
			VkDescriptorSet* descriptorSet = nullptr;
			VkDescriptorSetLayout* descriptorSetLayout = nullptr;
			ShaderID shaderID = InvalidShaderID;
			UniformBuffer* uniformBuffer = nullptr;

			VulkanTexture* diffuseTexture = nullptr;
			VulkanTexture* normalTexture = nullptr;
			VulkanTexture* cubemapTexture = nullptr;
			VulkanTexture* albedoTexture = nullptr;
			VulkanTexture* metallicTexture = nullptr;
			VulkanTexture* roughnessTexture = nullptr;
			VulkanTexture* hdrEquirectangularTexture = nullptr;
			VulkanTexture* aoTexture = nullptr;
			VulkanTexture* irradianceTexture = nullptr;
			VulkanTexture* brdfLUT = nullptr;
			VulkanTexture* prefilterTexture = nullptr;

			std::vector<std::pair<std::string, VkImageView*>> frameBufferViews; // Name of frame buffer paired with view i32o frame buffer
		};

		struct ImGui_PushConstBlock
		{
			glm::vec2 scale;
			glm::vec2 translate;
		};

		typedef std::vector<VulkanRenderObject*>::iterator RenderObjectIter;

		VkPrimitiveTopology TopologyModeToVkPrimitiveTopology(Renderer::TopologyMode mode);
		VkCullModeFlagBits CullFaceToVkCullMode(Renderer::CullFace cullFace);

		VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
			const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback);

		void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator);
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN