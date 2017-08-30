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

			virtual void PostInitialize() override;

			virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) override;
			virtual glm::uint InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(RenderID renderID) override;
			virtual DirectionalLightID InitializeDirectionalLight(const DirectionalLight& dirLight) override;
			virtual PointLightID InitializePointLight(const PointLight& pointLight) override;

			virtual DirectionalLight& GetDirectionalLight(DirectionalLightID dirLightID) override;
			virtual PointLight& GetPointLight(PointLightID pointLightID) override;

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

			virtual void SetFloat(ShaderID shaderID, const std::string& valName, float val) override;
			virtual void SetVec2f(ShaderID shaderID, const std::string& vecName, const glm::vec2& vec) override;
			virtual void SetVec3f(ShaderID shaderID, const std::string& vecName, const glm::vec3& vec) override;
			virtual void SetVec4f(ShaderID shaderID, const std::string& vecName, const glm::vec4& vec) override;
			virtual void SetMat4f(ShaderID shaderID, const std::string& matName, const glm::mat4& mat) override;

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
			void CreateDescriptorSetLayout(glm::uint shaderIndex);
			void CreateGraphicsPipeline(RenderID renderID);
			void CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo);
			void CreateDepthResources();
			void CreateFramebuffers();

			void CreateVulkanTexture(const std::string& filePath, VulkanTexture** texture);
			void CreateVulkanCubemap(const std::array<std::string, 6>& filePaths, VulkanTexture** texture);
			void CreateTextureImage(const std::string& filePath, VulkanTexture** texture);
			void CreateTextureImageView(VulkanTexture* texture);
			void CreateTextureSampler(VulkanTexture* texture, float maxAnisotropy = 16.0f, float minLod = 0.0f, float maxLod = 0.0f);

			void CreateStaticVertexBuffers();
			glm::uint CreateStaticVertexBuffer(Buffer* vertexBuffer, glm::uint shaderIndex, int size); // Returns vertex count
			void CreateStaticIndexBuffers();
			glm::uint CreateStaticIndexBuffer(Buffer* indexBuffer, glm::uint shaderIndex); // Returns index count
			void PrepareUniformBuffers();
			void CreateDescriptorPool();
			glm::uint AllocateUniformBuffer(glm::uint dynamicDataSize, void** data);
			void PrepareUniformBuffer(Buffer* buffer, glm::uint bufferSize,
				VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits);
			void CreateDescriptorSet(RenderID renderID);
			void CreateDescriptorSet(DescriptorSetCreateInfo* createInfo);
			void ReleaseUniformBuffers();

			// TODO: Create command buffer class
			void CreateCommandPool();
			void CreateCommandBuffers();
			VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, bool begin);
			void BuildCommandBuffers();
			void RebuildCommandBuffers();
			bool CheckCommandBuffers();
			void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free);
			void DestroyCommandBuffers();

			void CreateSemaphores();

			void CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView* imageView);
			void RecreateSwapChain(Window* window);
			VkCommandBuffer BeginSingleTimeCommands();
			void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
			void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
				VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImageLayout initialLayout, VkImage* image, VkDeviceMemory* imageMemory,
				glm::uint arrayLayers = 1, glm::uint mipLevels = 1, VkImageCreateFlags flags = 0);
			VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
			VkFormat FindDepthFormat();
			bool HasStencilComponent(VkFormat format);
			uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
			void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
			void CopyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height);
			void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
			void CreateAndAllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, Buffer* buffer);
			void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
			void DrawFrame(Window* window);
			void CreateShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule);
			VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
			VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes);
			VkExtent2D ChooseSwapExtent(Window* window, const VkSurfaceCapabilitiesKHR& capabilities);
			VulkanSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
			bool IsDeviceSuitable(VkPhysicalDevice device);
			bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
			VulkanQueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
			std::vector<const char*> GetRequiredExtensions();
			bool CheckValidationLayerSupport();

			void UpdateConstantUniformBuffers(const GameContext& gameContext);
			void UpdateUniformBufferDynamic(const GameContext& gameContext, RenderID renderID, const glm::mat4& model);

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

			VDeleter<VkDescriptorPool> m_DescriptorPool; // { m_Device, vkDestroyDescriptorPool };
			std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;

			std::vector<VkCommandBuffer> m_CommandBuffers;

			// TODO: Move into vector
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

			std::vector<VertexIndexBufferPair> m_VertexIndexBufferPairs;

			glm::uint m_DynamicAlignment = 0;

			VDeleter<VkSemaphore> m_ImageAvailableSemaphore; // { m_Device, vkDestroySemaphore };
			VDeleter<VkSemaphore> m_RenderFinishedSemaphore; // { m_Device, vkDestroySemaphore };

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
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN