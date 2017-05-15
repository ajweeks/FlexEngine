#pragma once
#if COMPILE_VULKAN

#include "../Renderer.h"

#include <functional>
#include <vector>

struct GameContext;
class Window;

template <typename T>
class VDeleter
{
public:
	VDeleter();

	VDeleter(std::function<void(T, VkAllocationCallbacks*)> deletef);
	VDeleter(const VDeleter<VkInstance>& instance, std::function<void(VkInstance, T, VkAllocationCallbacks*)> deletef);
	VDeleter(const VDeleter<VkDevice>& device, std::function<void(VkDevice, T, VkAllocationCallbacks*)> deletef);
	~VDeleter();

	const T* operator &() const;
	T* replace();
	operator T() const;
	void operator=(T rhs);

	template<typename V>
	bool operator==(V rhs);

private:
	T object{ VK_NULL_HANDLE };
	std::function<void(T)> deleter;

	void cleanup();
};

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
	static VkVertexInputBindingDescription GetVertPosColTexBindingDescription();
	static VkVertexInputBindingDescription GetVertPosColBindingDescription();
	static std::array<VkVertexInputAttributeDescription, 3> GetVertPosColTexAttributeDescriptions();
	static std::array<VkVertexInputAttributeDescription, 2> GetVertPosColAttributeDescriptions();
	//bool operator==(const VulkanVertex& other) const;
};

struct Buffer
{
	Buffer(const VDeleter<VkDevice>& device) :
		buffer(VDeleter<VkBuffer>(device, vkDestroyBuffer)),
		memory(VDeleter<VkDeviceMemory>(device, vkFreeMemory))
	{}

	VDeleter<VkBuffer> buffer; // { m_Device, vkDestroyBuffer };
	VDeleter<VkDeviceMemory> memory; // { m_Device, vkFreeMemory };
	VkDescriptorBufferInfo descriptor;
	VkDeviceSize size = 0;
	VkDeviceSize alignment = 0;
	void* mapped = nullptr;

	VkBufferUsageFlags usageFlags;
	VkMemoryPropertyFlags memoryPropertyFlags;
};

struct UniformBuffers 
{
	UniformBuffers(const VDeleter<VkDevice>& device) :
		viewBuffer(device),
		dynamicBuffer(device)
	{}

	Buffer viewBuffer;
	Buffer dynamicBuffer;
};

struct UniformBufferObjectData 
{
	glm::mat4 projection;
	glm::mat4 view;
};

struct UniformBufferObjectDynamic
{
	glm::mat4* model;
};

VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback);

void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator);


class VulkanRenderer : public Renderer
{
public:
	VulkanRenderer(GameContext& gameContext);
	virtual ~VulkanRenderer();
	
	virtual void PostInitialize() override;

	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices) override;
	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices,
		std::vector<glm::uint>* indices) override;
	
	virtual void SetClearColor(float r, float g, float b) override;

	virtual void Draw(const GameContext& gameContext, glm::uint renderID) override;

	virtual void OnWindowSize(int width, int height) override;

	virtual void SetVSyncEnabled(bool enableVSync) override;
	virtual void Clear(int flags, const GameContext& gameContext) override;
	virtual void SwapBuffers(const GameContext& gameContext) override;

	virtual void UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4x4& model) override;

	virtual int GetShaderUniformLocation(glm::uint program, const std::string uniformName) override;
	virtual void SetUniform1f(glm::uint location, float val) override;

	virtual void DescribeShaderVariable(glm::uint renderID, glm::uint program, const std::string& variableName, int size,
		Renderer::Type renderType, bool normalized, int stride, void* pointer) override;

	virtual void Destroy(glm::uint renderID) override;

private:
	void CreateInstance();
	void SetupDebugCallback();
	void CreateSurface(Window* window);
	void PickPhysicalDevice();
	void CreateLogicalDevice();
	void CreateSwapChain(Window* window);
	void CreateImageViews();
	void CreateRenderPass();
	void CreateDescriptorSetLayout();
	void CreateGraphicsPipeline();
	void CreateDepthResources();
	void CreateFramebuffers();
	void CreateTextureImage(const std::string& filePath);
	void CreateTextureImageView();
	void CreateTextureSampler();
	void LoadModel(const std::string& filePath);

	void CreateVertexBuffer();
	void CreateIndexBuffer();
	void PrepareUniformBuffers();
	void CreateDescriptorPool();
	void CreateDescriptorSet();

	void CreateCommandPool();
	void ReBuildCommandBuffers();
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
	void CreateAndAllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, Buffer& buffer);
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

	void UpdateUniformBuffer(const GameContext& gameContext);
	void UpdateUniformBufferDynamic(const GameContext& gameContext, glm::uint renderID, const glm::mat4& model);

	static std::vector<char> ReadFile(const std::string& filename);
	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugReportFlagsEXT flags, 
		VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix, 
		const char* msg, void* userData);

	struct RenderObject
	{
		glm::uint renderID;

		glm::uint VAO;
		glm::uint VBO;
		glm::uint IBO;

		std::vector<VertexPosCol>* vertices = nullptr;
		glm::uint vertexOffset = 0;

		bool indexed = false;
		std::vector<glm::uint>* indices = nullptr;
		glm::uint indexOffset = 0;
	};

	RenderObject* GetRenderObject(int renderID);

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
	VkDescriptorSet m_DescriptorSet;
	VDeleter<VkDescriptorSetLayout> m_DescriptorSetLayout{ m_Device, vkDestroyDescriptorSetLayout };
	VDeleter<VkPipelineLayout> m_PipelineLayout{ m_Device, vkDestroyPipelineLayout };
	VDeleter<VkPipeline> m_GraphicsPipeline{ m_Device, vkDestroyPipeline };

	VDeleter<VkCommandPool> m_CommandPool{ m_Device, vkDestroyCommandPool };
	std::vector<VkCommandBuffer> m_CommandBuffers;

	VDeleter<VkImage> m_TextureImage{ m_Device, vkDestroyImage };
	VDeleter<VkDeviceMemory> m_TextureImageMemory{ m_Device, vkFreeMemory };
	VDeleter<VkImageView> m_TextureImageView{ m_Device, vkDestroyImageView };
	VDeleter<VkSampler> m_TextureSampler{ m_Device, vkDestroySampler };

	VDeleter<VkImage> m_DepthImage{ m_Device, vkDestroyImage };
	VDeleter<VkDeviceMemory> m_DepthImageMemory{ m_Device, vkFreeMemory };
	VDeleter<VkImageView> m_DepthImageView{ m_Device, vkDestroyImageView };

	//std::vector<VertexPosCol> m_Vertices;
	//std::vector<uint32_t> m_Indices;

	//VDeleter<VkBuffer> m_UniformStagingBuffer{ m_Device, vkDestroyBuffer };
	//VDeleter<VkDeviceMemory> m_UniformStagingBufferMemory{ m_Device, vkFreeMemory };
	//VDeleter<VkBuffer> m_UniformBuffer{ m_Device, vkDestroyBuffer };
	//VDeleter<VkDeviceMemory> m_UniformBufferMemory{ m_Device, vkFreeMemory };

	VDeleter<VkDescriptorPool> m_DescriptorPool{ m_Device, vkDestroyDescriptorPool };

	Buffer m_VertexBuffer;
	Buffer m_IndexBuffer;

	size_t m_DynamicAlignment;

	VDeleter<VkSemaphore> m_ImageAvailableSemaphore{ m_Device, vkDestroySemaphore };
	VDeleter<VkSemaphore> m_RenderFinishedSemaphore{ m_Device, vkDestroySemaphore };
	
	VkClearColorValue m_ClearColor;

	UniformBuffers m_UniformBuffers;
	UniformBufferObjectData m_UniformBufferData;
	UniformBufferObjectDynamic m_UniformBufferDynamic;

	VulkanRenderer(const VulkanRenderer&) = delete;
	VulkanRenderer& operator=(const VulkanRenderer&) = delete;
};

#endif // COMPILE_VULKAN