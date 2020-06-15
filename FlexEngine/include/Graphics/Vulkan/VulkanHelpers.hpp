#pragma once
#if COMPILE_VULKAN

IGNORE_WARNINGS_PUSH
#include "volk/volk.h"

#if COMPILE_SHADER_COMPILER
#include "shaderc/shaderc.h" // For shaderc_shader_kind
#endif
IGNORE_WARNINGS_POP

#include "Graphics/RendererTypes.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "VDeleter.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanRenderPass.hpp"

namespace flex
{
	enum class ImageFormat;
	class ParticleSystem;

	namespace vk
	{
		struct VulkanDevice;

		std::string VulkanErrorString(VkResult errorCode);

		void VK_CHECK_RESULT(VkResult result);

		void GetVertexAttributeDescriptions(VertexAttributes vertexAttributes,
			std::vector<VkVertexInputAttributeDescription>& attributeDescriptions);

		// Framebuffer for offscreen rendering
		struct FrameBufferAttachment
		{
			struct CreateInfo
			{
				bool bIsDepth = false;
				bool bIsSampled = false;
				bool bIsCubemap = false;
				bool bIsTransferedSrc = false;
				bool bIsTransferedDst = false;
				u32 width = 0;
				u32 height = 0;
				VkFormat format = VK_FORMAT_UNDEFINED;
				VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			};

			FrameBufferAttachment(VulkanDevice* device, const CreateInfo& createInfo);
			~FrameBufferAttachment();

			void CreateImage(u32 inWidth = 0, u32 inHeight = 0, const char* optDBGName = nullptr);
			void CreateImageView(const char* optDBGName = nullptr);

			void TransitionToLayout(VkImageLayout newLayout, VkQueue graphicsQueue, VkCommandBuffer optCmdBuf = VK_NULL_HANDLE);

			VulkanDevice* device = nullptr;

			FrameBufferAttachmentID ID;

			// TODO: Store data in VulkanTexture
			VkImage image = VK_NULL_HANDLE;
			VkDeviceMemory mem = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			u32 width = 0;
			u32 height = 0;
			bool bIsDepth = false;
			bool bIsSampled = false;
			bool bIsCubemap = false;
			bool bIsTransferedSrc = false;
			bool bIsTransferedDst = false;
			bool bOwnsResources = true;
			VkFormat format = VK_FORMAT_UNDEFINED;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		};

		struct FrameBuffer
		{
			FrameBuffer(VulkanDevice* device);
			~FrameBuffer();

			void Create(VkFramebufferCreateInfo* createInfo, VulkanRenderPass* inRenderPass, const char* debugName);

			VkFramebuffer* Replace();

			operator VkFramebuffer()
			{
				return frameBuffer;
			}

			VulkanDevice* m_VulkanDevice = nullptr;
			u32 width = 0;
			u32 height = 0;
			VkFramebuffer frameBuffer = VK_NULL_HANDLE;
			std::vector<std::pair<std::string, FrameBufferAttachment>> frameBufferAttachments;
			VulkanRenderPass* renderPass = nullptr;
		};

		struct Cascade
		{
			Cascade(VulkanDevice* device);
			~Cascade();

			FrameBuffer frameBuffer;
			FrameBufferAttachment* attachment = nullptr;
			VDeleter<VkImageView> imageView;
			VkDescriptorSet descSet;
		};

		struct VulkanQueueFamilyIndices
		{
			i32 graphicsFamily = -1;
			i32 computeFamily = -1;
			i32 presentFamily = -1;

			bool IsComplete()
			{
				return graphicsFamily >= 0 && presentFamily >= 0 && computeFamily >= 0;
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

		enum class UniformBufferType
		{
			STATIC,
			DYNAMIC,
			PARTICLE_DATA,

			_NONE
		};

		struct UniformBuffer
		{
			UniformBuffer(VulkanDevice* device, UniformBufferType type);
			~UniformBuffer();

			VulkanBuffer buffer;
			VulkanUniformBufferObjectData data;
			u32 fullDynamicBufferSize = 0;

			UniformBufferType type = UniformBufferType::_NONE;
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
			bool bUseStagingBuffer = true; // Set to false for vertex buffers that need to be updated very frequently (e.g. ImGui vertex buffer)
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

				bool bHDR = false;
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

				const char* DBG_Name = nullptr;
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

				const char* DBG_Name = nullptr;
			};

			struct SamplerCreateInfo
			{
				VkSampler* sampler = nullptr;

				real maxAnisotropy = 16.0f;
				real minLod = 0.0f;
				real maxLod = 1.0f;
				VkFilter magFilter = VK_FILTER_LINEAR;
				VkFilter minFilter = VK_FILTER_LINEAR;
				VkSamplerAddressMode samplerAddressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				VkBorderColor borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

				const char* DBG_Name = nullptr;
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
				bool bGenerateMipMaps = false;
				bool bEnableTrilinearFiltering = true;

				// Leave following field empty to generate uninitialized cubemap
				std::array<std::string, 6> filePaths;

				const char* DBG_Name = nullptr;
			};

			// Static, globally usable functions

			/* Returns the size of the generated image */
			static VkDeviceSize CreateImage(VulkanDevice* device, ImageCreateInfo& createInfo);

			static void CreateImageView(VulkanDevice* device, ImageViewCreateInfo& createInfo);

			static void CreateSampler(VulkanDevice* device, SamplerCreateInfo& createInfo);

			// Expects *texture == nullptr
			static VkDeviceSize CreateCubemap(VulkanDevice* device, VkQueue graphicsQueue, CubemapCreateInfo& createInfo);

			u32 CreateFromMemory(void* buffer, u32 bufferSize, VkFormat inFormat, i32 inMipLevels, VkFilter filter = VK_FILTER_LINEAR, i32 layerCount = 1);

			void TransitionToLayout(VkImageLayout newLayout, VkCommandBuffer optCommandBuffer = VK_NULL_HANDLE);
			void CopyFromBuffer(VkBuffer buffer, u32 inWidth, u32 inHeight, VkCommandBuffer optCommandBuffer = 0);

			bool SaveToFile(const std::string& absoluteFilePath, ImageFormat saveFormat);

			void Build(void* data = nullptr);

			/*
			 * Creates image, image view, and sampler based on the texture at filePath
			 * Returns size of image in bytes
			 */
			VkDeviceSize CreateFromFile(VkFormat inFormat, bool bGenerateFullMipChain = false);

			/*
			 * Creates image, image view, and sampler
			 * Returns the size of the image
			*/
			VkDeviceSize CreateEmpty(VkFormat inFormat, u32 inMipLevels = 1, VkImageUsageFlags inUsage = VK_IMAGE_USAGE_SAMPLED_BIT);

			/*
			 * Creates an empty cubemap and returns the size of the generated image
			 * Returns the size of the image
			*/
			VkDeviceSize CreateCubemapEmpty(VkFormat inFormat, u32 inMipLevels, bool bEnableTrilinearFiltering);

			/*
			 * Creates a cubemap from the given 6 textures
			 * Returns the size of the image
			 */
			VkDeviceSize CreateCubemapFromTextures(VkFormat inFormat, const std::array<std::string, 6>& filePaths, bool bEnableTrilinearFiltering);

			void GenerateMipmaps();

			std::string GetRelativeFilePath() const;
			std::string GetName() const;
			void Reload();

			VkFormat CalculateFormat();

			VDeleter<VkImage> image;
			VDeleter<VkDeviceMemory> imageMemory;
			VDeleter<VkImageView> imageView;
			// TODO: CLEANUP: Don't store sampler per texture, pool together all unique samplers in VulkanRenderer
			VDeleter<VkSampler> sampler;

			u32 width = 0;
			u32 height = 0;
			u32 channelCount = 0;
			std::string name;
			std::string relativeFilePath;
			std::string fileName;
			std::array<std::string, 6> relativeCubemapFilePaths;
			u32 mipLevels = 1;
			bool bFlipVertically = false;
			bool bGenerateMipMaps = false;
			bool bHDR = false;
			bool bIsArray = false;
			bool bSamplerClampToBorder = false;

			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkFormat imageFormat = VK_FORMAT_UNDEFINED;

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
			VkImage* image,
			VkDeviceMemory* memory,
			VkImageView* imageView,
			const char* DBG_ImageName = nullptr,
			const char* DBG_ImageViewName = nullptr);

		void CreateAttachment(VulkanDevice* device, FrameBufferAttachment* frameBufferAttachment, const char* DBG_ImageName = nullptr, const char* DBG_ImageViewName = nullptr);

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
		void CopyBufferToImage(VulkanDevice* device, VkQueue graphicsQueue, VkBuffer buffer, VkImage image, u32 width, u32 height, VkCommandBuffer optCommandBuffer = 0);
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

		struct UniformBufferList
		{
			void Add(VulkanDevice* device, UniformBufferType type);
			UniformBuffer* Get(UniformBufferType type);
			const UniformBuffer* Get(UniformBufferType type) const;
			bool Has(UniformBufferType type) const;

			std::vector<UniformBuffer> uniformBufferList;
		};

		struct VulkanShader
		{
			VulkanShader(const VDeleter<VkDevice>& device, Shader* shader);

			Shader* shader = nullptr;

			VkRenderPass renderPass = VK_NULL_HANDLE;

			VDeleter<VkShaderModule> vertShaderModule;
			VDeleter<VkShaderModule> fragShaderModule;
			VDeleter<VkShaderModule> geomShaderModule;
			VDeleter<VkShaderModule> computeShaderModule;
		};

#if COMPILE_SHADER_COMPILER
		// NOTE: Not actually async at the moment! Compiling all shaders takes less than a second my machine though, so...
		// TODO: Either rename, or make async!
		struct AsyncVulkanShaderCompiler
		{
			AsyncVulkanShaderCompiler(bool bForceRecompile);

			bool TickStatus();

			ms startTime = 0.0f;
			ms lastCompileDuration = 0.0f;

			bool bSuccess = false;
			bool bComplete = false;

		private:
			static const char* s_ChecksumFilePath;
			static std::string s_ChecksumFilePathAbs;
			static const char* s_ShaderDirectory;
			static const char* s_RecognizedShaderTypes[];

			u64 CalculteChecksum(const std::string& filePath);
			shaderc_shader_kind FilePathToShaderKind(const std::string& fileSuffix);

		};
#endif // COMPILE_SHADER_COMPILER

		template<typename T>
		struct ShaderUniformContainer
		{
			using iter = typename std::vector<Pair<u64, T>>::iterator;

			void Add(u64 uniform, const T value, std::string slotName = "")
			{
				for (auto value_iter = values.begin(); value_iter != values.end(); ++value_iter)
				{
					if (value_iter->first == uniform)
					{
						value_iter->second = value;
						slotNames[value_iter - values.begin()] = slotName;
						return;
					}
				}

				values.emplace_back(uniform, value);
				slotNames.emplace_back(slotName);
			}

			u32 Count()
			{
				return (u32)values.size();
			}

			iter begin()
			{
				return values.begin();
			}

			iter end()
			{
				return values.end();
			}

			bool Contains(u64 uniform)
			{
				for (auto& pair : values)
				{
					if (pair.first == uniform)
					{
						return true;
					}
				}
				return false;
			}

			T operator[](u64 uniform)
			{
				for (auto& pair : values)
				{
					if (pair.first == uniform)
					{
						return pair.second;
					}
				}
				return nullptr;
			}

			std::vector<Pair<u64, T>> values;
			std::vector<std::string> slotNames;
		};

		struct VulkanMaterial
		{
			Material material; // More info is stored in the generic material struct

			// TODO: OPTIMIZE: MEMORY: Only store dynamic buffers here, store constant buffers in shader/globally
			UniformBufferList uniformBufferList;

			ShaderUniformContainer<VulkanTexture*> textures;
			VkFramebuffer hdrCubemapFramebuffer = VK_NULL_HANDLE;

			u32 cubemapSamplerID = 0;
			std::vector<VulkanCubemapGBuffer> cubemapSamplerGBuffersIDs;
			u32 cubemapDepthSamplerID = 0;

			// TODO: Remove, this always equals shaderID
			u32 descriptorSetLayoutIndex = 0;
		};

		struct VulkanRenderObject
		{
			VulkanRenderObject(const VDeleter<VkDevice>& device, RenderID renderID);

			VkPrimitiveTopology topology = VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			RenderID renderID = InvalidRenderID;
			MaterialID materialID = InvalidMaterialID;

			GameObject* gameObject = nullptr;

			u32 VAO = 0;
			u32 VBO = 0;
			u32 IBO = 0;

			VertexBufferData* vertexBufferData = nullptr;
			u32 vertexOffset = 0;

			bool bIndexed = false;
			std::vector<u32>* indices = nullptr;
			u32 indexOffset = 0;

			u32 shadowVertexOffset = 0;
			u32 shadowIndexOffset = 0;

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

			VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
			VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

			bool bEditorObject = false;
			bool bSetDynamicStates = false;

			u32 dynamicUBOOffset = 0;
			u32 dynamicShadowUBOOffset = 0;

			u64 dynamicVertexBufferOffset = u64_max;
			u64 dynamicIndexBufferOffset = u64_max;

			VDeleter<VkPipelineLayout> pipelineLayout;
			VDeleter<VkPipeline> graphicsPipeline;

			RenderPassType renderPassOverride = RenderPassType::_NONE;
		};

		struct GraphicsPipelineCreateInfo
		{
			ShaderID shaderID = InvalidShaderID;
			VertexAttributes vertexAttributes = 0;

			VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;

			VkRenderPass renderPass = VK_NULL_HANDLE;
			u32 subpass = 0;

			VkPushConstantRange* pushConstants = nullptr;
			u32 pushConstantRangeCount = 0;

			u32 descriptorSetLayoutIndex = 0;

			bool bSetDynamicStates = false;
			bool bEnableColorBlending = false;
			bool bEnableAdditiveColorBlending = false;

			VkBool32 depthTestEnable = VK_TRUE;
			VkBool32 depthWriteEnable = VK_TRUE;
			VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
			VkBool32 stencilTestEnable = VK_FALSE;

			VkSpecializationInfo* fragSpecializationInfo = nullptr;

			const char* DBG_Name = nullptr;

			// Out variables
			VkPipelineCache* pipelineCache = nullptr;
			VkPipelineLayout* pipelineLayout = nullptr;
			VkPipeline* graphicsPipeline = nullptr;
		};

		struct BufferDescriptorInfo
		{
			VkBuffer buffer;
			VkDeviceSize bufferSize;
			UniformBufferType type;
		};

		struct ImageDescriptorInfo
		{
			VkImageView imageView = VK_NULL_HANDLE;
			VkSampler imageSampler = VK_NULL_HANDLE;
		};

		struct DescriptorSetCreateInfo
		{
			VkDescriptorSet* descriptorSet = nullptr;
			VkDescriptorSetLayout* descriptorSetLayout = nullptr;
			ShaderID shaderID = InvalidShaderID;
			UniformBufferList* uniformBufferList = nullptr;

			ShaderUniformContainer<BufferDescriptorInfo> bufferDescriptors;
			ShaderUniformContainer<ImageDescriptorInfo> imageDescriptors;

			const char* DBG_Name = nullptr;
		};

		struct ImGui_PushConstBlock
		{
			glm::vec2 scale;
			glm::vec2 translate;
		};

		struct VulkanParticleSystem
		{
			VulkanParticleSystem(VulkanDevice* device);

			ParticleSystemID ID = InvalidParticleSystemID;
			VkDescriptorSet computeDescriptorSet = VK_NULL_HANDLE;
			VkDescriptorSet renderingDescriptorSet = VK_NULL_HANDLE;
			VDeleter<VkPipeline> graphicsPipeline;
			VDeleter<VkPipeline> computePipeline;
			ParticleSystem* system = nullptr;
		};

		enum class GPUVendor : u32
		{
			Unknown,
			ARM,
			AMD,
			Broadcom,
			Imagination,
			Intel,
			nVidia,
			Qualcomm,
			Verisilicon,
			Software,
		};

		constexpr GPUVendor GPUVendorFromPCIVendor(u32 vendorID)
		{
			return vendorID == 0x13B5 ? GPUVendor::ARM
				: vendorID == 0x1002 ? GPUVendor::AMD
				: vendorID == 0x1010 ? GPUVendor::Imagination
				: vendorID == 0x8086 ? GPUVendor::Intel
				: vendorID == 0x10DE ? GPUVendor::nVidia
				: vendorID == 0x5143 ? GPUVendor::Qualcomm
				: vendorID == 0x1AE0 ? GPUVendor::Software   // Google Swiftshader
				: vendorID == 0x1414 ? GPUVendor::Software   // Microsoft WARP
				: GPUVendor::Unknown;
		}

		VkPrimitiveTopology TopologyModeToVkPrimitiveTopology(TopologyMode mode);
		VkCullModeFlagBits CullFaceToVkCullMode(CullFace cullFace);

		TopologyMode VkPrimitiveTopologyToTopologyMode(VkPrimitiveTopology primitiveTopology);
		CullFace VkCullModeToCullFace(VkCullModeFlags cullMode);

		VkCompareOp DepthTestFuncToVkCompareOp(DepthTestFunc func);

		std::string DeviceTypeToString(VkPhysicalDeviceType type);

		VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* createInfo,
			const VkAllocationCallbacks* allocator, VkDebugReportCallbackEXT* callback);
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN