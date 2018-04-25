#pragma once
#if COMPILE_VULKAN

#include <array>
#include <map>

#include "Graphics/Renderer.hpp"
#include "Graphics/Vulkan/VulkanHelpers.hpp"
#include "VDeleter.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanDevice.hpp"
#include "Window/Window.hpp"
#include "Graphics/Vulkan/VulkanCommandBufferManager.hpp"

namespace flex
{
	class MeshPrefab;

	namespace vk
	{
		class VulkanPhysicsDebugDraw;

		class VulkanRenderer : public Renderer
		{
		public:
			VulkanRenderer(const GameContext& gameContext);
			virtual ~VulkanRenderer();

			virtual void Initialize(const GameContext& gameContext) override;
			virtual void PostInitialize(const GameContext& gameContext) override;

			virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) override;
			virtual u32 InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID) override;
			virtual DirectionalLightID InitializeDirectionalLight(const DirectionalLight& dirLight) override;
			virtual PointLightID InitializePointLight(const PointLight& PointLight) override;

			virtual DirectionalLight& GetDirectionalLight(DirectionalLightID dirLightID) override;
			virtual PointLight& GetPointLight(PointLightID PointLightID) override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColor(real r, real g, real b) override;

			virtual void Update(const GameContext& gameContext) override;
			virtual void Draw(const GameContext& gameContext) override;
			virtual void DrawImGuiItems(const GameContext& gameContext) override;

			virtual void UpdateRenderObjectVertexData(RenderID renderID) override;

			virtual void ReloadShaders(GameContext& gameContext) override;

			virtual void OnWindowSizeChanged(i32 width, i32 height) override;
			
			virtual void SetRenderObjectVisible(RenderID renderID, bool visible, bool effectChildren = true) override;

			virtual bool GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo) override;

			virtual void SetVSyncEnabled(bool enableVSync) override;
			virtual bool GetVSyncEnabled() override;

			virtual u32 GetRenderObjectCount() const override;
			virtual u32 GetRenderObjectCapacity() const override;

			virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size,
				DataType dataType, bool normalized, i32 stride, void* pointer) override;
			
			virtual void SetSkyboxMaterial(MaterialID skyboxMaterialID) override;
			virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) override;

			virtual Material& GetMaterial(MaterialID materialID) override;
			virtual Shader& GetShader(ShaderID shaderID) override;

			virtual bool GetMaterialID(const std::string& materialName, MaterialID& materialID) override;
			virtual bool GetShaderID(const std::string& shaderName, ShaderID& shaderID) override;

			virtual void DestroyRenderObject(RenderID renderID) override;
			
			virtual void ImGuiNewFrame() override;

		private:
			friend VulkanPhysicsDebugDraw;

			void DestroyRenderObject(RenderID renderID, VulkanRenderObject* renderObject);

			typedef void (VulkanTexture::*VulkanTextureCreateFunction)(VkQueue graphicsQueue, const std::string&, VkFormat, u32);

			struct UniformOverrides // Passed to UpdateUniformConstant or UpdateUniformDynamic to set values to something other than their defaults
			{
				Uniforms overridenUniforms; // To override a uniform, add it to this object, then set the overridden value to the respective member

				glm::mat4 projection;
				glm::mat4 view;
				glm::mat4 viewInv;
				glm::mat4 viewProjection;
				glm::vec4 camPos;
				glm::mat4 model;
				glm::mat4 modelInvTranspose;
				glm::mat4 modelViewProjection;
				u32 enableAlbedoSampler;
				u32 enableMetallicSampler;
				u32 enableRoughnessSampler;
				u32 enableAOSampler;
				u32 enableDiffuseSampler;
				u32 enableNormalSampler;
				u32 enableCubemapSampler;
				u32 enableIrradianceSampler;
			};

			void GenerateCubemapFromHDR(const GameContext& gameContext, VulkanRenderObject* renderObject);
			void GenerateIrradianceSampler(const GameContext& gameContext, VulkanRenderObject* renderObject);
			void GeneratePrefilteredCube(const GameContext& gameContext, VulkanRenderObject* renderObject);
			void GenerateBRDFLUT(const GameContext& gameContext, VulkanTexture* brdfTexture);

			// Draw all static geometry to the given render object's cubemap texture
			void CaptureSceneToCubemap(const GameContext& gameContext, RenderID cubemapRenderID);
			//void GenerateCubemapFromHDREquirectangular(const GameContext& gameContext, MaterialID cubemapMaterialID, const std::string& environmentMapPath);
			void GeneratePrefilteredMapFromCubemap(const GameContext& gameContext, MaterialID cubemapMaterialID);
			void GenerateIrradianceSamplerFromCubemap(const GameContext& gameContext, MaterialID cubemapMaterialID);
			//void GenerateBRDFLUT(const GameContext& gameContext, u32 brdfLUTTextureID, glm::uvec2 BRDFLUTSize);

			RenderID GetFirstAvailableRenderID() const;
			void InsertNewRenderObject(VulkanRenderObject* renderObject);
			void CreateInstance(const GameContext& gameContext);
			void SetupDebugCallback();
			void CreateSurface(Window* window);
			VkPhysicalDevice PickPhysicalDevice();
			void CreateLogicalDevice(VkPhysicalDevice physicalDevice);
			void CreateSwapChain(Window* window);
			void CreateSwapChainImageViews();
			void CreateRenderPass();
			void CreateDescriptorSetLayout(ShaderID shaderID);
			void CreateDescriptorSet(RenderID renderID);
			void CreateDescriptorSet(DescriptorSetCreateInfo* createInfo);
			void CreateGraphicsPipeline(RenderID renderID, bool setCubemapRenderPass);
			void CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo);
			void CreateDepthResources();
			void CreateFramebuffers();
			void PrepareOffscreenFrameBuffer(Window* window);
			void PrepareCubemapFrameBuffer();
			void PhysicsDebugRender(const GameContext& gameContext);

			void CreateUniformBuffers(VulkanShader* shader);

			// Returns a pointer i32o m_LoadedTextures if a texture has been loaded from that file path, otherwise returns nullptr
			VulkanTexture* GetLoadedTexture(const std::string& filePath);

			void CreateDynamicVertexBuffer(VulkanBuffer* vertexBuffer, u32 size, void* initialData = nullptr);
			
			// Creates vertex buffers for all render objects
			void CreateStaticVertexBuffers();

			// Creates vertex buffer for all render objects' verts which use specified shader index
			// Returns vertex count
			u32 CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, ShaderID shaderID, i32 size);
			void CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, void* vertexBufferData, u32 vertexBufferSize);

			// Creates static index buffers for all render objects
			void CreateStaticIndexBuffers();

			// Creates index buffer for all render objects' indices which use specified shader index
			// Returns index count
			u32 CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, ShaderID shaderID);
			void VulkanRenderer::CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, const std::vector<u32>& indices);

			void CreateDescriptorPool();
			u32 AllocateUniformBuffer(u32 dynamicDataSize, void** data);
			void PrepareUniformBuffer(VulkanBuffer* buffer, u32 bufferSize,
				VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits);

			void BuildCommandBuffers(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void BuildDeferredCommandBuffer(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void RebuildCommandBuffers(const GameContext& gameContext);

			void BindDescriptorSet(VulkanShader* shader, RenderID renderID, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet);
			void CreateSemaphores();
			void RecreateSwapChain(Window* window);

			void DrawFrame(Window* window);
			bool CreateShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule) const;
			VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
			VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
			VkExtent2D ChooseSwapExtent(Window* window, const VkSurfaceCapabilitiesKHR& capabilities) const;
			VulkanSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
			bool IsDeviceSuitable(VkPhysicalDevice device) const;
			bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
			std::vector<const char*> GetRequiredExtensions() const;
			bool CheckValidationLayerSupport() const;

			void UpdateConstantUniformBuffers(const GameContext& gameContext, UniformOverrides const* overridenUniforms = nullptr);
			void UpdateConstantUniformBuffer(const GameContext& gameContext, UniformOverrides const* overridenUniforms, size_t bufferIndex);
			void UpdateDynamicUniformBuffer(const GameContext& gameContext, RenderID renderID, UniformOverrides const * overridenUniforms = nullptr);

			void LoadDefaultShaderCode();
			void GenerateSkybox(const GameContext& gameContext);

			void DrawImGuiForRenderObjectAndChildren(VulkanRenderObject* renderObject);

			static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugReportFlagsEXT flags,
				VkDebugReportObjectTypeEXT objType, u64 obj, size_t location, i32 code, const char* layerPrefix,
				const char* msg, void* userData);

			VulkanRenderObject* GetRenderObject(RenderID renderID);

			/* 
			 * How many materials we expect to have *at most* at any given time
			 * This is only used to prevent local material reference variables to
			 * remain valid, it could (should?) also be solved by storing materials
			 * in a linked list, rather than a dynamic array
			*/ 
			static const u32 MAT_CAPACITY = 25;

			std::vector<VulkanRenderObject*> m_RenderObjects;
			std::vector<VulkanMaterial> m_LoadedMaterials;

			glm::vec2i m_CubemapFramebufferSize;
			glm::vec2i m_BRDFSize;
			VulkanTexture* m_BRDFTexture = nullptr;

			FrameBuffer* m_OffScreenFrameBuf = nullptr;
			VDeleter<VkSampler> m_ColorSampler;
			VkDescriptorSet m_OffscreenBufferDescriptorSet = VK_NULL_HANDLE;
			i32 m_DeferredQuadVertexBufferIndex = -1;

			bool m_PostInitialized = false;
			bool m_SwapChainNeedsRebuilding = false;

			const std::vector<const char*> m_ValidationLayers =
			{
				"VK_LAYER_LUNARG_standard_validation"
			};

			const std::vector<const char*> m_DeviceExtensions =
			{
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME
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

			VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
			VkQueue m_PresentQueue = VK_NULL_HANDLE;

			VDeleter<VkSwapchainKHR> m_SwapChain;
			std::vector<VkImage> m_SwapChainImages;
			VkFormat m_SwapChainImageFormat = VK_FORMAT_UNDEFINED;
			VkExtent2D m_SwapChainExtent;
			std::vector<VDeleter<VkImageView>> m_SwapChainImageViews;
			std::vector<VDeleter<VkFramebuffer>> m_SwapChainFramebuffers;

			FrameBuffer* m_CubemapFrameBuffer = nullptr;
			FrameBufferAttachment* m_CubemapDepthAttachment = nullptr;
			MeshPrefab* m_gBufferCubemapMesh = nullptr;
			MaterialID m_CubemapGBufferMaterialID = InvalidMaterialID;

			VDeleter<VkRenderPass> m_DeferredCombineRenderPass;

			VDeleter<VkDescriptorPool> m_DescriptorPool;
			std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;

			VulkanCommandBufferManager m_CommandBufferManager;
			std::vector<VulkanShader> m_Shaders;

			std::vector<VulkanTexture*> m_LoadedTextures;

			VulkanTexture* m_BlankTexture = nullptr;

			FrameBufferAttachment* m_DepthAttachment = nullptr;

			std::vector<VertexIndexBufferPair> m_VertexIndexBufferPairs;

			u32 m_DynamicAlignment = 0;

			VDeleter<VkSemaphore> m_PresentCompleteSemaphore;
			VDeleter<VkSemaphore> m_RenderCompleteSemaphore;

			VDeleter<VkPipelineCache> m_PipelineCache;

			VkCommandBuffer offScreenCmdBuffer = VK_NULL_HANDLE;
			VkSemaphore offscreenSemaphore = VK_NULL_HANDLE;

			RenderID m_GBufferQuadRenderID = InvalidRenderID;
			VertexBufferData m_gBufferQuadVertexBufferData;
			Transform m_gBufferQuadTransform;
			std::vector<u32> m_gBufferQuadIndices;

			MaterialID m_SkyBoxMaterialID = InvalidMaterialID; // Set by the user via SetSkyboxMaterial
			MeshPrefab* m_SkyBoxMesh = nullptr;
			
			VkClearColorValue m_ClearColor;

			static std::array<glm::mat4, 6> m_CaptureViews;

			VulkanPhysicsDebugDraw* m_PhysicsDebugDrawer = nullptr;

			VulkanRenderer(const VulkanRenderer&) = delete;
			VulkanRenderer& operator=(const VulkanRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN