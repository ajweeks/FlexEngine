#pragma once
#if COMPILE_VULKAN

#include "Graphics/Renderer.h"
#include "VDeleter.h"
#include "VulkanBuffer.h"
#include "Graphics/Vulkan/VulkanHelpers.h"

#include <vector>
#include <array>
#include <map>

struct GameContext;
class Window;

class VulkanRenderer : public Renderer
{
public:
	VulkanRenderer(GameContext& gameContext);
	virtual ~VulkanRenderer();
	
	virtual void PostInitialize() override;

	virtual glm::uint Initialize(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) override;
	
	virtual void SetTopologyMode(glm::uint renderID, TopologyMode topology) override;
	virtual void SetClearColor(float r, float g, float b) override;

	virtual void Update(const GameContext& gameContext) override;
	virtual void Draw(const GameContext& gameContext, glm::uint renderID) override;
	virtual void ReloadShaders(GameContext& gameContext) override;

	virtual void OnWindowSize(int width, int height) override;

	virtual void SetVSyncEnabled(bool enableVSync) override;
	virtual void Clear(int flags, const GameContext& gameContext) override;
	virtual void SwapBuffers(const GameContext& gameContext) override;

	virtual void UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4& model) override;

	virtual int GetShaderUniformLocation(glm::uint program, const std::string uniformName) override;
	virtual void SetUniform1f(glm::uint location, float val) override;

	virtual glm::uint GetProgram(glm::uint renderID) override;
	virtual void DescribeShaderVariable(glm::uint renderID, glm::uint program, const std::string& variableName, int size,
		Renderer::Type renderType, bool normalized, int stride, void* pointer) override;

	virtual void Destroy(glm::uint renderID) override;
		
private:
	void CreateInstance(const GameContext& gameContext);
	void SetupDebugCallback();
	void CreateSurface(Window* window);
	void PickPhysicalDevice();
	void CreateLogicalDevice();
	void CreateSwapChain(Window* window);
	void CreateImageViews();
	void CreateRenderPass();
	void CreateDescriptorSetLayout(glm::uint type);
	void CreateGraphicsPipeline(glm::uint renderID);
	void CreateDepthResources();
	void CreateFramebuffers();

	void CreateTextureImage(const std::string& filePath, VulkanTexture** texture);
	void CreateTextureImageView(VulkanTexture* texture);
	void CreateTextureSampler(VulkanTexture* texture);

	void CreateVertexBuffers();
	void CreateVertexBuffer(VulkanBuffer& vertexBuffer, glm::uint shaderIndex);
	void CreateIndexBuffers();
	void CreateIndexBuffer(VulkanBuffer& indexBuffer, glm::uint shaderIndex);
	void PrepareUniformBuffers();
	void CreateDescriptorPool();
	void AllocateUniformBuffer(size_t dynamicDataSize, void** data);
	void PrepareUniformBuffer(VulkanBuffer& buffer, glm::uint bufferSize,
		VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits);
	void CreateDescriptorSet(glm::uint renderID, glm::uint descriptorSetLayoutIndex);

	void CreateCommandPool();
	void RebuildCommandBuffers();
	void CreateCommandBuffers();
	void BuildCommandBuffers();
	bool CheckCommandBuffers();
	void DestroyCommandBuffers();
	
	void CreateSemaphores();

	void CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VDeleter<VkImageView>& imageView);
	void RecreateSwapChain(Window* window);
	VkCommandBuffer BeginSingleTimeCommands();
	void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
	void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
		VkMemoryPropertyFlags properties, VDeleter<VkImage>& image, VDeleter<VkDeviceMemory>& imageMemory);
	VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
	VkFormat FindDepthFormat();
	bool HasStencilComponent(VkFormat format);
	uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
	void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
	void CopyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height);
	void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
	void CreateAndAllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VulkanBuffer& buffer);
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

	void UpdateUniformBuffers(const GameContext& gameContext);
	void UpdateUniformBufferDynamic(const GameContext& gameContext, glm::uint renderID, const glm::mat4& model);

	void LoadDefaultShaderCode();

	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugReportFlagsEXT flags, 
		VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix, 
		const char* msg, void* userData);

	VkPrimitiveTopology TopologyModeToVkPrimitiveTopology(TopologyMode mode);

	RenderObject* GetRenderObject(int renderID);
	//RenderObjectIter Destroy(RenderObjectIter iter);

	// TODO: use sorted data type (map)
	std::vector<RenderObject*> m_RenderObjects;

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

	VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties m_PhysicalDeviceProperties;
	VDeleter<VkDevice> m_Device{ vkDestroyDevice };

	VkQueue m_GraphicsQueue;
	VkQueue m_PresentQueue;

	VDeleter<VkSwapchainKHR> m_SwapChain{ m_Device, vkDestroySwapchainKHR };
	std::vector<VkImage> m_SwapChainImages;
	VkFormat m_SwapChainImageFormat;
	VkExtent2D m_SwapChainExtent;
	std::vector<VDeleter<VkImageView>> m_SwapChainImageViews;
	std::vector<VDeleter<VkFramebuffer>> m_SwapChainFramebuffers;

	VDeleter<VkRenderPass> m_RenderPass{ m_Device, vkDestroyRenderPass };

	std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts; // { m_Device, vkDestroyDescriptorSetLayout };

	VDeleter<VkCommandPool> m_CommandPool{ m_Device, vkDestroyCommandPool };
	std::vector<VkCommandBuffer> m_CommandBuffers;


	VulkanTexture* m_BrickDiffuseTexture = nullptr;
	VulkanTexture* m_BrickNormalTexture = nullptr;
	VulkanTexture* m_BrickSpecularTexture = nullptr;

	VulkanTexture* m_WorkDiffuseTexture = nullptr;
	VulkanTexture* m_WorkNormalTexture = nullptr;
	VulkanTexture* m_WorkSpecularTexture = nullptr;


	VDeleter<VkImage> m_DepthImage{ m_Device, vkDestroyImage };
	VDeleter<VkDeviceMemory> m_DepthImageMemory{ m_Device, vkFreeMemory };
	VDeleter<VkImageView> m_DepthImageView{ m_Device, vkDestroyImageView };

	VDeleter<VkDescriptorPool> m_DescriptorPool{ m_Device, vkDestroyDescriptorPool };

	// TODO: Move to render object?
	VulkanBuffer m_VertexBuffer_Simple;
	VulkanBuffer m_IndexBuffer_Simple;

	VulkanBuffer m_VertexBuffer_Color;
	VulkanBuffer m_IndexBuffer_Color;

	size_t m_DynamicAlignment;

	VDeleter<VkSemaphore> m_ImageAvailableSemaphore{ m_Device, vkDestroySemaphore };
	VDeleter<VkSemaphore> m_RenderFinishedSemaphore{ m_Device, vkDestroySemaphore };
	
	VkClearColorValue m_ClearColor;

	UniformBuffers_Simple m_UniformBuffers_Simple;
	UniformBufferObjectData_Simple m_UniformBufferData_Simple;
	UniformBufferObjectDataDynamic_Simple m_UniformBufferDynamic_Simple;

	UniformBuffers_Color m_UniformBuffers_Color;
	UniformBufferObjectData_Color m_UniformBufferData_Color;
	UniformBufferObjectDataDynamic_Color m_UniformBufferDynamic_Color;

	const std::string m_DefaultFragShaderFilePath = "resources/shaders/GLSL/spv/vk_color_frag.spv";
	const std::string m_DefaultVertShaderFilePath = "resources/shaders/GLSL/spv/vk_color_vert.spv";

	std::vector<char> m_DefaultVertShaderCode;
	std::vector<char> m_DefaultFragShaderCode;

	std::map<std::string, std::vector<char>> m_LoadedShaderCode; // filepath and code

	VulkanRenderer(const VulkanRenderer&) = delete;
	VulkanRenderer& operator=(const VulkanRenderer&) = delete;
};

#endif // COMPILE_VULKAN