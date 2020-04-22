#pragma once
#if COMPILE_VULKAN

#include "Graphics/Renderer.hpp"

#include <array>
#include <map>

#include "Callbacks/InputCallbacks.hpp"
#include "VDeleter.hpp"
#include "VulkanCommandBufferManager.hpp"
#include "VulkanHelpers.hpp"
#include "VulkanRenderPass.hpp"
#include "Window/Window.hpp"

namespace flex
{
	namespace vk
	{
		class VulkanPhysicsDebugDraw;
		struct VulkanBuffer;
		struct VulkanDevice;

		class VulkanRenderer : public Renderer
		{
			struct ShaderBatchPair;

		public:
			VulkanRenderer();
			virtual ~VulkanRenderer();

			virtual void Initialize() override;
			virtual void PostInitialize() override;

			virtual void Destroy() override;

			virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace = InvalidMaterialID) override;
			virtual TextureID InitializeTextureFromFile(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) override;
			virtual TextureID InitializeTextureFromMemory(void* data, u32 size, VkFormat inFormat, const std::string& name, u32 width, u32 height, u32 channelCount, VkFilter inFilter) override;
			virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(RenderID renderID) override;
			virtual void DestroyTexture(TextureID textureID) override;

			virtual void ClearMaterials(bool bDestroyPersistentMats = false) override;

			virtual void Update() override;
			virtual void Draw() override;
			virtual void DrawImGuiWindows() override;

			virtual void UpdateVertexData(RenderID renderID, VertexBufferData const* vertexBufferData, const std::vector<u32>& indexData) override;

			virtual void RecompileShaders(bool bForce) override;
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

			virtual void SetSkyboxMesh(Mesh* skyboxMesh) override;
			virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) override;

			virtual Material& GetMaterial(MaterialID materialID) override;
			virtual Shader& GetShader(ShaderID shaderID) override;

			virtual bool GetShaderID(const std::string& shaderName, ShaderID& shaderID) override;
			virtual bool FindOrCreateMaterialByName(const std::string& materialName, MaterialID& materialID) override;
			virtual MaterialID GetRenderObjectMaterialID(RenderID renderID) override;

			virtual std::vector<Pair<std::string, MaterialID>> GetValidMaterialNames() const override;

			virtual void DestroyRenderObject(RenderID renderID) override;

			virtual void NewFrame() override;

			virtual PhysicsDebugDrawBase* GetDebugDrawer() override;

			virtual void DrawStringSS(const std::string& str,
				const glm::vec4& color,
				AnchorPoint anchor,
				const glm::vec2& pos, // Positional offset from anchor
				real spacing,
				real scale = 1.0f) override;

			virtual void DrawStringWS(const std::string& str,
				const glm::vec4& color,
				const glm::vec3& pos,
				const glm::quat& rot,
				real spacing,
				real scale = 1.0f) override;

			virtual void DrawAssetBrowserImGui(bool* bShowing) override;
			virtual void DrawImGuiForRenderObject(RenderID renderID) override;

			virtual void RecaptureReflectionProbe() override;
			virtual void RenderObjectStateChanged() override;

			virtual ParticleSystemID AddParticleSystem(const std::string& name, ParticleSystem* system, i32 particleCount) override;
			virtual bool RemoveParticleSystem(ParticleSystemID particleSystemID) override;

			void RegisterFramebufferAttachment(FrameBufferAttachment* frameBufferAttachment);
			FrameBufferAttachment* GetFrameBufferAttachment(FrameBufferAttachmentID frameBufferAttachmentID) const;

			static void SetObjectName(VulkanDevice* device, u64 object, VkDebugReportObjectTypeEXT type, const char* name);
			static void SetCommandBufferName(VulkanDevice* device, VkCommandBuffer commandBuffer, const char* name);
			static void SetSwapchainName(VulkanDevice* device, VkSwapchainKHR swapchain, const char* name);
			static void SetDescriptorSetName(VulkanDevice* device, VkDescriptorSet descSet, const char* name);
			static void SetPipelineName(VulkanDevice* device, VkPipeline pipeline, const char* name);
			static void SetFramebufferName(VulkanDevice* device, VkFramebuffer framebuffer, const char* name);
			static void SetRenderPassName(VulkanDevice* device, VkRenderPass renderPass, const char* name);
			static void SetImageName(VulkanDevice* device, VkImage image, const char* name);
			static void SetImageViewName(VulkanDevice* device, VkImageView imageView, const char* name);
			static void SetSamplerName(VulkanDevice* device, VkSampler sampler, const char* name);
			static void SetBufferName(VulkanDevice* device, VkBuffer buffer, const char* name);

			static void BeginDebugMarkerRegion(VkCommandBuffer cmdBuf, const char* markerName, glm::vec4 color = VEC4_ONE);
			static void EndDebugMarkerRegion(VkCommandBuffer cmdBuf);

			bool bDebugUtilsExtensionPresent = false;

			static PFN_vkDebugMarkerSetObjectNameEXT m_vkDebugMarkerSetObjectName;
			static PFN_vkCmdDebugMarkerBeginEXT m_vkCmdDebugMarkerBegin;
			static PFN_vkCmdDebugMarkerEndEXT m_vkCmdDebugMarkerEnd;
			bool m_bEnableDebugMarkers = false;

		protected:
			virtual bool LoadShaderCode(ShaderID shaderID) override;
			virtual void SetShaderCount(u32 shaderCount) override;
			virtual void RemoveMaterial(MaterialID materialID) override;
			virtual void FillOutGBufferFrameBufferAttachments(std::vector<Pair<std::string, void*>>& outVec) override;
			virtual bool LoadFont(FontMetaData& fontMetaData, bool bForceRender) override;

			virtual void EnqueueScreenSpaceSprites() override;
			virtual void EnqueueWorldSpaceSprites() override;

		private:
			friend VulkanPhysicsDebugDraw;
			friend VulkanRenderPass;

			void DestroyRenderObject(RenderID renderID, VulkanRenderObject* renderObject);

			VkPhysicalDeviceFeatures GetEnabledFeaturesForDevice(VkPhysicalDevice physicalDevice);

			struct UniformOverrides
			{
				Uniforms overridenUniforms;

				glm::mat4 projection;
				glm::mat4 view;
				glm::mat4 viewProjection;
				glm::vec4 camPos;
				glm::mat4 model;
				glm::mat4 modelInvTranspose;
				u32 enableAlbedoSampler;
				u32 enableMetallicSampler;
				u32 enableRoughnessSampler;
				u32 enableNormalSampler;
				u32 enableIrradianceSampler;
				i32 texChannel;
				glm::vec4 sdfData;
				glm::vec4 fontCharData;
				glm::vec2 texSize;
				glm::vec4 colorMultiplier;
				bool bSSAOVerticalPass;
				ParticleSimData* particleSimData = nullptr;
			};

			bool InitializeFreeType();
			void DestroyFreeType();

			void GenerateCubemapFromHDR(VulkanRenderObject* renderObject, const std::string& environmentMapPath);
			void GenerateIrradianceSampler(VulkanRenderObject* renderObject);
			void GeneratePrefilteredCube(VulkanRenderObject* renderObject);
			void GenerateBRDFLUT();

			void CaptureSceneToCubemap(RenderID cubemapRenderID);
			void GeneratePrefilteredMapFromCubemap(MaterialID cubemapMaterialID);
			void GenerateIrradianceSamplerFromCubemap(MaterialID cubemapMaterialID);

			void CreateSSAOPipelines();
			void CreateSSAODescriptorSets();

			MaterialID GetNextAvailableMaterialID() const;
			RenderID GetNextAvailableRenderID() const;
			ParticleSystemID GetNextAvailableParticleSystemID() const;

			void InsertNewRenderObject(VulkanRenderObject* renderObject);
			void InsertNewParticleSystem(VulkanParticleSystem* particleSystem);

			void CreateInstance();
			void SetupDebugCallback();
			void CreateSurface();
			//void SetupImGuiWindowData(ImGui_ImplVulkanH_WindowData* data, VkSurfaceKHR surface, i32 width, i32 height);
			VkPhysicalDevice PickPhysicalDevice();
			void CreateLogicalDevice(VkPhysicalDevice physicalDevice);
			void FindPresentInstanceExtensions();
			void CreateSwapChain();
			void CreateSwapChainImageViews();
			void CreateRenderPasses();
			void CalculateAutoLayoutTransitions();

			void FillOutBufferDescriptorInfos(ShaderUniformContainer<BufferDescriptorInfo>* descriptors, UniformBufferList* uniformBufferList, ShaderID shaderID);
			void CreateDescriptorSet(RenderID renderID);
			void CreateDescriptorSet(DescriptorSetCreateInfo* createInfo);
			void CreateDescriptorSetLayout(ShaderID shaderID);
			void CreateGraphicsPipeline(RenderID renderID, bool bSetCubemapRenderPass);
			void CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo);
			void CreateDepthResources();
			void CreateSwapChainFramebuffers();
			void CreateFrameBufferAttachments();
			void CreateFrameBuffers();
			void PrepareCubemapFrameBuffer();
			void PhysicsDebugRender();

			void CreateUniformBuffers(VulkanMaterial* material);

			void CreatePostProcessingResources();
			void CreateFullscreenBlitResources();
			void CreateComputeResources();
			void CreateParticleSystemResources(VulkanParticleSystem* particleSystem);

			// Returns a pointer into m_LoadedTextures if a texture has been loaded from that file path, otherwise returns nullptr
			VulkanTexture* GetLoadedTexture(const std::string& filePath);
			bool RemoveLoadedTexture(VulkanTexture* texture, bool bDestroy);

			void CreateStaticVertexBuffers();
			void CreateDynamicVertexAndIndexBuffers();

			void CreateShadowVertexBuffer();
			void CreateAndUploadToStaticVertexBuffer(VulkanBuffer* vertexBuffer, void* vertexBufferData, u32 vertexBufferSize);
			void CreateDynamicVertexBuffer(VulkanBuffer* vertexBuffer, u32 size);
			void CreateDynamicIndexBuffer(VulkanBuffer* indexBuffer, u32 size);

			void CreateStaticIndexBuffers();

			void CreateShadowIndexBuffer();
			void CreateAndUploadToStaticIndexBuffer(VulkanBuffer* indexBuffer, const std::vector<u32>& indices);

			void CreateDescriptorPool();
			u32 AllocateDynamicUniformBuffer(u32 dynamicDataSize, void** data, i32 maxObjectCount = -1);
			void PrepareUniformBuffer(VulkanBuffer* buffer, u32 bufferSize,
				VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits, bool bMap = true);

			void CreateSemaphores();

			void BatchRenderObjects();
			void DrawShaderBatch(const ShaderBatchPair& shaderBatches, VkCommandBuffer& commandBuffer, DrawCallInfo* drawCallInfo = nullptr);

			// Expects a render pass to be in flight, renders a fullscreen tri with minimal state setup
			void RenderFullscreenTri(
				VkCommandBuffer commandBuffer,
				MaterialID materialID,
				VkPipelineLayout pipelineLayout,
				VkDescriptorSet descriptorSet);

			// Begins the given render pass, renders a fullscreen tri, then ends the render pass
			void RenderFullscreenTri(
				VkCommandBuffer commandBuffer,
				VulkanRenderPass* renderPass,
				MaterialID materialID,
				VkPipelineLayout pipelineLayout,
				VkPipeline graphicsPipeline,
				VkDescriptorSet descriptorSet,
				bool bFlipViewport);

			void BuildCommandBuffers(const DrawCallInfo& drawCallInfo);

			void DrawFrame();

			void BindDescriptorSet(const VulkanMaterial* material, u32 dynamicOffsetOffset, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet) const;
			void RecreateSwapChain();

			void BeginDebugMarkerRegionInternal(VkCommandBuffer cmdBuf, const char* markerName, glm::vec4 color = VEC4_ONE);
			void EndDebugMarkerRegionInternal(VkCommandBuffer cmdBuf);

			bool CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule) const;
			VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
			VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
			VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
			VulkanSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
			bool IsDeviceSuitable(VkPhysicalDevice device);
			bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
			std::vector<const char*> GetRequiredExtensions() const;
			bool CheckValidationLayerSupport() const;

			bool ExtensionSupported(const std::string& extStr) const;

			void UpdateConstantUniformBuffers(UniformOverrides const* overridenUniforms = nullptr);
			void UpdateDynamicUniformBuffer(RenderID renderID, UniformOverrides const* overridenUniforms = nullptr,
				MaterialID materialIDOverride = InvalidMaterialID, u32 dynamicUBOOffsetOverride = InvalidID);
			void UpdateDynamicUniformBuffer(MaterialID materialID, u32 dynamicOffsetIndex, const glm::mat4& model, UniformOverrides const* uniformOverrides = nullptr);

			void GenerateIrradianceMaps();

			void OnShaderReloadSuccess();

			void SetLineWidthForCmdBuffer(VkCommandBuffer cmdBuffer, real requestedWidth = 3.0f);

			// Returns true if object was duplicated
			bool DoTextureSelector(const char* label, const std::vector<VulkanTexture*>& textures, i32* selectedIndex);
			void ImGuiUpdateTextureIndexOrMaterial(bool bUpdateTextureMaterial,
				const std::string& texturePath,
				std::string& matTexturePath,
				VulkanTexture* texture,
				i32 i,
				i32* textureIndex,
				VulkanTexture** textureToUpdate);
			void DoTexturePreviewTooltip(VulkanTexture* texture);

			void BeginGPUTimeStamp(VkCommandBuffer commandBuffer, const std::string& name);
			void EndGPUTimeStamp(VkCommandBuffer commandBuffer, const std::string& name);
			ms GetDurationBetweenTimeStamps(const std::string& name);

			static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugReportFlagsEXT flags,
				VkDebugReportObjectTypeEXT objType, u64 obj, size_t location, i32 code, const char* layerPrefix,
				const char* msg, void* userData);

			// TODO: Monitor number of used desc sets to set this value intelligently
			static const u32 MAX_NUM_DESC_SETS = 1024;
			static const u32 MAX_NUM_DESC_COMBINED_IMAGE_SAMPLERS = 1024;
			static const u32 MAX_NUM_DESC_UNIFORM_BUFFERS = 1024;
			static const u32 MAX_NUM_DESC_DYNAMIC_UNIFORM_BUFFERS = 1024;
			static const u32 MAX_NUM_DESC_DYNAMIC_STORAGE_BUFFERS = 1; // Particles

			inline VulkanRenderObject* GetRenderObject(RenderID renderID)
			{
				return m_RenderObjects[renderID];
			}

			u32 GetActiveRenderObjectCount() const;

			u32 GetAlignedUBOSize(u32 unalignedSize);

			void DrawTextSS(VkCommandBuffer commandBuffer);
			void DrawTextWS(VkCommandBuffer commandBuffer);
			void DrawSpriteBatch(const std::vector<SpriteQuadDrawInfo>& batch, VkCommandBuffer commandBuffer);
			void DrawParticles(VkCommandBuffer commandBuffer);

			VkDescriptorSet GetSpriteDescriptorSet(TextureID textureID, MaterialID spriteMaterialID, u32 textureLayer);

			VkRenderPass ResolveRenderPassType(RenderPassType renderPassType, const char* shaderName = nullptr);

			void CreateShadowResources();
			VkDescriptorSet CreateSpriteDescSet(MaterialID spriteMaterialID, TextureID textureID, u32 layer = 0);

			void InitializeAllParticleSystemBuffers();
			void InitializeParticleSystemBuffer(VulkanParticleSystem* particleSystem);

			u32 GetStaticVertexIndexBufferIndex(u32 stride);
			u32 GetDynamicVertexIndexBufferIndex(u32 stride);

			TextureID GetNextAvailableTextureID();
			TextureID AddLoadedTexture(VulkanTexture* texture);
			VulkanTexture* GetLoadedTexture(TextureID textureID);

			std::vector<std::string> m_SupportedDeviceExtenions;

			const u32 MAX_NUM_RENDER_OBJECTS = 4096; // TODO: Not this?
			std::vector<VulkanRenderObject*> m_RenderObjects;
			std::map<MaterialID, VulkanMaterial> m_Materials;
			struct RenderObjectBatch
			{
				std::vector<RenderID> objects;
			};

			struct MaterialBatchPair
			{
				MaterialID materialID = InvalidMaterialID;
				RenderObjectBatch batch;
			};

			struct MaterialBatch
			{
				// One per material
				std::vector<MaterialBatchPair> batches;
			};

			struct ShaderBatchPair
			{
				ShaderID shaderID = InvalidShaderID;
				bool bDynamic = false;
				MaterialBatch batch;
			};

			struct ShaderBatch
			{
				// One per shader
				std::vector<ShaderBatchPair> batches;
			};

			// One per deferred-rendered shader
			ShaderBatch m_DeferredObjectBatches;
			// One per forward-rendered shader
			ShaderBatch m_ForwardObjectBatches;
			ShaderBatch m_ShadowBatch;

			ShaderBatch m_DepthAwareEditorObjBatches;
			ShaderBatch m_DepthUnawareEditorObjBatches;

			glm::vec2i m_CubemapFramebufferSize;
			glm::vec2i m_BRDFSize;
			VulkanTexture* m_BRDFTexture = nullptr;
			bool bRenderedBRDFLUT = false;

			FrameBufferAttachment* m_GBufferColorAttachment0 = nullptr;
			FrameBufferAttachment* m_GBufferColorAttachment1 = nullptr;
			FrameBufferAttachment* m_GBufferDepthAttachment = nullptr;

			VDeleter<VkSampler> m_LinMipLinSampler;
			VDeleter<VkSampler> m_DepthSampler;
			VDeleter<VkSampler> m_NearestClampEdgeSampler;

			VkFormat m_OffscreenFrameBufferFormat = VK_FORMAT_UNDEFINED;
			FrameBufferAttachment* m_OffscreenFB0ColorAttachment0 = nullptr;
			FrameBufferAttachment* m_OffscreenFB1ColorAttachment0 = nullptr;

			FrameBufferAttachment* m_OffscreenFB0DepthAttachment = nullptr;
			FrameBufferAttachment* m_OffscreenFB1DepthAttachment = nullptr;

			VulkanTexture* m_HistoryBuffer = nullptr;

			VkDescriptorSet m_PostProcessDescriptorSet = VK_NULL_HANDLE;
			VkDescriptorSet m_GammaCorrectDescriptorSet = VK_NULL_HANDLE;
			VkDescriptorSet m_TAAResolveDescriptorSet = VK_NULL_HANDLE;
			VkDescriptorSet m_FinalFullscreenBlitDescriptorSet = VK_NULL_HANDLE;

			FrameBufferAttachment* m_SSAOFBColorAttachment0 = nullptr;
			FrameBufferAttachment* m_SSAOBlurHFBColorAttachment0 = nullptr;
			FrameBufferAttachment* m_SSAOBlurVFBColorAttachment0 = nullptr;

			FrameBufferAttachment* m_GBufferCubemapColorAttachment0 = nullptr;
			FrameBufferAttachment* m_GBufferCubemapColorAttachment1 = nullptr;
			FrameBufferAttachment* m_GBufferCubemapDepthAttachment = nullptr;

			VDeleter<VkImage> m_ShadowImage;
			VDeleter<VkDeviceMemory> m_ShadowImageMemory;
			VDeleter<VkImageView> m_ShadowImageView;
			VkFormat m_ShadowBufFormat = VK_FORMAT_UNDEFINED;
			VkDescriptorSet m_ShadowDescriptorSet = VK_NULL_HANDLE;
			Cascade* m_ShadowCascades[SHADOW_CASCADE_COUNT];

			std::map<FrameBufferAttachmentID, FrameBufferAttachment*> m_FrameBufferAttachments;

			Material::PushConstantBlock* m_SpritePerspPushConstBlock = nullptr;
			Material::PushConstantBlock* m_SpriteOrthoPushConstBlock = nullptr;
			Material::PushConstantBlock* m_SpriteOrthoArrPushConstBlock = nullptr;

			VulkanBuffer* m_FullScreenTriVertexBuffer = nullptr;

			struct SpriteDescSet
			{
				MaterialID materialID;
				VkDescriptorSet descSet;
				u32 textureLayer;
			};

			std::map<TextureID, SpriteDescSet> m_SpriteDescSets;

			Material::PushConstantBlock* m_CascadedShadowMapPushConstantBlock = nullptr;

			i32 m_DeferredQuadVertexBufferIndex = -1;

			glm::mat4 m_LastFrameViewProj;

			bool m_bPostInitialized = false;
			bool m_bSwapChainNeedsRebuilding = false;

			// TODO: Create other query pools
			VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;
			static const u64 MAX_TIMESTAMP_QUERIES = 1024;

			// Points from timestamp names to query indices. Index is negated on timestamp end to signify being ended.
			std::map<std::string, i32> m_TimestampQueryNames;

			static const u32 NUM_GPU_TIMINGS = 64;
			std::vector<std::array<real, NUM_GPU_TIMINGS>> m_TimestampHistograms;
			u32 m_TimestampHistogramIndex = 0;

			std::vector<const char*> m_ValidationLayers =
			{
				//"VK_LAYER_LUNARG_standard_validation",
				//"VK_LAYER_LUNARG_monitor", // FPS in title bar
				//"VK_LAYER_LUNARG_api_dump", // Log content
				//"VK_LAYER_LUNARG_screenshot",
				//"VK_LAYER_RENDERDOC_Capture", // RenderDoc captures, in engine integration works better (see COMPILE_RENDERDOC_API)
			};

			const std::vector<const char*> m_RequiredDeviceExtensions =
			{
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME,
				VK_KHR_MAINTENANCE1_EXTENSION_NAME, // For negative viewport height
			};

#ifdef SHIPPING
			const bool m_bEnableValidationLayers = false;
#else
			const bool m_bEnableValidationLayers = true;
#endif

			VkInstance m_Instance = VK_NULL_HANDLE;
			VkDebugReportCallbackEXT m_Callback = VK_NULL_HANDLE;
			VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

			VulkanDevice* m_VulkanDevice = nullptr;

			VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
			VkQueue m_PresentQueue = VK_NULL_HANDLE;

			VDeleter<VkSwapchainKHR> m_SwapChain;
			std::vector<VkImage> m_SwapChainImages;
			VkFormat m_SwapChainImageFormat = VK_FORMAT_UNDEFINED;
			VkExtent2D m_SwapChainExtent;
			std::vector<VDeleter<VkImageView>> m_SwapChainImageViews;
			std::vector<FrameBuffer*> m_SwapChainFramebuffers;
			std::vector<FrameBufferAttachment*> m_SwapChainFramebufferAttachments;
			FrameBufferAttachment* m_SwapChainDepthAttachment = nullptr;
			VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;

			VulkanRenderPass* m_ShadowRenderPass = nullptr;
			VulkanRenderPass* m_DeferredRenderPass = nullptr;
			VulkanRenderPass* m_DeferredCubemapRenderPass = nullptr;
			VulkanRenderPass* m_DeferredCombineRenderPass = nullptr;
			VulkanRenderPass* m_SSAORenderPass = nullptr;
			VulkanRenderPass* m_SSAOBlurHRenderPass = nullptr;
			VulkanRenderPass* m_SSAOBlurVRenderPass = nullptr;
			VulkanRenderPass* m_ForwardRenderPass = nullptr;
			VulkanRenderPass* m_PostProcessRenderPass = nullptr;
			VulkanRenderPass* m_GammaCorrectRenderPass = nullptr;
			VulkanRenderPass* m_TAAResolveRenderPass = nullptr;
			VulkanRenderPass* m_UIRenderPass = nullptr;
			// NOTE: Add new render passes to m_RenderPasses for automatic construction/clean up

			VulkanRenderPass** m_RenderPasses[12] = { &m_ShadowRenderPass, &m_DeferredRenderPass, &m_DeferredCubemapRenderPass, &m_DeferredCombineRenderPass, &m_SSAORenderPass, &m_SSAOBlurHRenderPass, &m_SSAOBlurVRenderPass,
				&m_ForwardRenderPass, &m_PostProcessRenderPass, &m_GammaCorrectRenderPass, &m_TAAResolveRenderPass, &m_UIRenderPass };
			std::vector<VulkanRenderPass*> m_AutoTransitionedRenderPasses;

			VDeleter<VkPipeline> m_ShadowGraphicsPipeline;
			VDeleter<VkPipelineLayout> m_ShadowPipelineLayout;

			VDeleter<VkPipeline> m_FontSSGraphicsPipeline;
			VDeleter<VkPipelineLayout> m_FontSSPipelineLayout;
			VDeleter<VkPipeline> m_FontWSGraphicsPipeline;
			VDeleter<VkPipelineLayout> m_FontWSPipelineLayout;

			VDeleter<VkPipeline> m_PostProcessGraphicsPipeline;
			VDeleter<VkPipelineLayout> m_PostProcessGraphicsPipelineLayout;
			VDeleter<VkPipeline> m_TAAResolveGraphicsPipeline;
			VDeleter<VkPipelineLayout> m_TAAResolveGraphicsPipelineLayout;
			VDeleter<VkPipeline> m_GammaCorrectGraphicsPipeline;
			VDeleter<VkPipelineLayout> m_GammaCorrectGraphicsPipelineLayout;

			VDeleter<VkPipeline> m_SpriteArrGraphicsPipeline;
			VDeleter<VkPipelineLayout> m_SpriteArrGraphicsPipelineLayout;

			VDeleter<VkPipeline> m_BlitGraphicsPipeline;
			VDeleter<VkPipelineLayout> m_BlitGraphicsPipelineLayout;

			VDeleter<VkPipelineLayout> m_ParticleGraphicsPipelineLayout;

			VDeleter<VkPipelineLayout> m_ParticleSimulationComputePipelineLayout;

			std::vector<VulkanParticleSystem*> m_ParticleSystems;

			VDeleter<VkDescriptorPool> m_DescriptorPool;
			std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;

			VulkanCommandBufferManager m_CommandBufferManager;
			std::vector<VulkanShader> m_Shaders;

			std::vector<VulkanTexture*> m_LoadedTextures;

			VulkanTexture* m_BlankTexture = nullptr;
			VulkanTexture* m_BlankTextureArr = nullptr;

			// Pair is: (stride, vertex buffer pair)
			// Indexed into through Shader::vertexBufferIndex
			// One element per unique stride
			// Uploaded to GPU once through staging buffer
			std::vector<std::pair<u32, VulkanBuffer*>> m_StaticVertexBuffers;
			VulkanBuffer* m_StaticIndexBuffer;

			// Pair is: (stride, vertex index buffer pair)
			// Indexed into through Material::dynamicVertexBufferIndex
			std::vector<std::pair<u32, VertexIndexBufferPair*>> m_DynamicVertexIndexBufferPairs;
			VertexIndexBufferPair* m_ShadowVertexIndexBufferPair = nullptr;

			u32 m_DynamicAlignment = 0;

			TextureID m_AlphaBGTextureID = InvalidTextureID;
			TextureID m_LoadingTextureID = InvalidTextureID;
			TextureID m_WorkTextureID = InvalidTextureID;

			TextureID m_PointLightIconID = InvalidTextureID;
			TextureID m_DirectionalLightIconID = InvalidTextureID;

			VDeleter<VkSemaphore> m_PresentCompleteSemaphore;
			VDeleter<VkSemaphore> m_RenderCompleteSemaphore;

			VkCommandBuffer m_OffScreenCmdBuffer = VK_NULL_HANDLE;
			VkSemaphore m_OffscreenSemaphore = VK_NULL_HANDLE;

			VkClearColorValue m_ClearColor;

			u32 m_CurrentSwapChainBufferIndex = 0;

			FT_Library m_FTLibrary;

			VulkanTexture* m_NoiseTexture = nullptr;
			ShaderID m_SSAOShaderID = InvalidShaderID;
			MaterialID m_SSAOMatID = InvalidMaterialID;
			ShaderID m_SSAOBlurShaderID = InvalidShaderID;
			MaterialID m_SSAOBlurMatID = InvalidMaterialID;
			VDeleter<VkPipeline> m_SSAOGraphicsPipeline;
			VDeleter<VkPipeline> m_SSAOBlurHGraphicsPipeline;
			VDeleter<VkPipeline> m_SSAOBlurVGraphicsPipeline;
			VDeleter<VkPipelineLayout> m_SSAOGraphicsPipelineLayout;
			VDeleter<VkPipelineLayout> m_SSAOBlurGraphicsPipelineLayout;
			VkDescriptorSet m_SSAODescSet = VK_NULL_HANDLE;
			VkDescriptorSet m_SSAOBlurHDescSet = VK_NULL_HANDLE;
			VkDescriptorSet m_SSAOBlurVDescSet = VK_NULL_HANDLE;

			// TODO: Create abstraction for specialization constants
			VkSpecializationMapEntry m_SSAOSpecializationMapEntry;
			VkSpecializationInfo m_SSAOSpecializationInfo;
			VkSpecializationMapEntry m_TAASpecializationMapEntry;
			VkSpecializationInfo m_TAAOSpecializationInfo;
			real m_TAA_ks[2];

#ifdef DEBUG
			AsyncVulkanShaderCompiler* m_ShaderCompiler = nullptr;
#endif

			enum DirtyFlags : u32
			{
				CLEAN			= 0,
				STATIC_DATA		= 1 << 0,
				DYNAMIC_DATA	= 1 << 1,
				SHADOW_DATA		= 1 << 2,

				MAX_VALUE		= 1 << 30,
				_NONE
			};

			u32 m_DirtyFlagBits = (u32)DirtyFlags::CLEAN;

			const FrameBufferAttachmentID SWAP_CHAIN_COLOR_ATTACHMENT_ID = 11000;
			const FrameBufferAttachmentID SWAP_CHAIN_DEPTH_ATTACHMENT_ID = 11001;
			const FrameBufferAttachmentID SHADOW_CASCADE_DEPTH_ATTACHMENT_ID = 22001;

			static std::array<glm::mat4, 6> s_CaptureViews;

			VulkanRenderer(const VulkanRenderer&) = delete;
			VulkanRenderer& operator=(const VulkanRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN