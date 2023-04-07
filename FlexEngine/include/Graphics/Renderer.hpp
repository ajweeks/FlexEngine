#pragma once

#include "BitmapFont.hpp"
#include "ConfigFile.hpp"
#include "Physics/PhysicsDebuggingSettings.hpp"
#include "RendererTypes.hpp"
#include "VertexBufferData.hpp"

struct FT_LibraryRec_;
typedef struct FT_LibraryRec_* FT_Library;

namespace flex
{
	class DirectionalLight;
	class DirectoryWatcher;
	class DebugRenderer;
	class GameObject;
	class EditorObject;
	class MeshComponent;
	class Mesh;
	class ParticleSystem;
	class PointLight;
	class UIMesh;
	struct TextCache;

	class Renderer
	{
	public:
		Renderer();
		virtual ~Renderer();

		virtual void Initialize();
		virtual void PostInitialize();
		virtual void Destroy();

		virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace = InvalidMaterialID) = 0;
		virtual TextureID InitializeLoadedTexture(TextureID textureID) = 0;
		virtual TextureID InitializeTextureFromFile(const std::string& relativeFilePath,
			HTextureSampler inSampler,
			bool bFlipVertically,
			bool bGenerateMipMaps,
			bool bHDR,
			TextureID existingTextureID = InvalidTextureID) = 0;
		virtual TextureID InitializeTextureFromMemory(void* data, u32 size, TextureFormat inFormat, const std::string& name, u32 width, u32 height, u32 channelCount, HTextureSampler inSampler, VkFilter inFilter) = 0;
		virtual TextureID InitializeTextureArrayFromMemory(void* data, u32 size, TextureFormat inFormat, const std::string& name, u32 width, u32 height, u32 layerCount, u32 channelCount, HTextureSampler inSampler) = 0;
		virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) = 0;
		virtual void PostInitializeRenderObject(RenderID renderID) = 0; // Only call when creating objects after calling PostInitialize()
		virtual void OnTextureDestroyed(TextureID textureID) = 0;

		virtual void ReplaceMaterialsOnObjects(MaterialID oldMatID, MaterialID newMatID) = 0;

		virtual void Update();
		virtual void EndOfFrame();
		virtual void Draw() = 0;
		virtual void DrawImGuiWindows();
		virtual void DrawImGuiRendererInfo() = 0;

		virtual void UpdateDynamicVertexData(RenderID renderID, VertexBufferData const* vertexBufferData, const std::vector<u32>& indexData) = 0;
		virtual void FreeDynamicVertexData(RenderID renderID) = 0;
		virtual void ShrinkDynamicVertexData(RenderID renderID, real minUnused = 0.0f) = 0;
		virtual u32 GetDynamicVertexBufferSize(RenderID renderID) = 0;
		virtual u32 GetDynamicVertexBufferUsedSize(RenderID renderID) = 0;

		virtual void DrawImGuiTexture(TextureID textureID, real texSize, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) = 0;
		virtual void DrawImGuiTexture(Texture* texture, real texSize, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) = 0;

		void QueueHologramMesh(PrefabID prefabID, Transform& transform, const glm::vec4& colour);
		void QueueHologramMesh(PrefabID prefabID, const glm::vec3& posWS, const glm::quat& rotWS, const glm::vec3& scaleWS, const glm::vec4& colour);

		virtual void OnWindowSizeChanged(i32 width, i32 height) = 0;

		virtual void OnPreSceneChange();
		virtual void OnPostSceneChange(); // Called once scene has been loaded and all objects have been initialized and post initialized

		virtual void OnSettingsReloaded() = 0;

		/*
		* Fills outInfo with an up-to-date version of the render object's info
		* Returns true if renderID refers to a valid render object
		*/
		virtual bool GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo) = 0;

		virtual void SetVSyncEnabled(bool bEnableVSync) = 0;

		virtual u32 GetRenderObjectCount() const = 0;
		virtual u32 GetRenderObjectCapacity() const = 0;

		virtual void SetSkyboxMesh(Mesh* skyboxMesh) = 0;

		virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) = 0;

		virtual MaterialID GetRenderObjectMaterialID(RenderID renderID) = 0;

		virtual std::vector<Pair<std::string, MaterialID>> GetValidMaterialNames(bool bEditorMaterials) const = 0;

		virtual bool DestroyRenderObject(RenderID renderID) = 0;

		virtual void SetGlobalUniform(Uniform const* uniform, void* data, u32 dataSize) = 0;
		virtual void AddRenderObjectUniformOverride(RenderID renderID, Uniform const* uniform, const MaterialPropertyOverride& propertyOverride) = 0;

		virtual void NewFrame();

		virtual void RenderObjectMaterialChanged(MaterialID materialID) = 0;

		virtual DebugRenderer* GetDebugRenderer() = 0;

		virtual void DrawStringSS(const std::string& str,
			const glm::vec4& colour,
			AnchorPoint anchor,
			const glm::vec2& pos,
			real spacing,
			real scale = 1.0f) = 0;

		virtual void DrawStringWS(const std::string& str,
			const glm::vec4& colour,
			const glm::vec3& pos,
			const glm::quat& rot,
			real spacing,
			real scale = 1.0f) = 0;

		virtual void RecaptureReflectionProbe() = 0;

		// Call whenever a user-controlled field, such as visibility, changes to rebatch render objects
		virtual void RenderObjectStateChanged() = 0;

		// TODO: Use MeshID
		virtual void RecreateRenderObjectsWithMesh(const std::string& relativeMeshFilePath) = 0;

		virtual ParticleSystemID AddParticleSystem(const std::string& name, ParticleSystem* system) = 0;
		virtual bool RemoveParticleSystem(ParticleSystemID particleSystemID) = 0;
		virtual bool AddParticleEmitterInstance(ParticleSystemID particleSystemID, ParticleEmitterID emitterID) = 0;
		virtual void RemoveParticleEmitterInstance(ParticleSystemID particleSystemID, ParticleEmitterID emitterID) = 0;
		virtual void OnParticleSystemTemplateUpdated(StringID particleTemplateNameSID) = 0;

		virtual void RecreateEverything() = 0;

		virtual void ReloadObjectsWithMesh(const std::string& meshFilePath) = 0;

		virtual void InitializeTerrain(MaterialID terrainMaterialID, TextureID randomTablesTextureID, const TerrainGenConstantData& constantData, u32 initialMaxChunkCount) = 0;
		virtual void RegenerateTerrain(const TerrainGenConstantData& constantData, u32 maxChunkCount) = 0;
		virtual void RegisterTerrainChunk(const glm::ivec3& chunkIndex, u32 linearIndex) = 0;
		virtual void RemoveTerrainChunk(const glm::ivec3& chunkIndex) = 0;
		virtual u32 GetCurrentTerrainChunkCapacity() const = 0;
		virtual u32 GetChunkVertCount(u32 chunkLinearIndex) const = 0;
		virtual void SetChunkVertCount(u32 chunkLinearIndex, u32 count) = 0;

		// Will attempt to find pre-rendered font at specified path, and
		// only render a new file if not present or if bForceRender is true
		// Returns true if succeeded
		virtual bool LoadFont(FontMetaData& fontMetaData, bool bForceRender) = 0;

		virtual void OnTextureReloaded(Texture* texture) = 0;

		virtual Texture* CreateTexture(const std::string& textureName) = 0;

		virtual HTextureSampler GetSamplerLinearRepeat() = 0;
		virtual HTextureSampler GetSamplerLinearClampToEdge() = 0;
		virtual HTextureSampler GetSamplerLinearClampToBorder() = 0;
		virtual HTextureSampler GetSamplerNearestClampToEdge() = 0;

		virtual GPUBufferID RegisterGPUBuffer(GPUBuffer* uniformBuffer) = 0;
		virtual void UnregisterGPUBuffer(GPUBufferID bufferID) = 0;
		virtual GPUBuffer* GetGPUBuffer(GPUBufferID bufferID) = 0;

		virtual void UploadDataViaStagingBuffer(GPUBufferID bufferID, u32 bufferSize, void* data, u32 dataSize) = 0;

		virtual void FreeGPUBuffer(GPUBuffer*) = 0;
		virtual GPUBuffer* AllocateGPUBuffer(GPUBufferType type, const std::string& debugName) = 0;

		virtual void CreateGPUBuffer(GPUBuffer* buffer,
			u32 bufferSize,
			VkBufferUsageFlags bufferUseageFlagBits,
			VkMemoryPropertyFlags memoryPropertyHostFlagBits,
			bool bMap = true) = 0;

		virtual void SetGPUBufferName(GPUBuffer const* buffer, const char* name) = 0;

		virtual u32 GetNonCoherentAtomSize() const = 0;

		void SetReflectionProbeMaterial(MaterialID reflectionProbeMaterialID);

		i32 GetShortMaterialIndex(MaterialID materialID, bool bShowEditorMaterials);
		// Returns true when the selected material has changed
		bool DrawImGuiMaterialList(MaterialID* selectedMaterialID, bool bShowEditorMaterials, bool bScrollToSelected);
		void DrawImGuiTexturePreviewTooltip(Texture* texture);
		// Returns true if any property changed
		bool DrawImGuiForGameObject(GameObject* gameObject, bool bDrawingEditorObjects);

		void DrawSpecializationConstantInfoImGui();
		void DrawImGuiSettings();

		void AddShaderSpecialziationConstant(ShaderID shaderID, StringID specializationConstant);
		void RemoveShaderSpecialziationConstant(ShaderID shaderID, StringID specializationConstant);
		std::vector<StringID>* GetShaderSpecializationConstants(ShaderID shaderID);
		bool DrawShaderSpecializationConstantImGui(ShaderID shaderID);
		void SetSpecializationConstantDirty();

		real GetStringWidth(const std::string& str, BitmapFont* font, real letterSpacing, bool bNormalized) const;
		real GetStringHeight(const std::string& str, BitmapFont* font, bool bNormalized) const;

		real GetStringWidth(const TextCache& textCache, BitmapFont* font) const;
		real GetStringHeight(const TextCache& textCache, BitmapFont* font) const;

		void SaveSettingsToDisk(bool bAddEditorStr = true);
		void LoadSettingsFromDisk();

		void SetShaderQualityLevel(i32 newQualityLevel);

		MaterialID GetMaterialID(const std::string& materialName);

		// Pos should lie in range [-1, 1], with y increasing upward
		// Output pos lies in range [0, 1], with y increasing downward,
		// Output scale lies in range [0, 1] - both outputs corrected for aspect ratio
		void TransformRectToScreenSpace(const glm::vec2& pos,
			const glm::vec2& scale,
			glm::vec2& posOut,
			glm::vec2& scaleOut);

		void NormalizeSpritePos(const glm::vec2& pos,
			AnchorPoint anchor,
			const glm::vec2& scale,
			glm::vec2& posOut,
			glm::vec2& scaleOut);

		void EnqueueUntexturedQuad(const glm::vec2& pos, AnchorPoint anchor, const glm::vec2& size, const glm::vec4& colour);
		void EnqueueUntexturedQuadRaw(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& colour);
		void EnqueueSprite(SpriteQuadDrawInfo drawInfo);

		void ToggleRenderGrid();
		bool IsRenderingGrid() const;
		void SetRenderGrid(bool bRenderGrid);

		void SetDisplayBoundingVolumesEnabled(bool bEnabled);
		bool IsDisplayBoundingVolumesEnabled()const;

		PhysicsDebuggingSettings& GetPhysicsDebuggingSettings();

		bool RegisterDirectionalLight(DirectionalLight* dirLight);
		void RemoveDirectionalLight();
		DirectionalLight* GetDirectionalLight();

		PointLightID RegisterPointLight(PointLightData* pointLightData);
		void RemovePointLight(PointLightID ID);
		void RemoveAllPointLights();
		void UpdatePointLightData(PointLightID ID, PointLightData* data);
		i32 GetNumPointLights();

		SpotLightID RegisterSpotLight(SpotLightData* spotLightData);
		void RemoveSpotLight(SpotLightID ID);
		void RemoveAllSpotLights();
		void UpdateSpotLightData(SpotLightID ID, SpotLightData* data);
		i32 GetNumSpotLights();

		AreaLightID RegisterAreaLight(AreaLightData* areaLightData);
		void RemoveAreaLight(AreaLightID ID);
		void RemoveAllAreaLights();
		void UpdateAreaLightData(AreaLightID ID, AreaLightData* data);
		i32 GetNumAreaLights();

		i32 GetFramesRenderedCount() const;

		BitmapFont* SetFont(StringID fontID);
		// Draws the given string in the center of the screen for a short period of time
		// Passing an empty string will immediately clear the current string
		void AddEditorString(const std::string& str);
		void AddNotificationMessage(const std::string& message);

		i32 GetMaterialCount();
		Material* GetMaterial(MaterialID materialID);
		void RemoveMaterial(MaterialID materialID);
		Shader* GetShader(ShaderID shaderID);
		MaterialID GetNextAvailableMaterialID() const;

		bool MaterialExists(MaterialID materialID);
		bool MaterialWithNameExists(const std::string& matName);

		bool GetShaderID(const std::string& shaderName, ShaderID& shaderID);
		bool FindOrCreateMaterialByName(const std::string& materialName, MaterialID& outMaterialID);

		struct PostProcessSettings
		{
			real saturation = 1.0f;
			glm::vec3 brightness;
			glm::vec3 offset;
			bool bEnableFXAA = true;
			bool bEnableFXAADEBUGShowEdges = false;
		};

		PostProcessSettings& GetPostProcessSettings();

		MaterialID GetPlaceholderMaterialID() const;

		void SetDisplayShadowCascadePreview(bool bPreview);
		bool GetDisplayShadowCascadePreview() const;

		void SetTAAEnabled(bool bEnabled);
		bool IsTAAEnabled() const;

		i32 GetTAASampleCount() const;

		void SetDirtyFlags(RenderBatchDirtyFlags flags);

		const std::map<MaterialID, Material*>& GetLoadedMaterials();

		void SetDynamicGeometryBufferDirty(u32 dynamicVertexBufferIndex);
		void SetStaticGeometryBufferDirty(u32 staticVertexBufferIndex);

		void ToggleWireframeOverlay();
		void ToggleWireframeSelectionOverlay();

		UIMesh* GetUIMesh();

		void SetDebugOverlayID(i32 newID);

		void ToggleFogEnabled();

		// Returns true if value changed
		bool DrawImGuiShadersDropdown(i32* selectedShaderID, Shader** outSelectedShader = nullptr);
		bool DrawImGuiTextureSelector(const char* label, const std::vector<std::string>& textureNames, i32* selectedIndex);
		bool DrawImGuiShadersList(i32* selectedShaderID, bool bShowFilter, Shader** outSelectedShader = nullptr);
		void DrawImGuiShaderErrors();

		void ClearShaderHash(const std::string& shaderName);
		void RecompileShaders(bool bForceCompileAll);

		u32 GetAlignedUBOSize(u32 unalignedSize);

		static const u32 MAX_PARTICLE_COUNT_PER_INSTANCE = 65536;
		static const u32 MAX_PARTICLE_EMITTER_INSTANCES_PER_SYSTEM = 16;
		static const u32 PARTICLES_PER_DISPATCH = 256;
		static const u32 SSAO_NOISE_DIM = 4;

		// TODO: Store in map (string name -> TextureID)
		TextureID blankTextureID = InvalidTextureID; // 1x1 white pixel texture
		TextureID blankTextureArrID = InvalidTextureID; // 1x1 white pixel with texture array with one layer
		TextureID pinkTextureID = InvalidTextureID; // 1x1 pink (magenta) texture
		TextureID alphaBGTextureID = InvalidTextureID;

		TextureID pointLightIconID = InvalidTextureID;
		TextureID spotLightIconID = InvalidTextureID;
		TextureID areaLightIconID = InvalidTextureID;
		TextureID directionalLightIconID = InvalidTextureID;

		StringID previewedFont = InvalidStringID;

		MaterialID m_SpriteMatSSID = InvalidMaterialID;
		MaterialID m_SpriteMatWSID = InvalidMaterialID;
		MaterialID m_SpriteArrMatID = InvalidMaterialID;

		MaterialID m_HologramMatID = InvalidMaterialID;
		//RenderID m_HologramProxyRenderID = InvalidRenderID;
		EditorObject* m_HologramProxyObject = nullptr;

		struct HologramMetaData
		{
			glm::vec3 posWS;
			glm::quat rotWS;
			glm::vec3 scaleWS;
			glm::vec4 colour;
		};
		// Stores data about the queued hologram (if any)
		HologramMetaData m_QueuedHologramData;
		PrefabID m_QueuedHologramPrefabID = InvalidPrefabID;

		Texture* m_BlankTexture = nullptr;
		Texture* m_BlankTextureArr = nullptr;
		Texture* m_NoiseTexture = nullptr;
		Texture* m_PinkTexture = nullptr;

	protected:
		struct ShaderMetaData
		{
			std::vector<std::string> vertexAttributes;
			std::vector<std::string> uboConstantFields;
			std::vector<std::string> uboDynamicFields;
			std::vector<std::string> sampledTextures;
		};

		virtual void InitializeShaders(const std::vector<ShaderInfo>& shaderInfos) = 0;
		virtual bool LoadShaderCode(ShaderID shaderID) = 0;
		virtual void FillOutGBufferFrameBufferAttachments(std::vector<Pair<std::string, void*>>& outVec) = 0;
		virtual void RecreateShadowFrameBuffers() = 0;

		virtual u32 GetStaticVertexIndexBufferIndex(u32 stride) = 0;
		virtual u32 GetDynamicVertexIndexBufferIndex(u32 stride) = 0;

		void ParseShaderMetaData();
		u32 ParseShaderBufferFields(const std::vector<std::string>& fileLines, u32 j, std::vector<std::string>& outFields);
		void LoadShaders();

		void EnqueueScreenSpaceSprites();
		void EnqueueWorldSpaceSprites();

		void GenerateGBuffer();

		void EnqueueScreenSpaceText();
		void EnqueueWorldSpaceText();

		void InitializeEngineMaterials();

		u32 UpdateTextBufferSS(std::vector<TextVertex2D>& outTextVertices);
		u32 UpdateTextBufferWS(std::vector<TextVertex3D>& outTextVertices);

		glm::vec4 GetSelectedObjectColourMultiplier() const;
		glm::mat4 GetPostProcessingMatrix() const;

		void GenerateSSAONoise(std::vector<glm::vec4>& noise);

		MaterialID CreateParticleSystemSimulationMaterial(const std::string& name);
		MaterialID CreateParticleSystemRenderingMaterial(const std::string& name);

		void ParseSpecializationConstantInfo();
		void ParseShaderSpecializationConstants();
		void SerializeSpecializationConstantInfo();
		void SerializeShaderSpecializationConstants();

		void CreateParticleBuffer(Material* material);

		bool InitializeFreeType();
		void DestroyFreeType();

		u8* m_LightData = nullptr;
		PointLightData* m_PointLightData = nullptr; // Points into m_LightData buffer
		SpotLightData* m_SpotLightData = nullptr; // Points into m_LightData buffer
		AreaLightData* m_AreaLightData = nullptr; // Points into m_LightData buffer
		i32 m_NumPointLightsEnabled = 0;
		i32 m_NumSpotLightsEnabled = 0;
		i32 m_NumAreaLightsEnabled = 0;
		DirectionalLight* m_DirectionalLight = nullptr;

		u32 m_DynamicAlignment = 0;

		struct DrawCallInfo
		{
			bool bRenderToCubemap = false;
			bool bDeferred = false;
			bool bWireframe = false;
			bool bWriteToDepth = true;
			bool bRenderingShadows = false;
			bool bCalculateDynamicUBOOffset = false;

			MaterialID materialIDOverride = InvalidMaterialID;

			u32 dynamicUBOOffset = 0;

			u64 graphicsPipelineOverride = InvalidID;
			u64 pipelineLayoutOverride = InvalidID;
			u64 descriptorSetOverride = InvalidID;
			Material::PushConstantBlock* pushConstantOverride = nullptr;

			// NOTE: DepthTestFunc only supported in GL, Vulkan requires specification at pipeline creation time
			DepthTestFunc depthTestFunc = DepthTestFunc::GEQUAL;
			RenderID cubemapObjectRenderID = InvalidRenderID;
			MaterialID materialOverride = InvalidMaterialID;
			CullFace cullFace = CullFace::_INVALID;
		};

		i32 m_ShadowCascadeCount = MAX_SHADOW_CASCADE_COUNT;
		u32 m_ShadowMapBaseResolution = 4096;

		std::vector<glm::mat4> m_ShadowLightViewMats;
		std::vector<glm::mat4> m_ShadowLightProjMats;

		// Filled every frame
		std::vector<SpriteQuadDrawInfo> m_QueuedWSSprites;
		std::vector<SpriteQuadDrawInfo> m_QueuedSSPreUISprites;
		std::vector<SpriteQuadDrawInfo> m_QueuedSSPostUISprites;
		std::vector<SpriteQuadDrawInfo> m_QueuedSSArrSprites;

		std::map<MaterialID, Material*> m_Materials;
		std::vector<Shader*> m_Shaders;
		std::map<ShaderID, std::vector<MaterialID>> m_ShaderUsedMaterials; // Stores all materials used by each shader
		std::map<std::string, ShaderMetaData> m_ShaderMetaData;

		// TODO: Use a mesh prefab here
		VertexBufferData m_Quad3DVertexBufferData;
		RenderID m_Quad3DRenderID = InvalidRenderID;
		RenderID m_Quad3DSSRenderID = InvalidRenderID;
		VertexBufferData m_FullScreenTriVertexBufferData;
		RenderID m_FullScreenTriRenderID = InvalidRenderID;

		RenderID m_GBufferQuadRenderID = InvalidRenderID;

		bool m_bVSyncEnabled = true;
		PhysicsDebuggingSettings m_PhysicsDebuggingSettings;

		/* Objects that are created at bootup and stay active until shutdown, regardless of scene */
		std::vector<GameObject*> m_PersistentObjects;

		BitmapFont* m_CurrentFont = nullptr;

		PostProcessSettings m_PostProcessSettings;
		ConfigFile m_Settings;

		UIMesh* m_UIMesh = nullptr;

		u32 m_FramesRendered = 0;

		bool m_bInitialized = false;
		bool m_bPostInitialized = false;
		bool m_bSwapChainNeedsRebuilding = false;
		bool m_bRebatchRenderObjects = true; // TODO: Replace with simply checking dirty flags

		bool m_bEnableWireframeOverlay = false;
		bool m_bEnableSelectionWireframe = false;
		bool m_bDisplayBoundingVolumes = false;
		bool m_bDisplayShadowCascadePreview = false;
		bool m_bRenderGrid = true;

		bool m_bCaptureScreenshot = false;
		bool m_bCaptureReflectionProbes = false;

		bool m_bEnableTAA = false;
		i32 m_TAASampleCount = 2;
		bool m_bTAAStateChanged = false;

		i32 m_ShaderQualityLevel = 1;
		const i32 MAX_SHADER_QUALITY_LEVEL = 3;

		sec m_EditorStrSecRemaining = 0.0f;
		sec m_EditorStrSecDuration = 1.5f;
		real m_EditorStrFadeDurationPercent = 0.25f;
		std::string m_EditorMessage;
		std::vector<std::string> m_NotificationMessages; // Frame-long messages that are displayed in the top right of the screen

		MaterialID m_ReflectionProbeMaterialID = InvalidMaterialID; // Set by the user via SetReflecionProbeMaterial

		MaterialID m_GBufferMaterialID = InvalidMaterialID;
		MaterialID m_FontMatSSID = InvalidMaterialID;
		MaterialID m_FontMatWSID = InvalidMaterialID;
		MaterialID m_ShadowMaterialID = InvalidMaterialID;
		MaterialID m_PostProcessMatID = InvalidMaterialID;
		MaterialID m_PostFXAAMatID = InvalidMaterialID;
		MaterialID m_SelectedObjectMatID = InvalidMaterialID;
		MaterialID m_GammaCorrectMaterialID = InvalidMaterialID;
		MaterialID m_TAAResolveMaterialID = InvalidMaterialID;
		MaterialID m_PlaceholderMaterialID = InvalidMaterialID;
		MaterialID m_IrradianceMaterialID = InvalidMaterialID;
		MaterialID m_PrefilterMaterialID = InvalidMaterialID;
		MaterialID m_BRDFMaterialID = InvalidMaterialID;
		MaterialID m_WireframeMatID = InvalidMaterialID;

		MaterialID m_ComputeSDFMatID = InvalidMaterialID;
		MaterialID m_FullscreenBlitMatID = InvalidMaterialID;

		MaterialID m_SSAOMatID = InvalidMaterialID;
		MaterialID m_SSAOBlurMatID = InvalidMaterialID;
		ShaderID m_SSAOShaderID = InvalidShaderID;
		ShaderID m_SSAOBlurShaderID = InvalidShaderID;

		Mesh* m_SkyBoxMesh = nullptr;
		ShaderID m_SkyboxShaderID = InvalidShaderID;

		ShaderID m_UIShaderID = InvalidShaderID;

		glm::mat4 m_LastFrameViewProj;

		ShadowSamplingData m_ShadowSamplingData;

		Material::PushConstantBlock* m_SpritePerspPushConstBlock = nullptr;
		Material::PushConstantBlock* m_SpriteOrthoPushConstBlock = nullptr;
		Material::PushConstantBlock* m_SpriteOrthoArrPushConstBlock = nullptr;

		Material::PushConstantBlock* m_CascadedShadowMapPushConstantBlock = nullptr;

		// One per deferred-rendered shader
		ShaderBatch m_DeferredObjectBatches;
		// One per forward-rendered shader
		ShaderBatch m_ForwardObjectBatches;
		ShaderBatch m_ShadowBatch;

		ShaderBatch m_DepthAwareEditorObjBatches;
		ShaderBatch m_DepthUnawareEditorObjBatches;

		ImGuiTextFilter m_MaterialFilter;

		static const u32 NUM_GPU_TIMINGS = 64;
		std::vector<std::array<real, NUM_GPU_TIMINGS>> m_TimestampHistograms;
		u32 m_TimestampHistogramIndex = 0;

		SSAOGenData m_SSAOGenData;
		SSAOBlurDataConstant m_SSAOBlurDataConstant;
		SSAOBlurDataDynamic m_SSAOBlurDataDynamic;
		i32 m_SSAOBlurSamplePixelOffset;
		SSAOSamplingData m_SSAOSamplingData;
		glm::vec2u m_SSAORes;
		bool m_bSSAOBlurEnabled = true;
		bool m_bSSAOStateChanged = false;

		FT_Library m_FTLibrary;

		real m_TAA_ks[2];

		// TODO: Remove
		RenderBatchDirtyFlags m_DirtyFlagBits = (RenderBatchDirtyFlags)RenderBatchDirtyFlag::CLEAN;

		DebugRenderer* m_DebugRenderer = nullptr;

		static std::array<glm::mat4, 6> s_CaptureViews;

		static const i32 CURRENT_RENDERER_SETTINGS_FILE_VERSION = 2;

		DirectoryWatcher* m_ShaderDirectoryWatcher = nullptr;

		std::vector<u32> m_DirtyStaticVertexBufferIndices;
		std::vector<u32> m_DirtyDynamicVertexAndIndexBufferIndices;

		// Maps specialization constant SID to pair of specialization constant ID & value
		std::map<StringID, SpecializationConstantMetaData> m_SpecializationConstants;
		// Maps shader ID to the list of specialization constant SIDs that shader uses
		std::map<ShaderID, std::vector<StringID>> m_ShaderSpecializationConstants;
		// Editor-only cache of specialization constant names
		std::map<StringID, std::string> m_SpecializationConstantNames;

#if COMPILE_SHADER_COMPILER
		struct ShaderCompiler* m_ShaderCompiler = nullptr;
#endif

		// Editor-only
		bool m_bSpecializationConstantsDirty = false;
		bool m_bShaderErrorWindowShowing = true;

	private:
		Renderer& operator=(const Renderer&) = delete;
		Renderer(const Renderer&) = delete;

	};
} // namespace flex
