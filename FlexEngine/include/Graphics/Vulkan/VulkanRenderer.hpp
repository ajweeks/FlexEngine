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
	class DebugRenderer;

	namespace vk
	{
		struct VulkanBuffer;
		struct VulkanDevice;

		class VulkanRenderer final : public Renderer
		{
		public:
			VulkanRenderer();
			virtual ~VulkanRenderer();

			VulkanRenderer(const VulkanRenderer&) = delete;
			VulkanRenderer(VulkanRenderer&&) = delete;
			VulkanRenderer& operator=(const VulkanRenderer&) = delete;
			VulkanRenderer& operator=(VulkanRenderer&&) = delete;

			virtual void Initialize() override;
			// Loads Vulkan & creates the instance on a background thread, overlapping with window creation (call before
			// Initialize, which waits for it). Only supported on Windows, where the instance extensions needed for
			// window surfaces are known before GLFW is initialized.
			void BeginCreateInstance(const std::string& applicationName);
			virtual void PostInitialize() override;

			virtual void Destroy() override;

			virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace = InvalidMaterialID) override;
			virtual TextureID InitializeTextureFromFile(const std::string& relativeFilePath,
				HTextureSampler inSampler,
				bool bFlipVertically,
				bool bGenerateMipMaps,
				bool bHDR,
				TextureID existingTextureID = InvalidTextureID) override;
			virtual TextureID InitializeLoadedTexture(TextureID textureID) override;
			virtual TextureID InitializeTextureFromMemory(void* data, u32 size, TextureFormat inFormat, const std::string& name, u32 width, u32 height, u32 channelCount, HTextureSampler inSampler, VkFilter inFilter) override;
			virtual TextureID InitializeTextureArrayFromMemory(void* data, u32 size, TextureFormat inFormat, const std::string& name, u32 width, u32 height, u32 layerCount, u32 channelCount, HTextureSampler inSampler) override;
			virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(RenderID renderID) override;
			virtual void OnTextureDestroyed(TextureID textureID, Texture* texture) override;

			virtual void ReplaceMaterialsOnObjects(MaterialID oldMatID, MaterialID newMatID) override;

			virtual void Update() override;
			virtual void Draw() override;
			virtual void DrawImGuiRendererInfo() override;
			virtual void DrawImGuiWindows() override;

			virtual void UpdateDynamicVertexData(RenderID renderID, VertexBufferData const* vertexBufferData, const std::vector<u32>& indexData) override;
			virtual void FreeDynamicVertexData(RenderID renderID) override;
			virtual void ShrinkDynamicVertexData(RenderID renderID, real minUnused = 0.0f) override;
			virtual void ReserveDynamicVertexData(MaterialID materialID, real usedFactor) override;
			virtual u32 GetDynamicVertexBufferSize(RenderID renderID) override;
			virtual u32 GetDynamicVertexBufferUsedSize(RenderID renderID) override;

			virtual void DrawImGuiTexture(TextureID textureID, real texSize, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) override;
			virtual void DrawImGuiTexture(Texture* texture, real texSize, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) override;

			virtual void OnWindowSizeChanged(i32 width, i32 height) override;

			virtual void OnPreSceneChange() override;
			virtual void OnPostSceneChange() override;

			virtual void OnSettingsReloaded() override;

			virtual bool GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo) override;

			virtual void SetVSyncEnabled(bool bEnableVSync) override;

			virtual u32 GetRenderObjectCount() const override;
			virtual u32 GetRenderObjectCapacity() const override;

			virtual void SetSkyboxMesh(Mesh* skyboxMesh) override;
			virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) override;
			virtual void SetRenderObjectHidden(RenderID renderID, bool bHidden) override;

			virtual MaterialID GetRenderObjectMaterialID(RenderID renderID) override;

			virtual std::vector<Pair<std::string, MaterialID>> GetValidMaterialNames(bool bEditorMaterials) const override;

			virtual bool DestroyRenderObject(RenderID renderID) override;

			virtual void SetGlobalUniform(Uniform const* uniform, void* data, u32 dataSize) override;
			virtual void ClearGlobalUniform(Uniform const* uniform, void* data) override;
			virtual void AddRenderObjectUniformOverride(RenderID renderID, Uniform const* uniform, const MaterialPropertyOverride& propertyOverride) override;

			virtual void NewFrame() override;

			virtual DebugRenderer* GetDebugRenderer() override;

			VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }

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

			virtual ParticleSystemID AddParticleSystem(const std::string& name, ParticleSystem* system) override;
			virtual bool RemoveParticleSystem(ParticleSystemID particleSystemID) override;
			virtual bool AddParticleEmitterInstance(ParticleSystemID particleSystemID, ParticleEmitterID emitterID) override;
			virtual void RemoveParticleEmitterInstance(ParticleSystemID particleSystemID, ParticleEmitterID emitterID) override;
			virtual void OnParticleSystemTemplateUpdated(StringID particleTemplateNameSID) override;

			virtual void RecreateEverything() override;

			virtual void ReloadObjectsWithMesh(const std::string& meshFilePath) override;

			virtual void InitializeTerrain(MaterialID terrainMaterialID, TextureID randomTablesTextureID, const TerrainGenConstantData& constantData, u32 initialMaxChunkCount) override;
			virtual void RegenerateTerrain(const TerrainGenConstantData& constantData, u32 maxChunkCount) override;
			virtual void RegisterTerrainChunk(const glm::ivec3& chunkIndex, u32 linearIndex) override;
			virtual void RemoveTerrainChunk(const glm::ivec3& chunkIndex) override;
			virtual u32 GetCurrentTerrainChunkCapacity() const;
			virtual u32 GetChunkVertCount(u32 chunkLinearIndex) const override;
			virtual void SetChunkVertCount(u32 chunkLinearIndex, u32 count) override;

			virtual bool LoadFont(FontMetaData& fontMetaData, bool bForceRender) override;

			virtual void OnTextureReloaded(Texture* texture) override;

			virtual Texture* CreateTexture(const std::string& textureName) override;

			virtual HTextureSampler GetSamplerLinearRepeat() override;
			virtual HTextureSampler GetSamplerLinearClampToEdge() override;
			virtual HTextureSampler GetSamplerLinearClampToBorder() override;
			virtual HTextureSampler GetSamplerNearestClampToEdge() override;

			virtual GPUBufferID RegisterGPUBuffer(GPUBuffer* uniformBuffer) override;
			virtual void UnregisterGPUBuffer(GPUBufferID bufferID) override;
			virtual GPUBuffer* GetGPUBuffer(GPUBufferID bufferID) override;
			virtual void UploadDataViaStagingBuffer(GPUBufferID bufferID, u32 bufferOffset, void* data, u32 dataSize) override;

			virtual void FreeGPUBuffer(GPUBuffer* buffer) override;
			virtual GPUBuffer* AllocateGPUBuffer(GPUBufferType type, const std::string& debugName) override;

			virtual void CreateGPUBuffer(GPUBuffer* buffer,
				u32 bufferSize,
				VkBufferUsageFlags bufferUseageFlagBits,
				VkMemoryPropertyFlags memoryPropertyHostFlagBits,
				bool bMap = true) override;

			virtual void SetGPUBufferName(GPUBuffer const* buffer, const char* name) override;

			virtual u32 GetNonCoherentAtomSize() const override;

			VulkanDevice* GetDevice();

			void RegisterFramebufferAttachment(FrameBufferAttachment* frameBufferAttachment);
			FrameBufferAttachment* GetFrameBufferAttachment(FrameBufferAttachmentID frameBufferAttachmentID) const;

			void GetCheckPointData();
			void PrintMemoryUsage();

			static void SetObjectName(VulkanDevice* device, u64 object, VkObjectType type, const char* name);
			static void SetCommandBufferName(VulkanDevice* device, VkCommandBuffer commandBuffer, const char* name);
			static void SetSwapchainName(VulkanDevice* device, VkSwapchainKHR swapchain, const char* name);
			static void SetDescriptorSetName(VulkanDevice* device, VkDescriptorSet descSet, const char* name);
			static void SetDescriptorSetLayoutName(VulkanDevice* device, VkDescriptorSetLayout descSetLayout, const char* name);
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

			virtual u32 GetStaticVertexIndexBufferIndex(u32 stride) override;
			virtual u32 GetDynamicVertexIndexBufferIndex(u32 stride, bool bDeviceLocal = false) override;

		private:
			friend DebugRenderer;
			friend VulkanRenderPass;
			friend VulkanDescriptorPool;

			void DestroyRenderObject(RenderID renderID, VulkanRenderObject* renderObject);

			void GenerateCubemapFromHDR(VulkanRenderObject* renderObject, const std::string& environmentMapPath);
			void GenerateIrradianceSampler(VulkanRenderObject* renderObject);
			void GeneratePrefilteredCube(VulkanRenderObject* renderObject);
			void GenerateBRDFLUT();

			void CreateSSAOMaterials();
			void CreateSSAOPipelines();
			void CreateSSAODescriptorSets();

			void CreateWireframeDescriptorSets();

			RenderID GetNextAvailableRenderID() const;
			ParticleSystemID GetNextAvailableParticleSystemID() const;

			void InsertNewRenderObject(VulkanRenderObject* renderObject);
			void InsertNewParticleSystem(VulkanParticleSystem* particleSystem);

			bool CreateInstance();
			bool LoadVulkanAndCreateInstance();
			// Creates m_VulkanDevice
			void CreateLogicalDevice(VkPhysicalDevice physicalDevice);
			std::future<bool> m_InstanceFuture;
			std::string m_ApplicationName;
			void SetupDebugCallback();
			void CreateSurface();
			VkPhysicalDevice PickPhysicalDevice();
			void CreateSwapChain();
			void CreateSwapChainImageViews();
			void CreateRenderPasses();
			void CalculateAutoLayoutTransitions();

			void CreateSpecialzationInfos();

			void FillOutTextureDescriptorInfos(ShaderUniformContainer<ImageDescriptorInfo>* imageDescriptors, MaterialID materialID);
			void FillOutBufferDescriptorInfos(ShaderUniformContainer<BufferDescriptorInfo>* descriptors, GPUBufferList const* gpuBufferList, ShaderID shaderID);

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
			void CreateSamplers();

			// Pipeline cache data is saved to disk on shutdown to speed up pipeline creation on subsequent runs
			void CreatePipelineCache();
			void SaveAndDestroyPipelineCache();
			std::string GetPipelineCacheFilePath() const;
			void PhysicsDebugRender();
			// Draws only the debug renderer's lines from the editor object batches
			void DrawDebugLinesOnly(VkCommandBuffer& commandBuffer);
			// Screenshots copy the frame's swap chain image into a staging buffer (as part of the frame), then save it
			// once the frame has finished
			void RecordScreenshotCopy(VkCommandBuffer commandBuffer, u32 swapChainImageIndex);
			void SaveScreenshot();
			VulkanBuffer* m_ScreenshotStagingBuffer = nullptr;
			VkExtent2D m_ScreenshotExtent = {};

			void CreateUniformBuffers(VulkanMaterial* material);
			void CreateStaticUniformBuffer(VulkanMaterial* material);
			void CreateDynamicUniformBuffer(VulkanMaterial* material);

			void CreateTerrainBuffers();

			void CreatePostProcessingResources();
			void CreateFullscreenBlitResources();
			VkSpecializationInfo* GenerateSpecializationInfo(const std::vector<SpecializationConstantCreateInfo>& entries);
			void CreateComputeResources();
			void CreateParticleSystemResources(VulkanParticleSystem* particleSystem);

			void CreateTerrainSystemResources();

			// Creates all static vertex buffers that are marked as dirty
			void CreateStaticVertexBuffers();
			void CreateAllStaticVertexBuffers();

			// Creates all dynamic vertex/index buffer pairs that are marked as dirty
			void CreateDynamicVertexAndIndexBuffers();
			void CreateAllDynamicVertexAndIndexBuffers();

			// Finds or creates the static vertex buffer matching each shader's vertex stride
			void AllocateStaticVertexBuffers();
			void AllocateDynamicVertexBuffers();

			// Creates the static index buffer used by all static geometry
			void CreateStaticIndexBuffer();

			void CreateAndUploadToStaticVertexBuffer(VulkanBuffer* vertexBuffer, void* vertexBufferData, u32 vertexBufferSize, const char* DEBUG_name = nullptr);
			void CreateDynamicVertexBuffer(VulkanBuffer* vertexBuffer, u32 size, const char* DEBUG_name = nullptr, bool bDeviceLocal = false);
			void CreateDynamicIndexBuffer(VulkanBuffer* indexBuffer, u32 size, const char* DEBUG_name = nullptr, bool bDeviceLocal = false);

			// Copies data into a device local buffer: the data is written into a staging buffer now & copied on the GPU
			// before this frame's rendering is submitted (in submission order, so later uploads to the same range win)
			void QueueBufferUpload(VulkanBuffer* dst, VkDeviceSize dstOffset, const void* data, VkDeviceSize size);
			// Submits the copies queued so far (without waiting for them)
			void FlushBufferUploads();
			// Waits for submitted copies (only once the frame's work has completed, so this doesn't stall) & reuses
			// their staging space
			void RecycleBufferUploads();
			void DestroyBufferUploads();

			void CreateAndUploadToStaticIndexBuffer(VulkanBuffer* indexBuffer, const std::vector<u32>& indices, const char* DEBUG_name = nullptr);

			void CreateSemaphores();
			void CreateRenderCompleteSemaphores();
			void DestroyRenderCompleteSemaphores();

			void FillOutShaderBatches(const std::vector<RenderID>& renderIDs, i32* inOutDynamicUBOOffset,
				MaterialBatchPair& matBatchPair, MaterialBatchPair& depthAwareEditorMatBatchPair, MaterialBatchPair& depthUnawareEditorMatBatchPair,
				MaterialID matID, bool bWriteUBOOffsets = true);
			// Applies pending batch changes (or a full rebuild when required) before drawing
			void UpdateRenderObjectBatches();
			void RebuildAllRenderObjectBatches();
			void MarkRenderObjectBatchDirty(RenderID renderID);
			// Returns true if any dynamic uniform buffers had to grow (invalidating their contents)
			bool AddRenderObjectToBatches(RenderID renderID);
			void RemoveRenderObjectFromBatches(VulkanRenderObject* renderObject);
			ShaderBatch* GetShaderBatchOfType(u8 batchType);
			// Returns true if the shader's dynamic UBO had to grow
			bool AllocateDynamicUBOSlot(VulkanRenderObject* renderObject, VulkanMaterial* material);
			void FreeDynamicUBOSlot(VulkanRenderObject* renderObject);

			GraphicsPipeline* GetShadowGraphicsPipeline(u32 vertexStride);
			// Returns InvalidID if the render object's slot doesn't fit in the shadow material's dynamic buffer
			u32 GetShadowDynamicUBOOffset(RenderID renderID) const;
			bool RenderObjectCastsShadow(VulkanRenderObject* renderObject) const;
			void DrawShaderBatch(const ShaderBatchPair& shaderBatches, VkCommandBuffer& commandBuffer, DrawCallInfo* drawCallInfo = nullptr);

			// Point light shadows (see docs/gi_plan.md): a pool of slots, each a cube map (6 faces of a 2D array) rendered
			// from a light's position. Slots are keyed by position (torches swap light IDs as the camera moves), each face
			// is cached until a hash of the casters in its frustum changes, & at most faceBudget faces are drawn per frame.
			struct PointShadowSlot
			{
				glm::vec3 pos = VEC3_ZERO; // World space, where the faces were (or are being) rendered from
				real fade = 0.0f; // Shadow strength, fades in once every face is ready & out before the slot is reused
				real score = 0.0f;
				u64 faceHash[6] = {};
				u64 currentHash[6] = {};
				u64 faceRenderedFrame[6] = {};
				bool bFaceValid[6] = {};
				bool bInUse = false; // Assigned to a light position
				bool bWanted = false; // Among the lights that should have shadows this frame
				bool bReferenced = false; // Used by the uniform data uploaded this frame (so can't be reassigned yet)
				// Casters overlapping each face (shader batch, render ID), gathered every frame
				std::vector<std::pair<const ShaderBatchPair*, RenderID>> faceCasters[6];
			};
			void CreatePointShadowResources();
			void DestroyPointShadowResources();
			// Called before the constant uniform buffers are written: matches lights to ready slots & fades them
			void UpdatePointShadowUniformData();
			// Called once batches are up to date: assigns slots to lights, hashes casters & picks the faces to render
			void UpdatePointShadowSlots();
			void RenderPointShadows(VkCommandBuffer commandBuffer);
			// The view projection (render space) of one face, & its basis (shared with PointLightShadow in vk_misc.glsl)
			glm::mat4 GetPointShadowFaceViewProj(const glm::vec3& lightPosRS, u32 face) const;
			static void GetPointShadowFaceBasis(u32 face, glm::vec3& outForward, glm::vec3& outUp);
			real GetPointShadowTanHalfFOV() const;
			std::vector<PointShadowSlot> m_PointShadowSlots;
			std::vector<std::pair<u32, u32>> m_PointShadowFacesToRender; // (slot, face)
			PointShadowData m_PointShadowData = {};
			VDeleter<VkImage> m_PointShadowImage;
			VDeleter<VkDeviceMemory> m_PointShadowImageMemory;
			VDeleter<VkImageView> m_PointShadowImageView;
			std::vector<Cascade*> m_PointShadowFaces; // A view & frame buffer per layer
			u32 m_PointShadowResolution = 0; // As allocated
			i32 m_PointShadowSlotCount = 0;
			u64 m_PointShadowFrameIndex = 0;
			// World space bounding sphere to cull an object with (w <= 0: never culled)
			glm::vec4 GetCullSphere(VulkanRenderObject* renderObject) const;
			// Side planes of the current camera's frustum, for culling (updated at the start of each frame)
			void UpdateCameraCullPlanes();
			glm::vec4 m_CameraCullPlanes[4];
			u32 m_FrameDrawCallCount = 0;
			u32 m_FrameShadowDrawCallCount = 0;
			u64 m_FrameTriangleCount = 0;
			u64 m_FrameShadowTriangleCount = 0;

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

			void FillOutOffscreenCommandBuffer();
			void FillOutForwardCommandBuffer(const DrawCallInfo& drawCallInfo);
			void DispatchParticleSimWorkloads(VkCommandBuffer commandBuffer);
			void DispatchTerrainGenWorkloads(VkCommandBuffer commandBuffer);
			u32 SubmitOffscreenWork();
			void SubmitSceneRenderingWork(u32 nextImageIndex);

			// Returns false (& binds offset 0) if dynamicOffset is out of the material's dynamic buffer's range
			bool BindDescriptorSet(const VulkanMaterial* material, u32 dynamicOffsetOffset, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet, VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS) const;
			void RecreateSwapChain();

			void BeginDebugMarkerRegionInternal(VkCommandBuffer cmdBuf, const char* markerName, const glm::vec4& colour);
			void EndDebugMarkerRegionInternal(VkCommandBuffer cmdBuf, const char* markerName);

			void SetCheckPoint(VkCommandBuffer cmdBuf, const char* checkPointName);

			bool CreateShaderModule(const std::string& shaderName, const char* shaderType, const std::vector<char>& code, VkShaderModule* shaderModule) const;
			VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
			VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
			VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
			VulkanSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
			bool IsDeviceSuitable(VkPhysicalDevice device);
			std::vector<const char*> GetRequiredInstanceExtensions() const;
			void DisableUnsupportedValidationLayers();

			void UpdateConstantUniformBuffers();
			void UpdateDynamicUniformBuffer(RenderID renderID,
				MaterialID materialIDOverride = InvalidMaterialID,
				u32 dynamicUBOOffsetOverride = InvalidID);
			void UpdateDynamicUniformBuffer(MaterialID materialID, u32 dynamicOffsetIndex,
				const glm::mat4& model, UniformOverrides const* uniformOverrides = nullptr);

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
			// Query resets aren't allowed inside render passes: reserve (& reset) a timestamp before the pass begins, then
			// time a part of the pass with BeginReservedGPUTimeStamp & EndGPUTimeStamp
			void ReserveGPUTimeStamp(VkCommandBuffer commandBuffer, const std::string& name);
			void BeginReservedGPUTimeStamp(VkCommandBuffer commandBuffer, const std::string& name);
			ms GetDurationBetweenTimeStamps(const std::string& name);

			static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
				VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
				VkDebugUtilsMessageTypeFlagsEXT messageTypes,
				const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
				void* pUserData);

			inline VulkanRenderObject* GetRenderObject(RenderID renderID)
			{
				return m_RenderObjects[renderID];
			}

			u32 GetActiveRenderObjectCount() const;

			void DrawText(VkCommandBuffer commandBuffer, bool bScreenSpace);
			void DrawSpriteBatch(const std::vector<SpriteQuadDrawInfo>& batch, VkCommandBuffer commandBuffer);
			MaterialID GetSpriteBatchMaterialID(const std::vector<SpriteQuadDrawInfo>& batch) const;
			// Grows sprite shaders' dynamic UBOs to fit every sprite queued this frame (must be called before recording)
			void EnsureSpriteCapacity();
			void DrawUIMesh(UIMesh* uiMesh, VkCommandBuffer commandBuffer);
			void DrawParticles(VkCommandBuffer commandBuffer);
			void DrawTerrain(VkCommandBuffer commandBuffer);

			VkDescriptorSet GetSpriteDescriptorSet(TextureID textureID, MaterialID spriteMaterialID, u32 textureLayer, bool bCacheResult);

			VkRenderPass ResolveRenderPassType(RenderPassType renderPassType, const char* shaderName = nullptr);

			void CreateShadowResources();
			VkDescriptorSet CreateSpriteDescSet(MaterialID spriteMaterialID, TextureID textureID, u32 layer = 0);

			bool InitializeParticleSystemBuffer(VulkanParticleSystem* particleSystem, ParticleEmitterID emitterID);

			void DestroyTerrain();

			void UpdateShaderMaxObjectCount(ShaderID shaderID, i32 newMax);
			// Grows the dynamic UBOs of every material using shaderID (geometrically) if they have fewer than
			// requiredSlotCount slots. Returns true if they grew (all their previous contents are kept).
			// Must not be called while command buffers are being recorded.
			bool EnsureDynamicUBOCapacity(ShaderID shaderID, u32 requiredSlotCount);

			void CreateBRDFTexture();


			static const bool ENABLE_ASYNC_TERRAIN_GEN = true;

			static const FrameBufferAttachmentID SWAP_CHAIN_COLOUR_ATTACHMENT_ID = 11000;
			static const FrameBufferAttachmentID SWAP_CHAIN_DEPTH_ATTACHMENT_ID = 11001;
			static const FrameBufferAttachmentID SHADOW_CASCADE_DEPTH_ATTACHMENT_ID = 22001;

#if defined(RELEASE) || defined(PROFILE)
			bool m_bEnableValidationLayers = false;
			bool m_bEnableGPUAssistanceValidationFeature = false;
			bool m_bEnableBestPracticesValidationFeature = false;
			bool m_bEnableDebugPrintf = false;
#else
			bool m_bEnableValidationLayers = true;
			bool m_bEnableGPUAssistanceValidationFeature = false;
			bool m_bEnableBestPracticesValidationFeature = false;
			bool m_bEnableDebugPrintf = true;
#endif

			// Nothing uses ray tracing yet, & enabling its extensions makes device creation slower
			bool m_bTryEnableRayTracing = false;
			bool m_bRayTracingEnabled = false;

			struct Version
			{
				i32 maj;
				i32 min;
				i32 patch;
			};

			Version m_InstanceVersion;
			Version m_DeviceVersion;
			Version m_DriverVersion;

			// Streamed terrain alone can use several thousand objects (more at high quality settings)
			const u32 MAX_NUM_RENDER_OBJECTS = 16384; // TODO: Support resizing
			std::vector<VulkanRenderObject*> m_RenderObjects;
			mutable u32 m_FirstFreeRenderIDHint = 0;

			// Render objects whose batch membership needs to be re-evaluated before the next draw
			std::vector<RenderID> m_RenderObjectsPendingBatch;

			struct DynamicUBOSlotAllocator
			{
				u32 slotCount = 0;
				std::vector<u32> freeSlots;
			};
			// Indexed by shader ID
			std::vector<DynamicUBOSlotAllocator> m_DynamicUBOSlotAllocators;
			// Dynamic UBO offsets are invalidated when this changes
			u32 m_BatchedDynamicAlignment = 0;

			std::vector<const char*> m_ValidationLayers =
			{
				"VK_LAYER_KHRONOS_validation",
				//"VK_LAYER_LUNARG_standard_validation",
				//"VK_LAYER_LUNARG_monitor", // FPS in title bar
				//"VK_LAYER_LUNARG_api_dump", // Log content
				//"VK_LAYER_LUNARG_screenshot",
				//"VK_LAYER_RENDERDOC_Capture", // RenderDoc captures, in engine integration works better (see COMPILE_RENDERDOC_API)
			};

			const std::vector<const char*> m_RequiredDeviceExtensions =
			{
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_KHR_MAINTENANCE1_EXTENSION_NAME,
			};

			const std::vector<const char*> m_RequiredInstanceExtensions =
			{
			};

			struct VkExtensionPair
			{
				bool bDebugOnly;
				const char* extensionName;
			};

			const std::vector<VkExtensionPair> m_OptionalInstanceExtensions =
			{
				{ true, VK_EXT_DEBUG_UTILS_EXTENSION_NAME },
				{ false, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME },
			};

			const std::vector<VkExtensionPair> m_OptionalDeviceExtensions =
			{
				{ true, VK_EXT_DEBUG_MARKER_EXTENSION_NAME },
				{ true, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME },
				{ true, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME },
			};

			std::vector<VkExtensionProperties> m_SupportedInstanceExtensions;
			std::vector<const char*> m_EnabledInstanceExtensions;

			bool m_bDiagnosticCheckpointsEnabled = false;
			bool m_bMemoryBudgetExtensionEnabled = false;

			glm::vec2i m_CubemapFramebufferSize;
			glm::vec2i m_BRDFSize;
			VulkanTexture* m_BRDFTexture = nullptr;
			bool bRenderedBRDFLUT = false;

			FrameBufferAttachment* m_GBufferColourAttachment0 = nullptr;
			FrameBufferAttachment* m_GBufferColourAttachment1 = nullptr;
			FrameBufferAttachment* m_GBufferDepthAttachment = nullptr;

			VDeleter<VkSampler> m_SamplerDepth;
			// Depth-comparison sampler for shadow maps (hardware PCF)
			VDeleter<VkSampler> m_SamplerShadow;
			VDeleter<VkSampler> m_SamplerLinearRepeat;
			VDeleter<VkSampler> m_SamplerLinearClampToEdge;
			VDeleter<VkSampler> m_SamplerLinearClampToBorder;
			VDeleter<VkSampler> m_SamplerNearestClampToEdge;
			// Allows sampling every mip level (other samplers clamp to mip 1)
			VDeleter<VkSampler> m_SamplerLinearClampToEdgeAllMips;

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

			// Sets reference both the texture (& layer) and the material's dynamic UBO, so they're unique per combination
			struct SpriteDescSetKey
			{
				TextureID textureID;
				MaterialID materialID;
				u32 textureLayer;

				bool operator<(const SpriteDescSetKey& other) const
				{
					if (textureID != other.textureID) return textureID < other.textureID;
					if (materialID != other.materialID) return materialID < other.materialID;
					return textureLayer < other.textureLayer;
				}
			};
			std::map<SpriteDescSetKey, SpriteDescSet> m_SpriteDescSets;
			// Number of dynamic UBO slots used by sprites this frame, per material (each material has its own buffer)
			std::map<MaterialID, u32> m_SpriteDynamicUBOSlotCounts;

			// Maps uniform IDs to data & data length
			std::map<u64, Pair<void*, u32>> m_GlobalUserUniforms;

			// TODO: Create other query pools
			VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;

			VkPipelineCache m_PipelineCache = VK_NULL_HANDLE;
			static const u64 MAX_TIMESTAMP_QUERIES = 1024;

			// Points from timestamp names to query indices. Index is negated on timestamp end to signify being ended.
			std::map<std::string, i32> m_TimestampQueryNames;

			VkInstance m_Instance = VK_NULL_HANDLE;
			VkDebugUtilsMessengerEXT m_DebugUtilsMessengerCallback = VK_NULL_HANDLE;
			VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

			VulkanDevice* m_VulkanDevice = nullptr;

			VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
			VkQueue m_AsyncComputeQueue = VK_NULL_HANDLE;
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

			// Maps source vertex stride to shadow pipeline (this map doesn't have ownership over pipelines, only references)
			std::map<u32, GraphicsPipelineID> m_ShadowGraphicsPipelines;

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

			VertexIndexBufferPair* m_DynamicUIVertexIndexBufferPair = nullptr;

			struct PendingBufferUpload
			{
				VulkanBuffer* dst;
				VkDeviceSize dstOffset;
				VkDeviceSize stagingOffset;
				VkDeviceSize size;
			};
			struct InFlightBufferUpload
			{
				VkCommandBuffer commandBuffer;
				VkFence fence;
			};
			std::vector<PendingBufferUpload> m_PendingBufferUploads;
			std::vector<InFlightBufferUpload> m_InFlightBufferUploads;
			VulkanBuffer* m_UploadStagingBuffer = nullptr;
			VkDeviceSize m_UploadStagingOffset = 0;

			VDeleter<VkSemaphore> m_PresentCompleteSemaphore;
			// One per swap chain image since presentation doesn't signal when it's done waiting on a semaphore,
			// we only know it's safe to reuse once the image it was presented with is reacquired
			std::vector<VkSemaphore> m_RenderCompleteSemaphores;

			VkCommandBuffer m_OffScreenCmdBuffer = VK_NULL_HANDLE;
			VkSemaphore m_OffscreenSemaphore = VK_NULL_HANDLE;
			VkCommandBuffer m_TerrainAsyncComputeCommandBuffer = VK_NULL_HANDLE;

			VkClearColorValue m_ClearColour;

			u32 m_CurrentSwapChainBufferIndex = 0;

			TextureID m_LTCMatricesID = InvalidTextureID;
			TextureID m_LTCAmplitudesID = InvalidTextureID;

			GraphicsPipelineID m_SSAOGraphicsPipelineID = InvalidGraphicsPipelineID;
			GraphicsPipelineID m_SSAOBlurHGraphicsPipelineID = InvalidGraphicsPipelineID;
			GraphicsPipelineID m_SSAOBlurVGraphicsPipelineID = InvalidGraphicsPipelineID;
			VkDescriptorSet m_SSAODescSet = VK_NULL_HANDLE;
			VkDescriptorSet m_SSAOBlurHDescSet = VK_NULL_HANDLE;
			VkDescriptorSet m_SSAOBlurVDescSet = VK_NULL_HANDLE;

			// Maps vertex attributes to pipeline (this map doesn't have ownership over pipelines, only references)
			std::map<VertexAttributes, GraphicsPipelineID> m_WireframeGraphicsPipelines;
			VkDescriptorSet m_WireframeDescSet = VK_NULL_HANDLE;

			PoolAllocator<DeviceDiagnosticCheckpoint, 32> m_CheckPointAllocator;

			struct Terrain
			{
				bool bVisibile = true;

				MaterialID genPointsMaterialID = InvalidMaterialID;
				MaterialID genMeshMaterialID = InvalidMaterialID;
				MaterialID renderingMaterialID = InvalidMaterialID;
				VkPipeline genPointsPipeline = VK_NULL_HANDLE;
				VkPipelineLayout genPointsPipelineLayout = VK_NULL_HANDLE;
				VkPipeline genMeshComputePipeline = VK_NULL_HANDLE;
				VkPipelineLayout genMeshComputePipelineLayout = VK_NULL_HANDLE;
				GraphicsPipelineID graphicsPipelineID = InvalidGraphicsPipelineID;
				VkDescriptorSet genPointsDescriptorSet = VK_NULL_HANDLE;
				VkDescriptorSet genMeshDescriptorSet = VK_NULL_HANDLE;
				VkDescriptorSet renderingDescriptorSet = VK_NULL_HANDLE;
				TerrainGenConstantData constantData;

				TextureID randomTablesTextureID = InvalidTextureID;

				//VulkanBuffer* indexBuffer = nullptr;
				//std::vector<u32> indexBufferBackingMemory;
				VkDrawIndirectCommand* indirectBufferCPU = nullptr;
				VulkanBuffer* indirectBuffer = nullptr;

				GPUBuffer* pointBufferGPU = nullptr;
				// Device-local only; first i32 is the atomic triangle count written by the mesh gen shader
				GPUBuffer* vertexBufferGPU = nullptr;
				// Host-visible copy of vertexBufferGPU's triangle count, filled at the end of each gen dispatch
				VulkanBuffer* triCountReadbackBuffer = nullptr;
				u32 maxChunkCount = 0;

				VkFence fence = VK_NULL_HANDLE;
				i32 lastTriCount = 0;
				glm::ivec3 loadingChunkIndex;
				u32 loadingChunkLinearIndex = u32_max;

				const u32 maxNumRenderedChunks = 256;
			};
			Terrain* m_Terrain = nullptr;

			std::vector<Pair<glm::ivec3, u32>> m_TerrainGenWorkloads;
			std::vector<Pair<glm::ivec3, u32>> m_TerrainChunksLoaded;

			std::vector<GPUBuffer*> m_GPUBuffers;
		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN
