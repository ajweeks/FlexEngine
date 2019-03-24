#pragma once
#if COMPILE_VULKAN

#include "Graphics/Renderer.hpp"

#include <array>
#include <map>

#include "Callbacks/InputCallbacks.hpp"
#include "VDeleter.hpp"
#include "VulkanCommandBufferManager.hpp"
#include "VulkanHelpers.hpp"
#include "Window/Window.hpp"

namespace flex
{
	class MeshComponent;

	namespace vk
	{
		class VulkanPhysicsDebugDraw;
		struct VulkanBuffer;
		struct VulkanDevice;

		class VulkanRenderer : public Renderer
		{
		public:
			VulkanRenderer();
			virtual ~VulkanRenderer();

			virtual void Initialize() override;
			virtual void PostInitialize() override;
			virtual void Destroy() override;

			virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace = InvalidMaterialID) override;
			virtual TextureID InitializeTexture(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) override;
			virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(RenderID renderID) override;

			virtual void ClearMaterials(bool bDestroyEngineMats = false) override;

			virtual void Update() override;
			virtual void Draw() override;
			virtual void DrawImGuiRenderObjects() override;

			virtual void UpdateVertexData(RenderID renderID, VertexBufferData* vertexBufferData) override;

			virtual void DrawUntexturedQuad(const glm::vec2& pos, AnchorPoint anchor, const glm::vec2& size, const glm::vec4& color) override;
			virtual void DrawUntexturedQuadRaw(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color) override;
			virtual void DrawSprite(const SpriteQuadDrawInfo& drawInfo) override;

			virtual void UpdateRenderObjectVertexData(RenderID renderID) override;

			virtual void ReloadShaders() override;
			virtual void LoadFonts(bool bForceRender) override;

			virtual void ReloadSkybox(bool bRandomizeTexture) override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColor(real r, real g, real b) override;

			virtual void OnWindowSizeChanged(i32 width, i32 height) override;

			virtual void OnPreSceneChange() override;
			virtual void OnPostSceneChange() override;

			virtual bool GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo) override;

			virtual void SetVSyncEnabled(bool bEnableVSync) override;

			virtual u32 GetRenderObjectCount() const override;
			virtual u32 GetRenderObjectCapacity() const override;

			virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size, DataType dataType, bool normalized, i32 stride, void* pointer) override;

			virtual void SetSkyboxMesh(GameObject* skyboxMesh) override;
			virtual GameObject* GetSkyboxMesh() override;
			virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) override;

			virtual Material& GetMaterial(MaterialID materialID) override;
			virtual Shader& GetShader(ShaderID shaderID) override;

			virtual bool GetShaderID(const std::string& shaderName, ShaderID& shaderID) override;
			virtual bool GetMaterialID(const std::string& materialName, MaterialID& materialID) override;
			virtual MaterialID GetMaterialID(RenderID renderID) override;

			virtual std::vector<Pair<std::string, MaterialID>> GetValidMaterialNames() const override;

			virtual void DestroyRenderObject(RenderID renderID) override;

			virtual void NewFrame() override;

			virtual btIDebugDraw* GetDebugDrawer() override;

			virtual void SetFont(BitmapFont* font) override;
			// Draws the given string in the center of the screen for a short period of time
			// Passing an empty string will immediately clear the current string
			virtual void AddEditorString(const std::string& str) override;
			virtual void DrawStringSS(const std::string& str,
				const glm::vec4& color,
				AnchorPoint anchor,
				const glm::vec2& pos, // Positional offset from anchor
				real spacing,
				bool bRaw = false) override;

			virtual void DrawStringWS(const std::string& str,
				const glm::vec4& color,
				const glm::vec3& pos,
				const glm::quat& rot,
				real spacing,
				bool bRaw = false) override;

			virtual real GetStringWidth(const std::string& str, BitmapFont* font, real letterSpacing, bool bNormalized) const override;
			virtual real GetStringHeight(const std::string& str, BitmapFont* font, bool bNormalized) const override;

			virtual void DrawAssetBrowserImGui(bool* bShowing) override;
			virtual void RecaptureReflectionProbe() override;
			virtual u32 GetTextureHandle(TextureID textureID) const override;
			virtual void RenderObjectStateChanged() override;

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
				u32 enableNormalSampler;
				u32 enableCubemapSampler;
				u32 enableIrradianceSampler;
			};

			void GenerateCubemapFromHDR(VulkanRenderObject* renderObject, const std::string& environmentMapPath);
			void GenerateIrradianceSampler(VulkanRenderObject* renderObject);
			void GeneratePrefilteredCube(VulkanRenderObject* renderObject);
			void GenerateBRDFLUT(VulkanTexture* brdfTexture);

			// Draw all static geometry to the given render object's cubemap texture
			void CaptureSceneToCubemap(RenderID cubemapRenderID);
			//void GenerateCubemapFromHDREquirectangular(MaterialID cubemapMaterialID, const std::string& environmentMapPath);
			void GeneratePrefilteredMapFromCubemap(MaterialID cubemapMaterialID);
			void GenerateIrradianceSamplerFromCubemap(MaterialID cubemapMaterialID);
			//void GenerateBRDFLUT(u32 brdfLUTTextureID, glm::vec2 BRDFLUTSize);

			MaterialID GetNextAvailableMaterialID();
			RenderID GetNextAvailableRenderID() const;

			void InsertNewRenderObject(VulkanRenderObject* renderObject);
			void CreateInstance();
			void SetupDebugCallback();
			void CreateSurface();
			//void SetupImGuiWindowData(ImGui_ImplVulkanH_WindowData* data, VkSurfaceKHR surface, i32 width, i32 height);
			VkPhysicalDevice PickPhysicalDevice();
			void CreateLogicalDevice(VkPhysicalDevice physicalDevice);
			void CreateSwapChain();
			void CreateSwapChainImageViews();
			void CreateRenderPass();
			void CreateDescriptorSetLayout(ShaderID shaderID);
			void CreateDescriptorSet(RenderID renderID);
			void CreateDescriptorSet(DescriptorSetCreateInfo* createInfo);
			void CreateGraphicsPipeline(RenderID renderID, bool bSetCubemapRenderPass);
			void CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo);
			void CreateDepthResources();
			void CreateFramebuffers();
			void PrepareOffscreenFrameBuffer();
			void PrepareCubemapFrameBuffer();
			void PhysicsDebugRender();

			void GenerateGBufferVertexBuffer();
			void GenerateGBuffer();

			void RemoveMaterial(MaterialID materialID);

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

			void BuildCommandBuffers(const DrawCallInfo& drawCallInfo);
			void BuildDeferredCommandBuffer(const DrawCallInfo& drawCallInfo);
			void RebuildCommandBuffers();

			void BindDescriptorSet(VulkanShader* shader, i32 meshIndex, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet);
			void CreateSemaphores();
			void RecreateSwapChain();

			void DrawFrame();
			bool CreateShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule) const;
			VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
			VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
			VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
			VulkanSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
			bool IsDeviceSuitable(VkPhysicalDevice device) const;
			bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
			std::vector<const char*> GetRequiredExtensions() const;
			bool CheckValidationLayerSupport() const;

			void UpdateConstantUniformBuffers(UniformOverrides const* overridenUniforms = nullptr);
			void UpdateDynamicUniformBuffer(RenderID renderID, UniformOverrides const * overridenUniforms = nullptr);

			void LoadDefaultShaderCode();

			void GenerateReflectionProbeAndIrradianceMaps();

			void DrawImGuiForRenderObjectAndChildren(GameObject* gameObject);
			// Returns true if object was duplicated
			bool DoTextureSelector(const char* label, const std::vector<VulkanTexture*>& textures, i32* selectedIndex, bool* bGenerateSampler);
			void ImGuiUpdateTextureIndexOrMaterial(bool bUpdateTextureMaterial,
				const std::string& texturePath,
				std::string& matTexturePath,
				VulkanTexture* texture,
				i32 i,
				i32* textureIndex,
				VkSampler* sampler);
			void DoTexturePreviewTooltip(VulkanTexture* texture);

			static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugReportFlagsEXT flags,
				VkDebugReportObjectTypeEXT objType, u64 obj, size_t location, i32 code, const char* layerPrefix,
				const char* msg, void* userData);

			VulkanRenderObject* GetRenderObject(RenderID renderID);

			u32 GetActiveRenderObjectCount() const;

			// Will attempt to find pre-rendered font at specified path, and
			// only render a new file if not present or if bForceRender is true
			bool LoadFont(BitmapFont** font,
				i16 size,
				const std::string& fontFilePath,
				const std::string& renderedFontFilePath,
				bool bForceRender,
				bool bScreenSpace);

			u32 GetAlignedUBOSize(u32 unalignedSize);

			const u32 MAX_NUM_RENDER_OBJECTS = 4096; // TODO: Not this?
			std::vector<VulkanRenderObject*> m_RenderObjects;
			std::map<MaterialID, VulkanMaterial> m_Materials;

			glm::vec2i m_CubemapFramebufferSize;
			glm::vec2i m_BRDFSize;
			VulkanTexture* m_BRDFTexture = nullptr;

			FrameBuffer* m_OffScreenFrameBuf = nullptr;
			VDeleter<VkSampler> m_ColorSampler;
			VkDescriptorSet m_OffscreenBufferDescriptorSet = VK_NULL_HANDLE;
			i32 m_DeferredQuadVertexBufferIndex = -1;

			bool m_bPostInitialized = false;
			bool m_bSwapChainNeedsRebuilding = false;

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
			const bool m_bEnableValidationLayers = false;
#else
			const bool m_bEnableValidationLayers = true;
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

			TextureID m_AlphaBGTextureID = InvalidTextureID;
			TextureID m_LoadingTextureID = InvalidTextureID;
			TextureID m_WorkTextureID = InvalidTextureID;

			TextureID m_PointLightIconID = InvalidTextureID;
			TextureID m_DirectionalLightIconID = InvalidTextureID;

			VDeleter<VkSemaphore> m_PresentCompleteSemaphore;
			VDeleter<VkSemaphore> m_RenderCompleteSemaphore;

			VDeleter<VkPipelineCache> m_PipelineCache;

			VkCommandBuffer offScreenCmdBuffer = VK_NULL_HANDLE;
			VkSemaphore offscreenSemaphore = VK_NULL_HANDLE;

			RenderID m_GBufferQuadRenderID = InvalidRenderID;
			VertexBufferData m_gBufferQuadVertexBufferData;
			std::vector<u32> m_gBufferQuadIndices;

			GameObject* m_SkyBoxMesh = nullptr;

			VkClearColorValue m_ClearColor;

			static std::array<glm::mat4, 6> s_CaptureViews;

			VulkanPhysicsDebugDraw* m_PhysicsDebugDrawer = nullptr;

			VulkanRenderer(const VulkanRenderer&) = delete;
			VulkanRenderer& operator=(const VulkanRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN