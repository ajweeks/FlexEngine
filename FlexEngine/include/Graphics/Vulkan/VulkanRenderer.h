#pragma once
#if COMPILE_VULKAN

#include <array>
#include <map>

#include "Graphics/Renderer.h"
#include "Graphics/Vulkan/VulkanHelpers.h"
#include "ShaderUtils.h"
#include "VDeleter.h"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "Window/Window.h"

namespace flex
{
	class VulkanRenderer : public Renderer
	{
	public:
		VulkanRenderer(GameContext& gameContext);
		virtual ~VulkanRenderer();

		virtual void PostInitialize() override;

		virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) override;
		virtual glm::uint InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) override;
		virtual void PostInitializeRenderObject(RenderID renderID) override;

		virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
		virtual void SetClearColor(float r, float g, float b) override;

		virtual void Update(const GameContext& gameContext) override;
		virtual void Draw(const GameContext& gameContext) override;
		virtual void ReloadShaders(GameContext& gameContext) override;

		virtual void OnWindowSize(int width, int height) override;

		virtual void SetVSyncEnabled(bool enableVSync) override;
		virtual void Clear(int flags, const GameContext& gameContext) override;
		virtual void SwapBuffers(const GameContext& gameContext) override;

		virtual void UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model) override;

		virtual int GetShaderUniformLocation(glm::uint program, const std::string& uniformName) override;
		virtual void SetUniform1f(int location, float val) override;

		virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size,
			Renderer::Type renderType, bool normalized, int stride, void* pointer) override;

		virtual void Destroy(RenderID renderID) override;

	private:
		RenderID GetFirstAvailableRenderID() const;
		void InsertNewRenderObject(RenderObject* renderObject);
		void CreateInstance(const GameContext& gameContext);
		void SetupDebugCallback();
		void CreateSurface(Window* window);
		VkPhysicalDevice PickPhysicalDevice();
		void CreateLogicalDevice(VkPhysicalDevice physicalDevice);
		void CreateSwapChain(Window* window);
		void CreateImageViews();
		void CreateRenderPass();
		void CreateDescriptorSetLayout(glm::uint shaderIndex);
		void CreateGraphicsPipeline(RenderID renderID);
		void CreateDepthResources();
		void CreateFramebuffers();

		void CreateVulkanTexture(const std::string& filePath, VulkanTexture** texture);
		void CreateVulkanCubemap(const std::array<std::string, 6>& filePaths, VulkanTexture** texture);
		void CreateTextureImage(const std::string& filePath, VulkanTexture** texture);
		void CreateTextureImageView(VulkanTexture* texture);
		void CreateTextureSampler(VulkanTexture* texture);

		void CreateVertexBuffers();
		void CreateVertexBuffer(VulkanBuffer* vertexBuffer, glm::uint shaderIndex);
		void CreateIndexBuffers();
		void CreateIndexBuffer(VulkanBuffer* indexBuffer, glm::uint shaderIndex);
		void PrepareUniformBuffers();
		void CreateDescriptorPool();
		glm::uint AllocateUniformBuffer(glm::uint dynamicDataSize, void** data);
		void PrepareUniformBuffer(VulkanBuffer* buffer, glm::uint bufferSize,
			VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits);
		void CreateDescriptorSet(RenderID renderID);
		void ReleaseUniformBuffers();

		void CreateCommandPool();
		void RebuildCommandBuffers();
		void CreateCommandBuffers();
		VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, bool begin);
		void BuildCommandBuffers();
		bool CheckCommandBuffers();
		void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free);
		void DestroyCommandBuffers();

		void CreateSemaphores();

		void CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VDeleter<VkImageView>& imageView);
		void RecreateSwapChain(Window* window);
		VkCommandBuffer BeginSingleTimeCommands();
		void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
		void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, 
			VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VDeleter<VkImage>& image, VDeleter<VkDeviceMemory>& imageMemory, 
			glm::uint arrayLayers = 1, glm::uint mipLevels = 1, VkImageCreateFlags flags = 0);
		VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
		VkFormat FindDepthFormat();
		bool HasStencilComponent(VkFormat format);
		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
		void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
		void CopyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height);
		void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
		void CreateAndAllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VulkanBuffer* buffer);
		void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
		void DrawFrame(Window* window);
		void CreateShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule);
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
		VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes);
		VkExtent2D ChooseSwapExtent(Window* window, const VkSurfaceCapabilitiesKHR& capabilities);
		SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
		bool IsDeviceSuitable(VkPhysicalDevice device);
		bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
		QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
		std::vector<const char*> GetRequiredExtensions();
		bool CheckValidationLayerSupport();

		void UpdateConstantUniformBuffers(const GameContext& gameContext);
		void UpdateUniformBufferDynamic(const GameContext& gameContext, RenderID renderID, const glm::mat4& model);

		void LoadDefaultShaderCode();

		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugReportFlagsEXT flags,
			VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix,
			const char* msg, void* userData);

		VkPrimitiveTopology TopologyModeToVkPrimitiveTopology(TopologyMode mode);
		VkCullModeFlagBits CullFaceToVkCullMode(CullFace cullFace);

		RenderObject* GetRenderObject(RenderID renderID);

		std::vector<RenderObject*> m_RenderObjects;
		std::vector<UniformBuffer> m_UniformBuffers;
		std::vector<ShaderFilePathPair> m_ShaderFilePaths;
		std::vector<ShaderCodePair> m_LoadedShaderCode;
		std::vector<Material> m_LoadedMaterials;

		bool m_VSyncEnabled;
		bool m_SwapChainNeedsRebuilding;

		const std::vector<const char*> m_ValidationLayers =
		{
			"VK_LAYER_LUNARG_standard_validation"
		};

		const std::vector<const char*> m_DeviceExtensions =
		{
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

#ifdef NDEBUG
		const bool m_EnableValidationLayers = false;
#else
		const bool m_EnableValidationLayers = true;
#endif

		VDeleter<VkInstance> m_Instance{ vkDestroyInstance };
		VDeleter<VkDebugReportCallbackEXT> m_Callback{ m_Instance, DestroyDebugReportCallbackEXT };
		VDeleter<VkSurfaceKHR> m_Surface{ m_Instance, vkDestroySurfaceKHR };

		VulkanDevice* m_VulkanDevice = nullptr;

		VkQueue m_GraphicsQueue;
		VkQueue m_PresentQueue;

		VDeleter<VkSwapchainKHR> m_SwapChain; //{ m_Device, vkDestroySwapchainKHR };
		std::vector<VkImage> m_SwapChainImages;
		VkFormat m_SwapChainImageFormat;
		VkExtent2D m_SwapChainExtent;
		std::vector<VDeleter<VkImageView>> m_SwapChainImageViews;
		std::vector<VDeleter<VkFramebuffer>> m_SwapChainFramebuffers;

		VDeleter<VkRenderPass> m_RenderPass; // { m_Device, vkDestroyRenderPass };

		std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;

		std::vector<VkCommandBuffer> m_CommandBuffers;

		VulkanTexture* m_BrickDiffuseTexture = nullptr;
		VulkanTexture* m_BrickNormalTexture = nullptr;
		VulkanTexture* m_BrickSpecularTexture = nullptr;

		VulkanTexture* m_WorkDiffuseTexture = nullptr;
		VulkanTexture* m_WorkNormalTexture = nullptr;
		VulkanTexture* m_WorkSpecularTexture = nullptr;

		VulkanTexture* m_BlankTexture = nullptr;
		VulkanTexture* m_SkyboxTexture = nullptr;

		VDeleter<VkImage> m_DepthImage; // { m_Device, vkDestroyImage };
		VDeleter<VkDeviceMemory> m_DepthImageMemory;// { m_Device, vkFreeMemory };
		VDeleter<VkImageView> m_DepthImageView;// { m_Device, vkDestroyImageView };

		VDeleter<VkDescriptorPool> m_DescriptorPool; // { m_Device, vkDestroyDescriptorPool };

		struct VertexIndexBufferPair
		{
			VulkanBuffer* vertexBuffer = nullptr;
			VulkanBuffer* indexBuffer = nullptr;
		};

		std::vector<VertexIndexBufferPair> m_VertexIndexBufferPairs;

		glm::uint m_DynamicAlignment;

		VDeleter<VkSemaphore> m_ImageAvailableSemaphore; // { m_Device, vkDestroySemaphore };
		VDeleter<VkSemaphore> m_RenderFinishedSemaphore; // { m_Device, vkDestroySemaphore };

		VkClearColorValue m_ClearColor;

		VulkanRenderer(const VulkanRenderer&) = delete;
		VulkanRenderer& operator=(const VulkanRenderer&) = delete;
	};
} // namespace flex

#endif // COMPILE_VULKAN