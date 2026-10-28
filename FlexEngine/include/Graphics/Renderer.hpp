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
	class BaseCamera;
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
		// NOTE: Called while ResourceManager's loaded textures mutex is held
		virtual void OnTextureDestroyed(TextureID textureID, Texture* texture) = 0;

		virtual void ReplaceMaterialsOnObjects(MaterialID oldMatID, MaterialID newMatID) = 0;

		virtual void Update();
		virtual void EndOfFrame();
		virtual void Draw() = 0;
		virtual void DrawImGuiWindows();
		virtual void DrawImGuiRendererInfo() = 0;

		virtual void UpdateDynamicVertexData(RenderID renderID, VertexBufferData const* vertexBufferData, const std::vector<u32>& indexData) = 0;
		virtual void FreeDynamicVertexData(RenderID renderID) = 0;
		virtual void ShrinkDynamicVertexData(RenderID renderID, real minUnused = 0.0f) = 0;
		// Grows the dynamic vertex & index buffers a material's meshes are allocated from to at least usedFactor times
		// the space in use. Growing stalls (it allocates & copies the whole buffer), so systems which stream geometry
		// in call this while loading to avoid having to grow during play.
		virtual void ReserveDynamicVertexData(MaterialID materialID, real usedFactor) = 0;
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
		bool IsVSyncEnabled() const { return m_bVSyncEnabled; }

		virtual u32 GetRenderObjectCount() const = 0;
		virtual u32 GetRenderObjectCapacity() const = 0;

		virtual void SetSkyboxMesh(Mesh* skyboxMesh) = 0;

		virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) = 0;
		// Hides an individual render object (game object visibility applies to all of its render objects)
		virtual void SetRenderObjectHidden(RenderID renderID, bool bHidden) = 0;

		// Overrides the shadow distance from the renderer settings (not saved). <= 0 to disable.
		// Used by scenes with large view distances (e.g. streamed terrain).
		void SetShadowDistanceOverride(real distance) { m_ShadowDistanceOverride = distance; }

		// Streamed terrain detail: 0: low, 1: medium, 2: high, 3: ultra
		i32 GetTerrainQuality() const { return m_TerrainQuality; }
		void SetTerrainQuality(i32 quality) { m_TerrainQuality = glm::clamp(quality, 0, 3); }
		real GetEffectiveShadowDistance() const { return (m_ShadowDistanceOverride > 0.0f ? m_ShadowDistanceOverride : m_ShadowDistance) * m_ShadowDistanceScale; }

		// Scalability settings (set by the game's settings screen, not saved in the renderer settings)
		// Sun & torch shadows: 0: off, 1: low, 2: medium, 3: high, 4: ultra (resolution, cascades, distance & point shadows)
		void SetShadowQuality(i32 quality);
		i32 GetShadowQuality() const { return m_ShadowQuality; }
		// 0: off, 1: FXAA, 2: TAA
		void SetAntiAliasing(i32 mode);
		i32 GetAntiAliasing() const;
		void SetSSAOEnabled(bool bEnabled);
		bool IsSSAOEnabled() const { return m_SSAOSamplingData.enabled != 0; }
		// Scales how far streamed terrain is drawn (TerrainGenerator's view distance), (0, 1]
		void SetDrawDistanceScale(real scale) { m_DrawDistanceScale = glm::clamp(scale, 0.05f, 1.0f); }
		real GetDrawDistanceScale() const { return m_DrawDistanceScale; }
		i32 GetShaderQualityLevel() const { return m_ShaderQualityLevel; }

		// Camera-relative rendering: everything the GPU sees (model matrices, view matrices, light positions, terrain
		// vertices after the vertex shader...) is in "render space", world space translated by -origin, where origin is
		// this point near the camera. Far from the world origin that keeps positions near the camera precise in floats. It
		// snaps to a coarse grid on the xz plane (y is always 0, so heights are unchanged), so it rarely changes & is
		// exactly zero near the world origin. Shaders that need absolute coordinates get it as the renderOrigin uniform.
		static glm::vec3 RenderOriginFor(const glm::vec3& cameraPos);
		// This frame's render origin (the current camera's, see BaseCamera::GetRenderOrigin)
		glm::vec3 GetRenderOrigin() const { return m_RenderOrigin; }

		virtual MaterialID GetRenderObjectMaterialID(RenderID renderID) = 0;

		virtual std::vector<Pair<std::string, MaterialID>> GetValidMaterialNames(bool bEditorMaterials) const = 0;

		virtual bool DestroyRenderObject(RenderID renderID) = 0;

		virtual void SetGlobalUniform(Uniform const* uniform, void* data, u32 dataSize) = 0;
		// Removes the global uniform only if it still points at data
		virtual void ClearGlobalUniform(Uniform const* uniform, void* data) = 0;
		virtual void AddRenderObjectUniformOverride(RenderID renderID, Uniform const* uniform, const MaterialPropertyOverride& propertyOverride) = 0;

		virtual void NewFrame();

		virtual void RenderObjectMaterialChanged(MaterialID materialID) = 0;

		virtual DebugRenderer* GetDebugRenderer() = 0;

		// Saves the next presented frame as a PNG (to the screenshots directory when no path is given)
		void RequestScreenshot(const std::string& absoluteFilePath = "");

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
		// Physics (Bullet) & render debug drawing toggles
		void DrawImGuiDebugObjects();

		void AddShaderSpecialziationConstant(ShaderID shaderID, StringID specializationConstant);
		void RemoveShaderSpecialziationConstant(ShaderID shaderID, StringID specializationConstant);
		std::vector<StringID>* GetShaderSpecializationConstants(ShaderID shaderID);
		bool DrawShaderSpecializationConstantImGui(ShaderID shaderID);
		void SetSpecializationConstantDirty();

		real GetStringWidth(const std::string& str, BitmapFont* font, real letterSpacing, bool bNormalized) const;
		real GetStringHeight(const std::string& str, BitmapFont* font, bool bNormalized) const;

		// tracking: extra space after each character (in font layout units)
		real GetStringWidth(const TextCache& textCache, BitmapFont* font, real tracking = 0.0f) const;
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
			glm::vec3 brightness = VEC3_ONE;
			glm::vec3 offset = VEC3_ZERO;
			bool bEnableFXAA = true;
			bool bEnableFXAADEBUGShowEdges = false;
		};

		PostProcessSettings& GetPostProcessSettings();

		MaterialID GetPlaceholderMaterialID() const;

		void SetDisplayShadowCascadePreview(bool bPreview);
		bool GetDisplayShadowCascadePreview() const;

		void SetTAAEnabled(bool bEnabled);

		// Underwater look for this frame: amount [0, 1] (0: above water), depth: meters below the surface. Replaces the
		// fog & sky gradient with murky water (so the scene fades out a few meters away) & tints the image.
		void SetUnderwater(real amount, real depth);
		real GetUnderwaterAmount() const { return m_UnderwaterAmount; }
		// The fog colour used underwater (display space), darker in deep water & at night
		glm::vec3 GetUnderwaterFogColour(const glm::vec3& sunColour) const;
		bool IsTAAEnabled() const;

		i32 GetTAASampleCount() const;

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
		// Cache of PrefabID -> mesh file path to avoid repeated Destroy/LoadFromFile
		std::map<PrefabID, std::string> m_HologramMeshCache;

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
		virtual u32 GetDynamicVertexIndexBufferIndex(u32 stride, bool bDeviceLocal = false) = 0;

		void ParseShaderMetaData();
		u32 ParseShaderBufferFields(const std::vector<std::string>& fileLines, u32 j, std::vector<std::string>& outFields);
		void LoadShaders();

		void EnqueueScreenSpaceSprites();
		void EnqueueWorldSpaceSprites();

		// Fits cascades to the camera's frustum out to m_ShadowDistance
		void UpdateShadowCascades(BaseCamera* cam);

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
			// When rendering shadows, used to cull objects outside of the cascade
			const glm::mat4* shadowCascadeViewProj = nullptr;
			// When set, objects (with cull spheres) entirely outside of these four planes (xyz: normal, w: distance) aren't drawn
			const glm::vec4* cullPlanes = nullptr;
			// Objects are only culled when they're also outside the planes after being moved by this much (shadow casters
			// affect what's behind them, away from the light)
			glm::vec3 cullExtrusion = VEC3_ZERO;

			// NOTE: DepthTestFunc only supported in GL, Vulkan requires specification at pipeline creation time
			DepthTestFunc depthTestFunc = DepthTestFunc::GEQUAL;
			RenderID cubemapObjectRenderID = InvalidRenderID;
			MaterialID materialOverride = InvalidMaterialID;
			CullFace cullFace = CullFace::_INVALID;
		};

		// See GetRenderOrigin
		glm::vec3 m_RenderOrigin = VEC3_ZERO;

		i32 m_ShadowCascadeCount = MAX_SHADOW_CASCADE_COUNT;
		u32 m_ShadowMapBaseResolution = 4096;
		// Distance from the camera that shadow cascades cover (shadows fade out towards this distance)
		real m_ShadowDistance = 200.0f;
		real m_ShadowDistanceOverride = 0.0f;
		real m_ShadowDistanceScale = 1.0f; // See SetShadowQuality
		i32 m_ShadowQuality = 3;
		bool m_bSunShadowsEnabled = true;
		real m_DrawDistanceScale = 1.0f;
		i32 m_TerrainQuality = 1;
		// Blend between uniform (0) and logarithmic (1) cascade split distances
		real m_ShadowCascadeSplitLambda = 0.75f;
		// The furthest the first cascade reaches (in meters), so shadows near the camera stay sharp with long shadow distances
		real m_ShadowNearCascadeDistance = 16.0f;
		// How far (in degrees) the light can turn before shadow maps follow (see UpdateShadowLightDirection). 0: always.
		real m_ShadowLightDirStep = 0.25f;
		glm::vec3 m_ShadowLightDir = VEC3_ZERO;
		// Constant depth bias in world units
		real m_ShadowBias = 0.02f;
		// Slope-scaled depth bias applied when rendering casters, in shadow map texels of depth per texel of slope
		real m_ShadowSlopeBias = 1.5f;
		// The largest slope-scaled bias, in shadow map texels (limits peter-panning on surfaces nearly parallel to the light)
		real m_ShadowSlopeBiasMaxTexels = 6.0f;
		// Light space depth covered by each cascade, in world units
		real m_ShadowCascadeDepthRanges[MAX_SHADOW_CASCADE_COUNT] = {};

		std::vector<glm::mat4> m_ShadowLightViewMats;
		std::vector<glm::mat4> m_ShadowLightProjMats;

		// Distant cascades cover large areas (& so many objects) but change little from frame to frame, so they're
		// re-rendered less often, keeping the matrices they were rendered with. They're padded so the camera can move a
		// little before they no longer cover what they need to (at which point they're re-rendered immediately).
		struct CachedShadowCascade
		{
			glm::vec3 center = VEC3_ZERO;
			real radius = 0.0f;
			glm::vec3 lightDir = VEC3_ZERO;
			real shadowDistance = 0.0f;
			glm::vec3 renderOrigin = VEC3_ZERO; // center is relative to this
			u64 frameRendered = 0;
			bool bValid = false;
		};
		CachedShadowCascade m_CachedShadowCascades[MAX_SHADOW_CASCADE_COUNT];
		// Whether each cascade must be rendered this frame
		bool m_bRenderShadowCascade[MAX_SHADOW_CASCADE_COUNT] = {};
		bool m_bCacheDistantShadowCascades = true;
		u64 m_ShadowCascadeFrameIndex = 0;

		// Shadow map updates cross-fade: when the light direction steps (see UpdateShadowLightDirection), cascades move
		// to the new direction one at a time, each rendered into one spare layer (cascadeCount) which fades in over the
		// old map. Once faded in, the cascade's own layer is re-rendered with the new direction & the spare is free for
		// the next one. So only one cascade's worth of extra memory is needed, not a second set of maps.
		bool m_bShadowUpdateCrossFade = true;
		sec m_ShadowFadeDuration = 0.4f;
		glm::vec3 m_CascadeLightDirs[MAX_SHADOW_CASCADE_COUNT] = {}; // The direction each cascade's layer was rendered with
		i32 m_ShadowFadeCascade = -1; // < 0: none
		real m_ShadowFadeT = 0.0f;
		glm::vec3 m_ShadowFadeLightDir = VEC3_ZERO;
		CachedShadowCascade m_ShadowFadeCache;
		bool m_bRenderShadowFade = false; // Whether the spare layer must be rendered this frame
		real m_ShadowFadeDepthRange = 1.0f;
		bool m_bShadowFadeLayerAllocated = false; // Whether the shadow image has the spare layer

		// Layers in the shadow map image (the cascades, plus a spare one for cross-fading updates)
		i32 GetShadowLayerCount() const { return m_ShadowCascadeCount + (m_bShadowUpdateCrossFade ? 1 : 0); }

		void InvalidateShadowCascadeCache();

		// Point light shadows: a pool of cached cube maps (6 faces in a 2D array each) for the lights nearest the camera,
		// re-rendered only when their casters change, with a per-frame budget of faces (see docs/gi_plan.md)
		struct PointShadowSettings
		{
			bool bEnabled = true;
			// Lights that can have shadows at once (each takes 6 faces)
			i32 slotCount = 8;
			u32 resolution = 256;
			// Most faces rendered per frame (the update cost is bounded by this)
			i32 faceBudget = 12;
			// Casters further than this from a light don't shadow it (the faces' far plane)
			real range = 20.0f;
			// Casters nearer than this to a light are ignored (e.g. the torch's own flame)
			real nearPlane = 0.3f;
			// Shadows fade out towards this distance from the camera
			real distance = 60.0f;
			real normalOffset = 1.5f; // texels
			real slopeBias = 1.5f;
			real compareBias = 0.0002f; // normalized depth
		};
		struct PointShadowStats
		{
			u32 shadowedLights = 0; // Lights with a shadow this frame
			u32 slotsInUse = 0;
			u32 facesRendered = 0; // This frame
			u32 facesDirty = 0; // Waiting for the budget
			u32 drawCalls = 0;
			ms cpuMs = 0.0f; // Slot update & caster scan
		};
		PointShadowSettings m_PointShadowSettings;
		PointShadowStats m_PointShadowStats;
		void DrawPointShadowImGui(bool& bSaveSettings);

		// Directional light shadow cascade helpers (see UpdateShadowCascades)
		void ComputeShadowCascadeSplits(real zNear, real shadowDistance, i32 cascadeCount, real* outSplitDepths) const;
		// Returns the light direction to render shadow maps with (lightDir, quantized)
		glm::vec3 UpdateShadowLightDirection(const glm::vec3& lightDir);
		static void FrustumSliceBoundingSphere(real nearDepth, real farDepth, real tanDiagonalSq, real& outCenterDepth, real& outRadius);

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
		real m_UnderwaterAmount = 0.0f;
		real m_UnderwaterDepth = 0.0f;
		ConfigFile m_Settings;

		UIMesh* m_UIMesh = nullptr;

		u32 m_FramesRendered = 0;

		bool m_bInitialized = false;
		bool m_bPostInitialized = false;
		bool m_bSwapChainNeedsRebuilding = false;
		// Forces all render object batches to be rebuilt from scratch (incremental changes are tracked per render object)
		bool m_bRebatchRenderObjects = true;
		bool m_bRecreateDescriptorSets = false;

		bool m_bEnableWireframeOverlay = false;
		bool m_bEnableSelectionWireframe = false;
		bool m_bDisplayBoundingVolumes = false;
		bool m_bDisplayShadowCascadePreview = false;
		bool m_bRenderGrid = true;

		bool m_bCaptureScreenshot = false;
		std::string m_ScreenshotFilePath;
		bool m_bCaptureReflectionProbes = false;

		bool m_bEnableTAA = false;
		i32 m_TAASampleCount = 2;
		bool m_bTAAStateChanged = false;

		// Persisted in renderer settings & applied to the shader_quality_level specialization constant (-1 = use its default)
		i32 m_ShaderQualityLevel = -1;
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

		// Relative to m_LastFrameRenderOrigin (see GetRenderOrigin)
		glm::mat4 m_LastFrameViewProj;
		glm::vec3 m_LastFrameRenderOrigin = VEC3_ZERO;

		ShadowSamplingData m_ShadowSamplingData;

		Material::PushConstantBlock* m_SpritePerspPushConstBlock = nullptr;
		Material::PushConstantBlock* m_SpriteOrthoPushConstBlock = nullptr;
		Material::PushConstantBlock* m_SpriteOrthoArrPushConstBlock = nullptr;

		Material::PushConstantBlock* m_CascadedShadowMapPushConstantBlock = nullptr;

		// One per deferred-rendered shader
		ShaderBatch m_DeferredObjectBatches;
		// One per forward-rendered shader
		ShaderBatch m_ForwardObjectBatches;

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
