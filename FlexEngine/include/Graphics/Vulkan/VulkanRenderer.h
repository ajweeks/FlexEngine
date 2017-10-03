#pragma once
#if COMPILE_VULKAN

#include <array>
#include <map>

#include <imgui.h>

#include "Graphics/Renderer.h"
#include "Graphics/Vulkan/VulkanHelpers.h"
#include "VDeleter.h"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "Window/Window.h"

namespace flex
{
	namespace vk
	{
		class VulkanRenderer : public Renderer
		{
		public:
			VulkanRenderer(const GameContext& gameContext);
			virtual ~VulkanRenderer();

			virtual void PostInitialize(const GameContext& gameContext) override;

			virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) override;
			virtual glm::uint InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID) override;
			virtual DirectionalLightID InitializeDirectionalLight(const DirectionalLight& dirLight) override;
			virtual PointLightID InitializePointLight(const PointLight& pointLight) override;

			virtual DirectionalLight& GetDirectionalLight(DirectionalLightID dirLightID) override;
			virtual PointLight& GetPointLight(PointLightID pointLightID) override;
			virtual std::vector<PointLight>& GetAllPointLights() override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColor(float r, float g, float b) override;

			virtual void Update(const GameContext& gameContext) override;
			virtual void Draw(const GameContext& gameContext) override;
			virtual void ReloadShaders(GameContext& gameContext) override;

			virtual void OnWindowSize(int width, int height) override;

			virtual void SetVSyncEnabled(bool enableVSync) override;

			virtual void UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model) override;

			virtual glm::uint GetRenderObjectCount() const override;
			virtual glm::uint GetRenderObjectCapacity() const override;

			virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size,
				Renderer::Type renderType, bool normalized, int stride, void* pointer) override;

			virtual void Destroy(RenderID renderID) override;

			virtual void GetRenderObjectInfos(std::vector<RenderObjectInfo>& vec) override;

			virtual void ImGui_Init(const GameContext& gameContext) override;
			virtual void ImGui_NewFrame(const GameContext& gameContext) override;
			virtual void ImGui_Render() override;
			virtual void ImGui_ReleaseRenderObjects() override;

		private:
			void ImGui_InitResources();
			bool ImGui_CreateFontsTexture(VkQueue copyQueue);
			void ImGui_UpdateBuffers();
			void ImGui_DrawFrame(VkCommandBuffer commandBuffer);
			void ImGui_InvalidateDeviceObjects();
			uint32_t ImGui_MemoryType(VkMemoryPropertyFlags properties, uint32_t type_bits);

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
			void CreateDescriptorSetLayout(ShaderID shaderID);
			void CreateDescriptorSet(RenderID renderID);
			void CreateDescriptorSet(DescriptorSetCreateInfo* createInfo);
			void CreateGraphicsPipeline(RenderID renderID);
			void CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo);
			void CreateDepthResources();
			void CreateFramebuffers();
			void PrepareOffscreenFrameBuffer(Window* window);

			// Expects *texture == nullptr
			void CreateVulkanTexture(const std::string& filePath, VulkanTexture** texture)const;
			// Expects *texture == nullptr
			void CreateVulkanCubemap(const std::array<std::string, 6>& filePaths, VulkanTexture** texture) const;
			void CreateTextureImage(const std::string& filePath, VulkanTexture** texture) const;
			void CreateTextureImageView(VulkanTexture* texture) const;
			void CreateTextureSampler(VulkanTexture* texture, float maxAnisotropy = 16.0f, float minLod = 0.0f, float maxLod = 0.0f) const;

			// Creates vertex buffers for all render objects
			void CreateStaticVertexBuffers();
			
			// Creates vertex buffer for all render objects' verts which use specified shader index
			// Returns vertex count
			glm::uint CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, ShaderID shaderID, int size);
			void CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, void* vertexBufferData, glm::uint vertexBufferSize);
			
			// Creates static index buffers for all render objects
			void CreateStaticIndexBuffers();

			// Creates index buffer for all render objects' indices which use specified shader index
			// Returns index count
			glm::uint CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, ShaderID shaderID);
			void VulkanRenderer::CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, const std::vector<glm::uint>& indices);

			void PrepareUniformBuffers();
			void CreateDescriptorPool();
			glm::uint AllocateUniformBuffer(glm::uint dynamicDataSize, void** data);
			void PrepareUniformBuffer(VulkanBuffer* buffer, glm::uint bufferSize,
				VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits);
			void ReleaseUniformBuffers();

			// TODO: Create command buffer class
			void CreateCommandPool();
			void CreateCommandBuffers();
			VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, bool begin) const;
			void BuildCommandBuffers();
			void BuildDeferredCommandBuffer();
			void RebuildCommandBuffers();
			bool CheckCommandBuffers() const;
			void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free) const;
			void DestroyCommandBuffers();

			void CreateSemaphores();

			void CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView* imageView) const;
			void RecreateSwapChain(Window* window);
			VkCommandBuffer BeginSingleTimeCommands() const;
			void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;
			void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
				VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImageLayout initialLayout, VkImage* image, VkDeviceMemory* imageMemory, glm::uint arrayLayers = 1, glm::uint mipLevels = 1, VkImageCreateFlags flags = 0) const;
			VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;
			bool HasStencilComponent(VkFormat format) const;
			uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
			void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) const;
			void CopyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height) const;
			void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
			void CreateAndAllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VulkanBuffer* buffer) const;
			void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0) const;
			void DrawFrame(Window* window);
			bool CreateShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule) const;
			VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
			VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes) const;
			VkExtent2D ChooseSwapExtent(Window* window, const VkSurfaceCapabilitiesKHR& capabilities) const;
			VulkanSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
			bool IsDeviceSuitable(VkPhysicalDevice device) const;
			bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
			VulkanQueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
			std::vector<const char*> GetRequiredExtensions() const;
			bool CheckValidationLayerSupport() const;

			void UpdateConstantUniformBuffers(const GameContext& gameContext);
			void UpdateDynamicUniformBuffer(const GameContext& gameContext, RenderID renderID, const glm::mat4& model);

			void LoadDefaultShaderCode();

			static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugReportFlagsEXT flags,
				VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix,
				const char* msg, void* userData);

			RenderObject* GetRenderObject(RenderID renderID);

			std::vector<RenderObject*> m_RenderObjects;
			DirectionalLight m_DirectionalLight;
			std::vector<PointLight> m_PointLights;
			std::vector<UniformBuffer> m_UniformBuffers;
			std::vector<Shader> m_Shaders;
			std::vector<VulkanMaterial> m_LoadedMaterials;


			FrameBuffer* offScreenFrameBuf = nullptr;
			VkSampler colorSampler;
			VkDescriptorSet m_OffscreenBufferDescriptorSet = VK_NULL_HANDLE;
			int m_DeferredQuadVertexBufferIndex;



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

			VDeleter<VkSwapchainKHR> m_SwapChain;
			std::vector<VkImage> m_SwapChainImages;
			VkFormat m_SwapChainImageFormat;
			VkExtent2D m_SwapChainExtent;
			std::vector<VDeleter<VkImageView>> m_SwapChainImageViews;
			std::vector<VDeleter<VkFramebuffer>> m_SwapChainFramebuffers;

			VDeleter<VkRenderPass> m_DeferredCombineRenderPass;

			VDeleter<VkDescriptorPool> m_DescriptorPool;
			std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;

			std::vector<VkCommandBuffer> m_CommandBuffers;

			std::vector<VulkanTexture*> m_LoadedTextures;
			
			VulkanTexture* m_BlankTexture = nullptr;

			// TODO: Use FrameBufferAttachment
			VDeleter<VkImage> m_DepthImage;
			VDeleter<VkDeviceMemory> m_DepthImageMemory;
			VDeleter<VkImageView> m_DepthImageView;
			VkFormat m_DepthImageFormat;

			std::vector<VertexIndexBufferPair> m_VertexIndexBufferPairs;

			glm::uint m_DynamicAlignment = 0;

			VDeleter<VkSemaphore> m_PresentCompleteSemaphore;
			VDeleter<VkSemaphore> m_RenderCompleteSemaphore;


			VkPipelineLayout m_DeferredPipelineLayout = VK_NULL_HANDLE;
			VkPipeline m_DeferredPipeline = VK_NULL_HANDLE;
			VkCommandBuffer offScreenCmdBuffer = VK_NULL_HANDLE;
			VkSemaphore offscreenSemaphore = VK_NULL_HANDLE;
			VertexIndexBufferPair offscreenQuadVertexIndexBufferPair;

			
			VkClearColorValue m_ClearColor;


			// ImGui members
			VkPipelineLayout m_ImGui_PipelineLayout = VK_NULL_HANDLE;
			VkPipeline m_ImGui_GraphicsPipeline = VK_NULL_HANDLE;

			const int IMGUI_VK_QUEUED_FRAMES = 2;
			const int IMGUI_MAX_POSSIBLE_BACK_BUFFERS = 16;

			VkPipelineCache m_ImGuiPipelineCache = VK_NULL_HANDLE;
			VkDescriptorSet m_ImGuiDescriptorSet = VK_NULL_HANDLE;
			VulkanTexture* m_ImGuiFontTexture = nullptr;

			PushConstBlock m_ImGuiPushConstBlock;

			VulkanRenderer(const VulkanRenderer&) = delete;
			VulkanRenderer& operator=(const VulkanRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN