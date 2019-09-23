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
			struct ShaderBatchPair;

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

			virtual void ClearMaterials(bool bDestroyPersistentMats = false) override;

			virtual void Update() override;
			virtual void Draw() override;
			virtual void DrawImGuiWindows() override;

			virtual void UpdateVertexData(RenderID renderID, VertexBufferData const* vertexBufferData) override;

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
			virtual u32 GetTextureHandle(TextureID textureID) const override;
			virtual void RenderObjectStateChanged() override;

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

			void DestroyRenderObject(RenderID renderID, VulkanRenderObject* renderObject);

			VkPhysicalDeviceFeatures GetEnabledFeaturesForDevice(VkPhysicalDevice physicalDevice);

			typedef void (VulkanTexture::*VulkanTextureCreateFunction)(VkQueue graphicsQueue, const std::string&, VkFormat, u32);

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
				SHCoeffs shCoeffs;
			};

			void GenerateCubemapFromHDR(VulkanRenderObject* renderObject, const std::string& environmentMapPath);
			void GenerateIrradianceSampler(VulkanRenderObject* renderObject);
			void GeneratePrefilteredCube(VulkanRenderObject* renderObject);
			void GenerateBRDFLUT();

			void CaptureSceneToCubemap(RenderID cubemapRenderID);
			void GeneratePrefilteredMapFromCubemap(MaterialID cubemapMaterialID);
			void GenerateIrradianceSamplerFromCubemap(MaterialID cubemapMaterialID);

			void CreateSSAOPipelines();
			void CreateSSAODescriptorSets();

			void CreateRenderPass(VkRenderPass* outPass, VkFormat colorFormat, const char* passName,
				VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				bool bDepth = false,
				VkFormat depthFormat = VK_FORMAT_UNDEFINED,
				VkImageLayout finalDepthLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VkImageLayout initialDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			MaterialID GetNextAvailableMaterialID();
			RenderID GetNextAvailableRenderID() const;

			void InsertNewRenderObject(VulkanRenderObject* renderObject);
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
			void CreateDescriptorSetLayout(ShaderID shaderID);
			void CreateDescriptorSet(RenderID renderID);
			void CreateDescriptorSet(DescriptorSetCreateInfo* createInfo);
			void CreateGraphicsPipeline(RenderID renderID, bool bSetCubemapRenderPass);
			void CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo);
			void CreateDepthResources();
			void CreateFramebuffers();
			void PrepareFrameBuffers();
			void PrepareCubemapFrameBuffer();
			void PhysicsDebugRender();

			void CreateUniformBuffers(VulkanShader* shader);

			void CreatePostProcessingObjects();

			// Returns a pointer into m_LoadedTextures if a texture has been loaded from that file path, otherwise returns nullptr
			VulkanTexture* GetLoadedTexture(const std::string& filePath);
			bool RemoveLoadedTexture(VulkanTexture* texture, bool bDestroy);
			
			void CreateStaticVertexBuffers();
			void CreateDynamicVertexBuffers();

			u32 CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, ShaderID shaderID, u32 size);
			void CreateShadowVertexBuffer();
			void CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, void* vertexBufferData, u32 vertexBufferSize);
			void CreateDynamicVertexBuffer(VulkanBuffer* vertexBuffer, u32 size);

			void CreateStaticIndexBuffers();

			// Creates index buffer for all render objects' indices which use specified shader index. Returns index count
			u32 CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, ShaderID shaderID);
			void CreateShadowIndexBuffer();
			void CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, const std::vector<u32>& indices);

			void CreateDescriptorPool();
			u32 AllocateDynamicUniformBuffer(u32 dynamicDataSize, void** data, i32 maxObjectCount = -1);
			void PrepareUniformBuffer(VulkanBuffer* buffer, u32 bufferSize,
				VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits);

			void CreateSemaphores();

			void BatchRenderObjects();
			void DrawShaderBatch(const ShaderBatchPair &shaderBatches, VkCommandBuffer& commandBuffer, DrawCallInfo* drawCallInfo = nullptr);

			void RenderFullscreenQuad(VkCommandBuffer commandBuffer, VkRenderPass renderPass, VkFramebuffer framebuffer, ShaderID shaderID,
				VkPipelineLayout pipelineLayout, VkPipeline graphicsPipeline, VkDescriptorSet descriptorSet, bool bFlipViewport);

			void BuildCommandBuffers(const DrawCallInfo& drawCallInfo);

			void DrawFrame();

			void BindDescriptorSet(VulkanShader* shader, u32 dynamicOffsetOffset, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet);
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
			void UpdateDynamicUniformBuffer(RenderID renderID, UniformOverrides const * overridenUniforms = nullptr,
				MaterialID materialIDOverride = InvalidMaterialID, u32 dynamicUBOOffsetOverride = InvalidID);
			void UpdateDynamicUniformBuffer(MaterialID materialID, u32 dynamicOffsetIndex, const glm::mat4& model, UniformOverrides const* uniformOverrides = nullptr);

			void GenerateIrradianceMaps();

			void OnShaderReloadSuccess();

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

			VulkanRenderObject* GetRenderObject(RenderID renderID);

			u32 GetActiveRenderObjectCount() const;

			u32 GetAlignedUBOSize(u32 unalignedSize);

			void DrawTextSS(VkCommandBuffer commandBuffer);
			void DrawTextWS(VkCommandBuffer commandBuffer);
			void DrawSpriteBatch(const std::vector<SpriteQuadDrawInfo>& batch, VkCommandBuffer commandBuffer);

			VkRenderPass ResolveRenderPassType(RenderPassType renderPassType, const char* shaderName = nullptr);

			void CreateShadowResources();
			VkDescriptorSet CreateSpriteDescSet(ShaderID spriteShaderID, TextureID textureID, u32 layer = 0);

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

			FrameBuffer* m_GBufferFrameBuf = nullptr;
			FrameBufferAttachment* m_GBufferDepthAttachment = nullptr;
			VDeleter<VkSampler> m_ColorSampler;
			VDeleter<VkSampler> m_DepthSampler;

			VkFormat m_OffscreenFrameBufferFormat = VK_FORMAT_UNDEFINED;
			FrameBuffer* m_OffscreenFrameBuffer0 = nullptr;
			FrameBuffer* m_OffscreenFrameBuffer1 = nullptr;

			FrameBufferAttachment* m_OffscreenDepthAttachment0 = nullptr;
			FrameBufferAttachment* m_OffscreenDepthAttachment1 = nullptr;

			VulkanTexture* m_HistoryBuffer = nullptr;

			VkDescriptorSet m_PostProcessDescriptorSet = VK_NULL_HANDLE;
			VkDescriptorSet m_TAAResolveDescriptorSet = VK_NULL_HANDLE;
			VkDescriptorSet m_GammaCorrectDescriptorSet = VK_NULL_HANDLE;

			FrameBuffer* m_SSAOFrameBuf = nullptr;
			FrameBuffer* m_SSAOBlurHFrameBuf = nullptr;
			FrameBuffer* m_SSAOBlurVFrameBuf = nullptr;

			FrameBuffer* m_GBufferCubemapFrameBuffer = nullptr;
			FrameBufferAttachment* m_CubemapDepthAttachment = nullptr;

			VDeleter<VkImage> m_ShadowImage;
			VDeleter<VkDeviceMemory> m_ShadowImageMemory;
			VDeleter<VkImageView> m_ShadowImageView;
			VkFormat m_ShadowBufFormat = VK_FORMAT_UNDEFINED;
			VkDescriptorSet m_ShadowDescriptorSet = VK_NULL_HANDLE;
			Cascade* m_ShadowCascades[NUM_SHADOW_CASCADES];

			Material::PushConstantBlock* m_SpritePerspPushConstBlock = nullptr;
			Material::PushConstantBlock* m_SpriteOrthoPushConstBlock = nullptr;
			Material::PushConstantBlock* m_SpriteOrthoArrPushConstBlock = nullptr;

			VulkanBuffer* m_FullScreenTriVertexBuffer = nullptr;

			struct SpriteDescSet
			{
				ShaderID shaderID;
				VkDescriptorSet descSet;
				u32 textureLayer;
			};

			std::map<TextureID, SpriteDescSet> m_SpriteDescSets;

			i32 m_DeferredQuadVertexBufferIndex = -1;

			glm::mat4 m_LastFrameViewProj;

			bool m_bPostInitialized = false;
			bool m_bSwapChainNeedsRebuilding = false;

			// TODO: Create other query pools
			VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;
			static const u64 MAX_TIMESTAMP_QUERIES = 1024;

			// Points from timestamp names to query indices. Index is negated on timestamp end to signify being ended.
			std::map<std::string, i32> m_TimestampQueryNames;
			std::vector<ms> m_TimestampQueryDurations;

			static const u32 NUM_GPU_TIMINGS = 64;
			std::vector<std::array<real, NUM_GPU_TIMINGS>> m_TimestampHistograms;
			u32 m_TimestampHistogramIndex = 0;

			std::vector<const char*> m_ValidationLayers =
			{
				"VK_LAYER_LUNARG_standard_validation",
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
			FrameBufferAttachment* m_SwapChainDepthAttachment = nullptr;

			MaterialID m_ComputeSDFMatID = InvalidMaterialID;

			VDeleter<VkRenderPass> m_ShadowRenderPass;
			VDeleter<VkRenderPass> m_DeferredCombineRenderPass;
			VDeleter<VkRenderPass> m_SSAORenderPass;
			VDeleter<VkRenderPass> m_SSAOBlurHRenderPass;
			VDeleter<VkRenderPass> m_SSAOBlurVRenderPass;
			VDeleter<VkRenderPass> m_ForwardRenderPass;
			VDeleter<VkRenderPass> m_PostProcessRenderPass;
			VDeleter<VkRenderPass> m_TAAResolveRenderPass;
			VDeleter<VkRenderPass> m_UIRenderPass;
			VDeleter<VkRenderPass> m_GammaCorrectRenderPass;

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

			VDeleter<VkDescriptorPool> m_DescriptorPool;
			std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;

			VulkanCommandBufferManager m_CommandBufferManager;
			std::vector<VulkanShader> m_Shaders;

			std::vector<VulkanTexture*> m_LoadedTextures;

			VulkanTexture* m_BlankTexture = nullptr;
			VulkanTexture* m_BlankTextureArr = nullptr;


			std::vector<VertexIndexBufferPair> m_VertexIndexBufferPairs;

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
			VDeleter<VkSampler> m_SSAOSampler;

			// TODO: Create abstraction for specialization constants
			VkSpecializationMapEntry m_SSAOSpecializationMapEntry;
			VkSpecializationInfo m_SSAOSpecializationInfo;
			VkSpecializationMapEntry m_TAASpecializationMapEntry;
			VkSpecializationInfo m_TAAOSpecializationInfo;
			real m_TAA_ks[2];

			VDeleter<VkPipeline> m_SpriteArrGraphicsPipeline;
			VDeleter<VkPipelineLayout> m_SpriteArrGraphicsPipelineLayout;

#ifdef DEBUG
			AsyncVulkanShaderCompiler* m_ShaderCompiler = nullptr;
#endif

			static std::array<glm::mat4, 6> s_CaptureViews;

			VulkanRenderer(const VulkanRenderer&) = delete;
			VulkanRenderer& operator=(const VulkanRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN