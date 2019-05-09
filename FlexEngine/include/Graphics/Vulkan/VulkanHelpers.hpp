#pragma once
#if COMPILE_VULKAN

IGNORE_WARNINGS_PUSH
#include <vulkan/vulkan.hpp>
IGNORE_WARNINGS_POP

#include "Graphics/RendererTypes.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "VDeleter.hpp"
#include "VulkanBuffer.hpp"

namespace flex
{
	enum class ImageFormat;

	namespace vk
	{
		struct VulkanDevice;

		std::string VulkanErrorString(VkResult errorCode);

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
			VkFormat format = VK_FORMAT_UNDEFINED;
		};

		struct FrameBuffer
		{
			FrameBuffer(const VDeleter<VkDevice>& device);

			u32 width = 0;
			u32 height = 0;
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
			u32 fullDynamicBufferSize = 0;
		};

		struct VertexIndexBufferPair
		{
			VertexIndexBufferPair(VulkanBuffer* vertexBuffer, VulkanBuffer* indexBuffer) :
				vertexBuffer(vertexBuffer),
				indexBuffer(indexBuffer)
			{}

			void Destroy();
			void Empty();

			VulkanBuffer* vertexBuffer = nullptr;
			VulkanBuffer* indexBuffer = nullptr;
			u32 vertexCount = 0;
			u32 indexCount = 0;
			bool useStagingBuffer = true; // Set to false for vertex buffers that need to be updated very frequently (e.g. ImGui vertex buffer)
		};

		struct VulkanTexture
		{
			VulkanTexture(VulkanDevice* device, VkQueue graphicsQueue, const std::string& name, u32 width, u32 height, u32 channelCount);
			VulkanTexture(VulkanDevice* device, VkQueue graphicsQueue, const std::string& relativeFilePath, u32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR);
			VulkanTexture(VulkanDevice* device, VkQueue graphicsQueue, const std::array<std::string, 6>& relativeCubemapFilePaths, u32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR);

			struct ImageCreateInfo
			{
				VkImage* image = nullptr;
				VkDeviceMemory* imageMemory = nullptr;

				bool isHDR = false;
				u32 width = 0;
				u32 height = 0;
				VkFormat format = VK_FORMAT_UNDEFINED;
				VkImageUsageFlags usage = 0;
				VkMemoryPropertyFlags properties = 0;
				u32 mipLevels = 1;

				VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				VkImageType imageType = VK_IMAGE_TYPE_2D;
				VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
				u32 arrayLayers = 1;
				VkImageCreateFlags flags = 0;
			};

			struct ImageViewCreateInfo
			{
				VkImageView* imageView = nullptr;
				VkImage* image = nullptr;

				VkFormat format = VK_FORMAT_UNDEFINED;
				u32 mipLevels = 1;
				u32 layerCount = 1;
				VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
				VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
			};

			struct SamplerCreateInfo
			{
				VkSampler* sampler = nullptr;

				real maxAnisotropy = 16.0f;
				real minLod = 0.0f;
				real maxLod = 0.0f;
				VkFilter magFilter = VK_FILTER_LINEAR;
				VkFilter minFilter = VK_FILTER_LINEAR;
				VkSamplerAddressMode samplerAddressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				VkBorderColor borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			};

			struct CubemapCreateInfo
			{
				VkImage* image = nullptr;
				VkDeviceMemory* imageMemory = nullptr;
				VkImageView* imageView = nullptr;
				VkSampler* sampler = nullptr;

				VkImageLayout imageLayoutOut = VK_IMAGE_LAYOUT_UNDEFINED; // Will be set upon successful creation

				VkFormat format = VK_FORMAT_UNDEFINED;
				u32 width = 0;
				u32 height = 0;
				u32 channels = 0;
				u32 totalSize = 0;
				u32 mipLevels = 1;
				bool generateMipMaps = false;
				bool enableTrilinearFiltering = true;

				// Leave following field empty to generate uninitialized cubemap
				std::array<std::string, 6> filePaths;
			};

			// Static, globally usable functions

			/* Returns the size of the generated image */
			static VkDeviceSize CreateImage(VulkanDevice* device, VkQueue graphicsQueue, ImageCreateInfo& createInfo);

			static void CreateImageView(VulkanDevice* device, ImageViewCreateInfo& createInfo);

			static void CreateSampler(VulkanDevice* device, SamplerCreateInfo& createInfo);

			// Expects *texture == nullptr
			static VkDeviceSize CreateCubemap(VulkanDevice* device, VkQueue graphicsQueue, CubemapCreateInfo& createInfo);

			// Non-static member functions
			void Create(ImageCreateInfo& imageCreateInfo, ImageViewCreateInfo& imageViewCreateInfo, SamplerCreateInfo& samplerCreateInfo);

			u32 CreateFromMemory(void* buffer, u32 bufferSize, VkFormat inFormat, i32 inMipLevels, VkFilter filter = VK_FILTER_LINEAR);

			void TransitionToLayout(VkImageLayout newLayout);
			void CopyFromBuffer(VkBuffer buffer, u32 inWidth, u32 inHeight);

			void Destroy();

			bool SaveToFile(const std::string& absoluteFilePath, ImageFormat saveFormat);

			void Build(void* data = nullptr);

			/*
			 * Creates image, image view, and sampler based on the texture at filePath
			 * Returns size of image in bytes
			 */
			VkDeviceSize CreateFromFile(VkFormat inFormat);

			/*
			 * Creates image, image view, and sampler
			 * Returns the size of the image
			*/
			VkDeviceSize CreateEmpty(VkFormat inFormat, u32 inMipLevels = 1, VkImageUsageFlags inUsage = VK_IMAGE_USAGE_SAMPLED_BIT);

			/*
			 * Creates an empty cubemap and returns the size of the generated image
			 * Returns the size of the image
			*/
			VkDeviceSize CreateCubemapEmpty(VkFormat inFormat, u32 inMipLevels, bool enableTrilinearFiltering);

			/*
			 * Creates a cubemap from the given 6 textures
			 * Returns the size of the image
			 */
			VkDeviceSize CreateCubemapFromTextures(VkFormat inFormat, const std::array<std::string, 6>& filePaths, bool enableTrilinearFiltering);

			void UpdateImageDescriptor();

			std::string GetRelativeFilePath() const;
			std::string GetName() const;
			void Reload();

			VkFormat CalculateFormat();

			u32 width = 0;
			u32 height = 0;
			u32 channelCount = 0;
			std::string name;
			std::string relativeFilePath;
			std::array<std::string, 6> relativeCubemapFilePaths;
			u32 mipLevels = 1;
			bool bFlipVertically = false;
			bool bGenerateMipMaps = false;
			bool bHDR = false;
			bool bSamplerClampToBorder = false;

			VDeleter<VkImage> image;
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkFormat imageFormat = VK_FORMAT_UNDEFINED;
			VDeleter<VkDeviceMemory> imageMemory;
			VDeleter<VkImageView> imageView;
			VDeleter<VkSampler> sampler;
			VkDescriptorImageInfo imageInfoDescriptor;

		private:
			VulkanDevice* m_VulkanDevice = nullptr;
			VkQueue m_GraphicsQueue = VK_NULL_HANDLE;

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

		void InsertImageMemoryBarrier(
			VkCommandBuffer cmdbuffer,
			VkImage image,
			VkAccessFlags srcAccessMask,
			VkAccessFlags dstAccessMask,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask,
			VkImageSubresourceRange subresourceRange);

		void CreateAttachment(
			VulkanDevice* device,
			VkFormat format,
			VkImageUsageFlags usage,
			u32 width,
			u32 height,
			u32 arrayLayers,
			VkImageViewType imageViewType,
			VkImageCreateFlags imageFlags,
			FrameBufferAttachment *attachment);

		template<class T>
		void CopyPixels(const T* srcData, T* dstData, u32 dstOffset, u32 width, u32 height, u32 channelCount, u32 pitch, bool bColorSwizzle);

		VkBool32 GetSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat* depthFormat);

		VkFormat FindSupportedFormat(VulkanDevice* device, const std::vector<VkFormat>& candidates, VkImageTiling tiling,
			VkFormatFeatureFlags features);
		bool HasStencilComponent(VkFormat format);
		u32 FindMemoryType(VulkanDevice* device, u32 typeFilter, VkMemoryPropertyFlags properties);
		void TransitionImageLayout(VulkanDevice* device, VkQueue graphicsQueue, VkImage image, VkFormat format, VkImageLayout oldLayout,
			VkImageLayout newLayout, u32 mipLevels, VkCommandBuffer optCmdBuf = VK_NULL_HANDLE, bool bIsDepthTexture = false);

		void CopyImage(VulkanDevice* device, VkQueue graphicsQueue, VkImage srcImage, VkImage dstImage, u32 width, u32 height,
			VkCommandBuffer optCmdBuf = VK_NULL_HANDLE, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);
		void CopyBufferToImage(VulkanDevice* device, VkQueue graphicsQueue, VkBuffer buffer, VkImage image, u32 width, u32 height);
		VkResult CreateAndAllocateBuffer(VulkanDevice* device, VkDeviceSize size, VkBufferUsageFlags usage,
			VkMemoryPropertyFlags properties, VulkanBuffer* buffer);
		void CopyBuffer(VulkanDevice* device, VkQueue graphicsQueue, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
			VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);

		VkCommandBuffer BeginSingleTimeCommands(VulkanDevice* device);
		void EndSingleTimeCommands(VulkanDevice* device, VkQueue graphicsQueue, VkCommandBuffer commandBuffer);

		VulkanQueueFamilyIndices FindQueueFamilies(VkSurfaceKHR surface, VkPhysicalDevice device);

		struct VulkanCubemapGBuffer
		{
			VulkanCubemapGBuffer(u32 id, const char* name, VkFormat internalFormat);

			u32 id = 0;
			const char* name = "";
			VkFormat internalFormat = VK_FORMAT_UNDEFINED;
		};

		struct VulkanShader
		{
			VulkanShader(const VDeleter<VkDevice>& device, const std::string& name,
				const std::string& vertexShaderFilePath,
				const std::string& fragmentShaderFilePath,
				const std::string& geomShaderFilePath = "");

			Shader shader;

			VkRenderPass renderPass = VK_NULL_HANDLE;
			UniformBuffer uniformBuffer;
			bool bDynamic = false;
			u32 dynamicVertexBufferSize = 0;
		};

#ifdef DEBUG
		struct AsyncVulkanShaderCompiler
		{
			AsyncVulkanShaderCompiler();
			AsyncVulkanShaderCompiler(bool bForceRecompile);

			// Returns true once task is complete
			bool TickStatus();

			std::thread taskThread;
			std::atomic<bool> is_done = false;

			sec startTime = 0.0f;
			sec lastTime = 0.0f;
			sec totalSecWaiting = 0.0f;
			sec secBetweenStatusChecks = 0.05f;
			sec secSinceStatusCheck = 0.0f;

			bool bSuccess = false;
			bool bComplete = false;

		private:
			i64 CalculteChecksum(const std::string& directory);

			std::string m_ChecksumFilePath;
			i64 m_ShaderCodeChecksum = 0;
		};
#endif // DEBUG

		struct VulkanMaterial
		{
			Material material = {}; // More info is stored in the generic material struct

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
			VulkanTexture* noiseTexture = nullptr;
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

			GameObject* gameObject = nullptr;

			std::string materialName = "";

			u32 VAO = 0;
			u32 VBO = 0;
			u32 IBO = 0;

			VertexBufferData* vertexBufferData = nullptr;
			u32 vertexOffset = 0;

			bool bIndexed = false;
			std::vector<u32>* indices = nullptr;
			u32 indexOffset = 0;

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

			VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
			VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

			bool bEditorObject = false;
			bool bSetDynamicStates = false;

			u32 dynamicUBOOffset = 0;

			VDeleter<VkPipelineLayout> pipelineLayout;
			VDeleter<VkPipeline> graphicsPipeline;
		};

		struct GraphicsPipelineCreateInfo
		{
			ShaderID shaderID = InvalidShaderID;
			VertexAttributes vertexAttributes = 0;

			VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;

			VkRenderPass renderPass;
			u32 subpass = 0;

			VkPushConstantRange* pushConstants = nullptr;
			u32 pushConstantRangeCount = 0;

			u32 descriptorSetLayoutIndex = 0;

			bool bSetDynamicStates = false;
			bool bEnableColorBlending = false;
			bool bEnableAdditiveColorBlending = false;

			VkBool32 depthWriteEnable = VK_TRUE;
			VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
			VkBool32 stencilTestEnable = VK_FALSE;

			VkSpecializationInfo* fragSpecializationInfo = nullptr;

			// Out variables
			VkPipelineCache* pipelineCache = nullptr;
			VkPipelineLayout* pipelineLayout = nullptr;
			VkPipeline* graphicsPipeline = nullptr;
		};

		struct DescriptorSetCreateInfo
		{
			VkDescriptorSet* descriptorSet = nullptr;
			VkDescriptorSetLayout* descriptorSetLayout = nullptr;
			ShaderID shaderID = InvalidShaderID;
			UniformBuffer* uniformBuffer = nullptr;

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
			VulkanTexture* noiseTexture = nullptr;

			VkImageView ssaoNormalImageView = VK_NULL_HANDLE;
			VkSampler ssaoNormalSampler = VK_NULL_HANDLE;

			VkImageView ssaoImageView = VK_NULL_HANDLE;
			VkSampler ssaoSampler = VK_NULL_HANDLE;

			VkImageView ssaoFinalImageView = VK_NULL_HANDLE;
			VkSampler ssaoFinalSampler = VK_NULL_HANDLE;

			bool bDepthSampler = false;

			std::vector<std::pair<u32, VkImageView*>> frameBufferViews; // Name of frame buffer paired with view i32o frame buffer
		};

		struct ImGui_PushConstBlock
		{
			glm::vec2 scale;
			glm::vec2 translate;
		};

		typedef std::vector<VulkanRenderObject*>::iterator RenderObjectIter;

		VkPrimitiveTopology TopologyModeToVkPrimitiveTopology(TopologyMode mode);
		VkCullModeFlagBits CullFaceToVkCullMode(CullFace cullFace);

		TopologyMode VkPrimitiveTopologyToTopologyMode(VkPrimitiveTopology primitiveTopology);
		CullFace VkCullModeToCullFace(VkCullModeFlags cullMode);

		VkCompareOp DepthTestFuncToVkCompareOp(DepthTestFunc func);

		std::string DeviceTypeToString(VkPhysicalDeviceType type);

		VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* createInfo,
			const VkAllocationCallbacks* allocator, VkDebugReportCallbackEXT* callback);

		void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator);
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN