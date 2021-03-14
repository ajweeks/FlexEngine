#pragma once
#if COMPILE_VULKAN

#include "Graphics/Renderer.hpp"

#include <array>
#include <map>

#include "Callbacks/InputCallbacks.hpp"
#include "Pair.hpp"
#include "PoolAllocator.hpp"
#include "VDeleter.hpp"
#include "VulkanCommandBufferManager.hpp"
#include "VulkanHelpers.hpp"
#include "VulkanRenderPass.hpp"
#include "Window/Window.hpp"

namespace flex
{
	struct ShaderBatchPair;

	namespace vk
	{
		class VulkanPhysicsDebugDraw;
		struct VulkanBuffer;
		struct VulkanDevice;

		class VulkanRenderer final : public Renderer
		{
		public:
			VulkanRenderer();
			virtual ~VulkanRenderer();

			virtual void Initialize() override;
			virtual void PostInitialize() override;

			virtual void Destroy() override;

			virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace = InvalidMaterialID) override;
			virtual TextureID InitializeTextureFromFile(const std::string& relativeFilePath, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) override;
			virtual TextureID InitializeTextureFromMemory(void* data, u32 size, VkFormat inFormat, const std::string& name, u32 width, u32 height, u32 channelCount, VkFilter inFilter) override;
			virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(RenderID renderID) override;
			virtual void OnTextureDestroyed(TextureID textureID) override;

			virtual void ReplaceMaterialsOnObjects(MaterialID oldMatID, MaterialID newMatID) override;

			virtual void Update() override;
			virtual void Draw() override;
			virtual void DrawImGuiRendererInfo() override;
			virtual void DrawImGuiWindows() override;

			virtual void UpdateDynamicVertexData(RenderID renderID, VertexBufferData const* vertexBufferData, const std::vector<u32>& indexData) override;
			virtual void FreeDynamicVertexData(RenderID renderID) override;
			virtual void ShrinkDynamicVertexData(RenderID renderID, real minUnused = 0.0f) override;
			virtual u32 GetDynamicVertexBufferSize(RenderID renderID) override;
			virtual u32 GetDynamicVertexBufferUsedSize(RenderID renderID) override;

			virtual bool DrawImGuiShadersDropdown(i32* selectedShaderIndex, Shader** outSelectedShader = nullptr);
			virtual bool DrawImGuiShadersList(i32* selectedShaderIndex, bool bShowFilter, Shader** outSelectedShader = nullptr) override;
			virtual bool DrawImGuiTextureSelector(const char* label, const std::vector<std::string>& textureNames, i32* selectedIndex) override;
			virtual void DrawImGuiShaderErrors() override;
			virtual void DrawImGuiTexture(TextureID textureID, real texSize, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) override;
			virtual void DrawImGuiTexture(Texture* texture, real texSize, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) override;

			virtual void ClearShaderHash(const std::string& shaderName) override;
			virtual void RecompileShaders(bool bForce) override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColour(real r, real g, real b) override;

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

			virtual MaterialID GetRenderObjectMaterialID(RenderID renderID) override;

			virtual std::vector<Pair<std::string, MaterialID>> GetValidMaterialNames() const override;

			virtual bool DestroyRenderObject(RenderID renderID) override;

			virtual void SetGlobalUniform(u64 uniform, void* data, u32 dataSize) override;

			virtual void NewFrame() override;

			virtual PhysicsDebugDrawBase* GetDebugDrawer() override;

			virtual void DrawStringSS(const std::string& str,
				const glm::vec4& colour,
				AnchorPoint anchor,
				const glm::vec2& pos, // Positional offset from anchor
				real spacing,
				real scale = 1.0f) override;

			virtual void DrawStringWS(const std::string& str,
				const glm::vec4& colour,
				const glm::vec3& pos,
				const glm::quat& rot,
				real spacing,
				real scale = 1.0f) override;

			virtual void RecaptureReflectionProbe() override;

			virtual void RenderObjectMaterialChanged(MaterialID materialID) override;

			virtual void RenderObjectStateChanged() override;
			virtual void RecreateRenderObjectsWithMesh(const std::string& relativeMeshFilePath) override;

			virtual ParticleSystemID AddParticleSystem(const std::string& name, ParticleSystem* system, i32 particleCount) override;
			virtual bool RemoveParticleSystem(ParticleSystemID particleSystemID) override;

			virtual void RecreateEverything() override;

			virtual void ReloadObjectsWithMesh(const std::string& meshFilePath) override;

			virtual bool LoadFont(FontMetaData& fontMetaData, bool bForceRender) override;

			void RegisterFramebufferAttachment(FrameBufferAttachment* frameBufferAttachment);
			FrameBufferAttachment* GetFrameBufferAttachment(FrameBufferAttachmentID frameBufferAttachmentID) const;

			void GetCheckPointData();
			void PrintMemoryUsage();

			static void SetObjectName(VulkanDevice* device, u64 object, VkObjectType type, const char* name);
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

			static void BeginDebugMarkerRegion(VkCommandBuffer cmdBuf, const char* markerName, glm::vec4 colour = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
			static void EndDebugMarkerRegion(VkCommandBuffer cmdBuf, const char* markerName = nullptr); // markerName optional, useful for device check-pointing though

			static PFN_vkSetDebugUtilsObjectNameEXT m_vkSetDebugUtilsObjectNameEXT;
			static PFN_vkCmdBeginDebugUtilsLabelEXT m_vkCmdBeginDebugUtilsLabelEXT;
			static PFN_vkCmdEndDebugUtilsLabelEXT m_vkCmdEndDebugUtilsLabelEXT;
			static PFN_vkGetPhysicalDeviceMemoryProperties2 m_vkGetPhysicalDeviceMemoryProperties2;

		protected:
			virtual void InitializeShaders(const std::vector<ShaderInfo>& shaderInfos) override;
			virtual bool LoadShaderCode(ShaderID shaderID) override;
			virtual void FillOutGBufferFrameBufferAttachments(std::vector<Pair<std::string, void*>>& outVec) override;
			virtual void RecreateShadowFrameBuffers() override;

		private:
			friend VulkanPhysicsDebugDraw;
			friend VulkanRenderPass;
			friend VulkanDescriptorPool;

			void DestroyRenderObject(RenderID renderID, VulkanRenderObject* renderObject);

			bool InitializeFreeType();
			void DestroyFreeType();

			void GenerateCubemapFromHDR(VulkanRenderObject* renderObject, const std::string& environmentMapPath);
			void GenerateIrradianceSampler(VulkanRenderObject* renderObject);
			void GeneratePrefilteredCube(VulkanRenderObject* renderObject);
			void GenerateBRDFLUT();

			void CreateSSAOPipelines();
			void CreateSSAODescriptorSets();

			void CreateWireframeDescriptorSets();
			void DestroyWireframePipelines();

			RenderID GetNextAvailableRenderID() const;
			ParticleSystemID GetNextAvailableParticleSystemID() const;

			void InsertNewRenderObject(VulkanRenderObject* renderObject);
			void InsertNewParticleSystem(VulkanParticleSystem* particleSystem);

			void CreateInstance();
			void SetupDebugCallback();
			void CreateSurface();
			VkPhysicalDevice PickPhysicalDevice();
			void CreateSwapChain();
			void CreateSwapChainImageViews();
			void CreateRenderPasses();
			void CalculateAutoLayoutTransitions();

			void FillOutImageDescriptorInfos(ShaderUniformContainer<ImageDescriptorInfo>* imageDescriptors, MaterialID materialID);
			void FillOutBufferDescriptorInfos(ShaderUniformContainer<BufferDescriptorInfo>* descriptors, UniformBufferList const* uniformBufferList, ShaderID shaderID);

			void CreateDescriptorSets();

			void CreateGraphicsPipeline(RenderID renderID);
			void CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo, GraphicsPipelineID& outPipelineID);
			void DestroyAllGraphicsPipelines();
			void DestroyNonPersistentGraphicsPipelines();
			void DestroyGraphicsPipeline(GraphicsPipelineID pipelineID);
			bool IsGraphicsPipelineValid(GraphicsPipelineID pipelineID) const;
			GraphicsPipelineConfiguration* GetGraphicsPipeline(GraphicsPipelineID pipelineID) const;
			void CreateDepthResources();
			void CreateSwapChainFramebuffers();
			void CreateFrameBufferAttachments();
			void PrepareCubemapFrameBuffer();
			void PhysicsDebugRender();

			void CreateUniformBuffers(VulkanMaterial* material);
			void CreateStaticUniformBuffer(VulkanMaterial* material);
			void CreateDynamicUniformBuffer(VulkanMaterial* material);
			void CreateParticleUniformBuffer(VulkanMaterial* material);

			void CreatePostProcessingResources();
			void CreateFullscreenBlitResources();
			VkSpecializationInfo* GenerateSpecializationInfo(const std::vector<SpecializationConstantCreateInfo>& entries);
			void CreateComputeResources();
			void CreateParticleSystemResources(VulkanParticleSystem* particleSystem);

			// Creates all static vertex buffers that are marked as dirty
			void CreateStaticVertexBuffers();
			void CreateAllStaticVertexBuffers();

			// Creates all dynamic vertex/index buffer pairs that are marked as dirty
			void CreateDynamicVertexAndIndexBuffers();
			void CreateAllDynamicVertexAndIndexBuffers();

			// Creates the static index buffer used by all static geometry
			void CreateStaticIndexBuffer();

			void CreateShadowVertexBuffer();
			void CreateAndUploadToStaticVertexBuffer(VulkanBuffer* vertexBuffer, void* vertexBufferData, u32 vertexBufferSize);
			void CreateDynamicVertexBuffer(VulkanBuffer* vertexBuffer, u32 size);
			void CreateDynamicIndexBuffer(VulkanBuffer* indexBuffer, u32 size);

			void CreateShadowIndexBuffer();
			void CreateAndUploadToStaticIndexBuffer(VulkanBuffer* indexBuffer, const std::vector<u32>& indices);

			u32 AllocateDynamicUniformBuffer(u32 bufferUnitSize, void** data, i32 maxObjectCount = -1);
			void PrepareUniformBuffer(VulkanBuffer* buffer, u32 bufferSize,
				VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits, bool bMap = true);

			void CreateSemaphores();

			void FillOutShaderBatches(const std::vector<RenderID>& renderIDs, i32* inOutDynamicUBOOffset,
				MaterialBatchPair& matBatchPair, MaterialBatchPair& depthAwareEditorMatBatchPair, MaterialBatchPair& depthUnawareEditorMatBatchPair,
				MaterialID matID, bool bWriteUBOOffsets = true);
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

			void BeginDebugMarkerRegionInternal(VkCommandBuffer cmdBuf, const char* markerName, const glm::vec4& colour);
			void EndDebugMarkerRegionInternal(VkCommandBuffer cmdBuf, const char* markerName);

			void SetCheckPoint(VkCommandBuffer cmdBuf, const char* checkPointName);

			bool CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule) const;
			VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
			VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
			VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
			VulkanSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
			bool IsDeviceSuitable(VkPhysicalDevice device);
			std::vector<const char*> GetRequiredInstanceExtensions() const;
			bool CheckValidationLayerSupport() const;

			void UpdateConstantUniformBuffers(UniformOverrides const* overridenUniforms = nullptr);
			void UpdateDynamicUniformBuffer(RenderID renderID, UniformOverrides const* overridenUniforms = nullptr,
				MaterialID materialIDOverride = InvalidMaterialID, u32 dynamicUBOOffsetOverride = InvalidID);
			void UpdateDynamicUniformBuffer(MaterialID materialID, u32 dynamicOffsetIndex, const glm::mat4& model, UniformOverrides const* uniformOverrides = nullptr);

			void CreateFontGraphicsPipelines();

			void GenerateIrradianceMaps();

			void SetLineWidthForCmdBuffer(VkCommandBuffer cmdBuffer, real requestedWidth = 3.0f);

			void ImGuiUpdateTextureIndexOrMaterial(bool bUpdateTextureMaterial,
				const std::string& texturePath,
				std::string& matTexturePath,
				VulkanTexture* texture,
				i32 i,
				i32* textureIndex,
				VulkanTexture** textureToUpdate);

			void BeginGPUTimeStamp(VkCommandBuffer commandBuffer, const std::string& name);
			void EndGPUTimeStamp(VkCommandBuffer commandBuffer, const std::string& name);
			ms GetDurationBetweenTimeStamps(const std::string& name);

			static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
				VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
				VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
				const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
				void* pUserData);

			inline VulkanRenderObject* GetRenderObject(RenderID renderID)
			{
				return m_RenderObjects[renderID];
			}

			u32 GetActiveRenderObjectCount() const;

			u32 GetAlignedUBOSize(u32 unalignedSize);

			void DrawText(VkCommandBuffer commandBuffer, bool bScreenSpace);
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

			void UpdateShaderMaxObjectCount(ShaderID shaderID, i32 newMax);

			const u32 MAX_NUM_RENDER_OBJECTS = 4096; // TODO: Not this?
			std::vector<VulkanRenderObject*> m_RenderObjects;

			glm::vec2i m_CubemapFramebufferSize;
			glm::vec2i m_BRDFSize;
			VulkanTexture* m_BRDFTexture = nullptr;
			bool bRenderedBRDFLUT = false;

			FrameBufferAttachment* m_GBufferColourAttachment0 = nullptr;
			FrameBufferAttachment* m_GBufferColourAttachment1 = nullptr;
			FrameBufferAttachment* m_GBufferDepthAttachment = nullptr;

			VDeleter<VkSampler> m_LinMipLinSampler;
			VDeleter<VkSampler> m_DepthSampler;
			VDeleter<VkSampler> m_NearestClampEdgeSampler;

			VkFormat m_OffscreenFrameBufferFormat = VK_FORMAT_UNDEFINED;
			FrameBufferAttachment* m_OffscreenFB0ColourAttachment0 = nullptr;
			FrameBufferAttachment* m_OffscreenFB1ColourAttachment0 = nullptr;

			FrameBufferAttachment* m_OffscreenFB0DepthAttachment = nullptr;
			FrameBufferAttachment* m_OffscreenFB1DepthAttachment = nullptr;

			VulkanTexture* m_HistoryBuffer = nullptr;

			VkDescriptorSet m_PostProcessDescriptorSet = VK_NULL_HANDLE;
			VkDescriptorSet m_GammaCorrectDescriptorSet = VK_NULL_HANDLE;
			VkDescriptorSet m_TAAResolveDescriptorSet = VK_NULL_HANDLE;
			VkDescriptorSet m_FinalFullscreenBlitDescriptorSet = VK_NULL_HANDLE;

			FrameBufferAttachment* m_SSAOFBColourAttachment0 = nullptr;
			FrameBufferAttachment* m_SSAOBlurHFBColourAttachment0 = nullptr;
			FrameBufferAttachment* m_SSAOBlurVFBColourAttachment0 = nullptr;

			FrameBufferAttachment* m_GBufferCubemapColourAttachment0 = nullptr;
			FrameBufferAttachment* m_GBufferCubemapColourAttachment1 = nullptr;
			FrameBufferAttachment* m_GBufferCubemapDepthAttachment = nullptr;

			VDeleter<VkImage> m_ShadowImage;
			VDeleter<VkDeviceMemory> m_ShadowImageMemory;
			VDeleter<VkImageView> m_ShadowImageView;
			VkFormat m_ShadowBufFormat = VK_FORMAT_UNDEFINED;
			VkDescriptorSet m_ShadowDescriptorSet = VK_NULL_HANDLE;
			std::vector<Cascade*> m_ShadowCascades;

			std::map<FrameBufferAttachmentID, FrameBufferAttachment*> m_FrameBufferAttachments;

			VulkanBuffer* m_FullScreenTriVertexBuffer = nullptr;

			struct SpriteDescSet
			{
				MaterialID materialID;
				VkDescriptorSet descSet;
				u32 textureLayer;
			};

			std::map<TextureID, SpriteDescSet> m_SpriteDescSets;

			std::map<u64, Pair<void*, u32>> m_GlobalUserUniforms;

			// TODO: Create other query pools
			VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;
			static const u64 MAX_TIMESTAMP_QUERIES = 1024;

			// Points from timestamp names to query indices. Index is negated on timestamp end to signify being ended.
			std::map<std::string, i32> m_TimestampQueryNames;

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

			const std::vector<const char*> m_RequiredInstanceExtensions =
			{
			};

			const std::vector<const char*> m_OptionalInstanceExtensions =
			{
				VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
				VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
			};

			const std::vector<const char*> m_OptionalDeviceExtensions =
			{
				VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
				VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME,
				VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
			};

			std::vector<VkExtensionProperties> m_SupportedInstanceExtensions;
			std::vector<const char*> m_EnabledInstanceExtensions;

			bool m_bDiagnosticCheckpointsEnabled = false;
			bool m_bMemoryBudgetExtensionEnabled = false;

#ifdef RELEASE
			const bool m_bEnableValidationLayers = false;
			const bool m_bEnableGPUAssistanceValidationFeature = false;
			const bool m_bEnableBestPracticesValidationFeature = false;
#else
			const bool m_bEnableValidationLayers = true;
			const bool m_bEnableGPUAssistanceValidationFeature = true;
			const bool m_bEnableBestPracticesValidationFeature = true;
#endif

			bool m_bShaderErrorWindowShowing = true;

			VkInstance m_Instance = VK_NULL_HANDLE;
			VkDebugUtilsMessengerEXT m_DebugUtilsMessengerCallback = VK_NULL_HANDLE;
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

			GraphicsPipelineID m_ShadowGraphicsPipelineID = InvalidGraphicsPipelineID;

			GraphicsPipelineID m_FontSSGraphicsPipelineID = InvalidGraphicsPipelineID;
			GraphicsPipelineID m_FontWSGraphicsPipelineID = InvalidGraphicsPipelineID;

			GraphicsPipelineID m_PostProcessGraphicsPipelineID = InvalidGraphicsPipelineID;
			GraphicsPipelineID m_TAAResolveGraphicsPipelineID = InvalidGraphicsPipelineID;
			GraphicsPipelineID m_GammaCorrectGraphicsPipelineID = InvalidGraphicsPipelineID;

			GraphicsPipelineID m_SpriteArrGraphicsPipelineID = InvalidGraphicsPipelineID;

			GraphicsPipelineID m_BlitGraphicsPipelineID = InvalidGraphicsPipelineID;

			VDeleter<VkPipelineLayout> m_ParticleSimulationComputePipelineLayout;

			std::map<u64, GraphicsPipelineID> m_GraphicsPipelineHashes;
			std::map<GraphicsPipelineID, GraphicsPipelineConfiguration*> m_GraphicsPipelines;
			u32 m_LastGraphicsPipelineID = 0; // Monotonically increasing value to give unique IDs to pipelines

			//std::map<u64, GraphicsPipelineID> m_DescriptorSetsHashes;
			u32 m_LastDescriptorSetID = 0; // Monotonically increasing value to give unique IDs to descriptor sets

			// TODO: Make RenderAPI-agnostic and move to resource manager
			std::vector<VulkanParticleSystem*> m_ParticleSystems;

			VulkanDescriptorPool* m_DescriptorPoolPersistent = nullptr; // Persistent across scene loads (only destroyed at application quit)
			VulkanDescriptorPool* m_DescriptorPool = nullptr; // Non-persistent pool (destroyed on scene change)

			VulkanCommandBufferManager m_CommandBufferManager;

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

			VDeleter<VkSemaphore> m_PresentCompleteSemaphore;
			VDeleter<VkSemaphore> m_RenderCompleteSemaphore;

			VkCommandBuffer m_OffScreenCmdBuffer = VK_NULL_HANDLE;
			VkSemaphore m_OffscreenSemaphore = VK_NULL_HANDLE;

			VkClearColorValue m_ClearColour;

			u32 m_CurrentSwapChainBufferIndex = 0;


			GraphicsPipelineID m_SSAOGraphicsPipelineID = InvalidGraphicsPipelineID;
			GraphicsPipelineID m_SSAOBlurHGraphicsPipelineID = InvalidGraphicsPipelineID;
			GraphicsPipelineID m_SSAOBlurVGraphicsPipelineID = InvalidGraphicsPipelineID;
			VkDescriptorSet m_SSAODescSet = VK_NULL_HANDLE;
			VkDescriptorSet m_SSAOBlurHDescSet = VK_NULL_HANDLE;
			VkDescriptorSet m_SSAOBlurVDescSet = VK_NULL_HANDLE;

			// Maps vertex attributes to pipeline
			std::map<VertexAttributes, GraphicsPipelineConfiguration*> m_WireframeGraphicsPipelines;
			VDeleter<VkPipelineLayout> m_WireframePipelineLayout;
			VkDescriptorSet m_WireframeDescSet = VK_NULL_HANDLE;

			PoolAllocator<DeviceDiagnosticCheckpoint, 32> m_CheckPointAllocator;

#if COMPILE_SHADER_COMPILER
			struct VulkanShaderCompiler* m_ShaderCompiler = nullptr;
#endif

			const FrameBufferAttachmentID SWAP_CHAIN_COLOUR_ATTACHMENT_ID = 11000;
			const FrameBufferAttachmentID SWAP_CHAIN_DEPTH_ATTACHMENT_ID = 11001;
			const FrameBufferAttachmentID SHADOW_CASCADE_DEPTH_ATTACHMENT_ID = 22001;

			VulkanRenderer(const VulkanRenderer&) = delete;
			VulkanRenderer& operator=(const VulkanRenderer&) = delete;
		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN
