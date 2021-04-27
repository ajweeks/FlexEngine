#pragma once

IGNORE_WARNINGS_PUSH
#include "LinearMath/btIDebugDraw.h"
IGNORE_WARNINGS_POP

#include "Physics/PhysicsDebuggingSettings.hpp"
#include "RendererTypes.hpp"
#include "BitmapFont.hpp"
#include "VertexBufferData.hpp"

class btIDebugDraw;
struct FT_LibraryRec_;
typedef struct FT_LibraryRec_* FT_Library;

namespace flex
{
	class DirectionalLight;
	class DirectoryWatcher;
	class GameObject;
	class MeshComponent;
	class Mesh;
	class ParticleSystem;
	class PointLight;
	class UIMesh;
	struct TextCache;

	class PhysicsDebugDrawBase : public btIDebugDraw
	{
	public:
		virtual ~PhysicsDebugDrawBase() {};

		virtual void Initialize() = 0;
		virtual void Destroy() = 0;
		virtual void DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& colour) = 0;
		virtual void DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& colourFrom, const btVector4& colourTo) = 0;

		virtual void OnPostSceneChange() = 0;

		virtual void flushLines() override;

		void DrawAxes(const btVector3& origin, const btQuaternion& orientation, real scale);

		void UpdateDebugMode();
		void ClearLines();

	protected:
		virtual void Draw() = 0;

		struct LineSegment
		{
			LineSegment() {}

			LineSegment(const btVector3& vStart, const btVector3& vEnd, const btVector3& vColFrom, const btVector3& vColTo)
			{
				memcpy(start, vStart.m_floats, sizeof(real) * 3);
				memcpy(end, vEnd.m_floats, sizeof(real) * 3);
				memcpy(colourFrom, vColFrom.m_floats, sizeof(real) * 3);
				memcpy(colourTo, vColTo.m_floats, sizeof(real) * 3);
				colourFrom[3] = 1.0f;
				colourTo[3] = 1.0f;
			}
			LineSegment(const btVector3& vStart, const btVector3& vEnd, const btVector4& vColFrom, const btVector3& vColTo)
			{
				memcpy(start, vStart.m_floats, sizeof(real) * 3);
				memcpy(end, vEnd.m_floats, sizeof(real) * 3);
				memcpy(colourFrom, vColFrom.m_floats, sizeof(real) * 4);
				memcpy(colourTo, vColTo.m_floats, sizeof(real) * 4);
			}
			real start[3];
			real end[3];
			real colourFrom[4];
			real colourTo[4];
		};

		// TODO: Allocate from pool to reduce startup alloc size (currently 57MB!)
		static const u32 MAX_NUM_LINE_SEGMENTS = 1'048'576;
		u32 m_LineSegmentIndex = 0;
		LineSegment m_LineSegments[MAX_NUM_LINE_SEGMENTS];

		i32 m_DebugMode = 0;

	};

	class Renderer
	{
	public:
		Renderer();
		virtual ~Renderer();

		virtual void Initialize();
		virtual void PostInitialize();
		virtual void Destroy();
		void DestroyPersistentObjects();

		virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace = InvalidMaterialID) = 0;
		virtual TextureID InitializeTextureFromFile(const std::string& relativeFilePath, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) = 0;
		virtual TextureID InitializeTextureFromMemory(void* data, u32 size, VkFormat inFormat, const std::string& name, u32 width, u32 height, u32 channelCount, VkFilter inFilter) = 0;
		virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) = 0;
		virtual void PostInitializeRenderObject(RenderID renderID) = 0; // Only call when creating objects after calling PostInitialize()
		virtual void OnTextureDestroyed(TextureID textureID) = 0;

		virtual void ReplaceMaterialsOnObjects(MaterialID oldMatID, MaterialID newMatID) = 0;

		virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) = 0;
		virtual void SetClearColour(real r, real g, real b) = 0;

		virtual void Update();
		virtual void Draw() = 0;
		virtual void DrawImGuiWindows() = 0;
		virtual void DrawImGuiRendererInfo() = 0;

		virtual void UpdateDynamicVertexData(RenderID renderID, VertexBufferData const* vertexBufferData, const std::vector<u32>& indexData) = 0;
		virtual void FreeDynamicVertexData(RenderID renderID) = 0;
		virtual void ShrinkDynamicVertexData(RenderID renderID, real minUnused = 0.0f) = 0;
		virtual u32 GetDynamicVertexBufferSize(RenderID renderID) = 0;
		virtual u32 GetDynamicVertexBufferUsedSize(RenderID renderID) = 0;

		// Returns true if value changed
		virtual bool DrawImGuiShadersDropdown(i32* selectedShaderIndex, Shader** outSelectedShader = nullptr) = 0;
		virtual bool DrawImGuiShadersList(i32* selectedShaderIndex, bool bShowFilter, Shader** outSelectedShader = nullptr) = 0;
		virtual bool DrawImGuiTextureSelector(const char* label, const std::vector<std::string>& textureNames, i32* selectedIndex) = 0;
		virtual void DrawImGuiShaderErrors() = 0;
		virtual void DrawImGuiTexture(TextureID textureID, real texSize, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) = 0;
		virtual void DrawImGuiTexture(Texture* texture, real texSize, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) = 0;

		virtual void ClearShaderHash(const std::string& shaderName) = 0;
		virtual void RecompileShaders(bool bForce) = 0;

		virtual void OnWindowSizeChanged(i32 width, i32 height) = 0;

		virtual void OnPreSceneChange() = 0;
		virtual void OnPostSceneChange(); // Called once scene has been loaded and all objects have been initialized and post initialized

		/*
		* Fills outInfo with an up-to-date version of the render object's info
		* Returns true if renderID refers to a valid render object
		*/
		virtual bool GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo) = 0;

		virtual void SetVSyncEnabled(bool bEnableVSync) = 0;

		virtual u32 GetRenderObjectCount() const = 0;
		virtual u32 GetRenderObjectCapacity() const = 0;

		virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size, DataType dataType, bool normalized,
			i32 stride, void* pointer) = 0;

		virtual void SetSkyboxMesh(Mesh* skyboxMesh) = 0;

		virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) = 0;

		virtual MaterialID GetRenderObjectMaterialID(RenderID renderID) = 0;

		virtual std::vector<Pair<std::string, MaterialID>> GetValidMaterialNames() const = 0;

		virtual bool DestroyRenderObject(RenderID renderID) = 0;

		virtual void SetGlobalUniform(const Uniform& uniform, void* data, u32 dataSize) = 0;

		virtual void NewFrame();

		virtual void RenderObjectMaterialChanged(MaterialID materialID) = 0;

		virtual PhysicsDebugDrawBase* GetDebugDrawer() = 0;

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

		virtual ParticleSystemID AddParticleSystem(const std::string& name, ParticleSystem* system, i32 particleCount) = 0;
		virtual bool RemoveParticleSystem(ParticleSystemID particleSystemID) = 0;

		virtual void RecreateEverything() = 0;

		virtual void ReloadObjectsWithMesh(const std::string& meshFilePath) = 0;

		// Will attempt to find pre-rendered font at specified path, and
		// only render a new file if not present or if bForceRender is true
		// Returns true if succeeded
		virtual bool LoadFont(FontMetaData& fontMetaData, bool bForceRender) = 0;

		void SetReflectionProbeMaterial(MaterialID reflectionProbeMaterialID);

		i32 GetShortMaterialIndex(MaterialID materialID, bool bShowEditorMaterials);
		// Returns true when the selected material has changed
		bool DrawImGuiMaterialList(i32* selectedMaterialIndexShort, MaterialID* selectedMaterialID, bool bShowEditorMaterials, bool bScrollToSelected);
		void DrawImGuiTexturePreviewTooltip(Texture* texture);
		// Returns true if any property changed
		bool DrawImGuiForGameObject(GameObject* gameObject);

		void DrawImGuiSettings();

		real GetStringWidth(const std::string& str, BitmapFont* font, real letterSpacing, bool bNormalized) const;
		real GetStringHeight(const std::string& str, BitmapFont* font, bool bNormalized) const;

		real GetStringWidth(const TextCache& textCache, BitmapFont* font) const;
		real GetStringHeight(const TextCache& textCache, BitmapFont* font) const;

		void SaveSettingsToDisk(bool bAddEditorStr = true);
		void LoadSettingsFromDisk();

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
		PointLightID RegisterPointLight(PointLightData* pointLightData);
		void UpdatePointLightData(PointLightID ID, PointLightData* data);
		SpotLightID RegisterSpotLight(SpotLightData* spotLightData);
		void UpdateSpotLightData(SpotLightID ID, SpotLightData* data);
		AreaLightID RegisterAreaLight(AreaLightData* areaLightData);
		void UpdateAreaLightData(AreaLightID ID, AreaLightData* data);

		void RemoveDirectionalLight();
		void RemovePointLight(PointLightID ID);
		void RemoveAllPointLights();
		void RemoveSpotLight(SpotLightID ID);
		void RemoveAllSpotLights();
		void RemoveAreaLight(AreaLightID ID);
		void RemoveAllAreaLights();

		DirLightData* GetDirectionalLight();
		PointLightData* GetPointLight(PointLightID ID);
		i32 GetNumPointLights();
		i32 GetNumSpotLights();
		i32 GetNumAreaLights();

		i32 GetFramesRenderedCount() const;

		BitmapFont* SetFont(StringID fontID);
		// Draws the given string in the center of the screen for a short period of time
		// Passing an empty string will immediately clear the current string
		void AddEditorString(const std::string& str);

		i32 GetMaterialCount();
		Material* GetMaterial(MaterialID materialID);
		void RemoveMaterial(MaterialID materialID);
		Shader* GetShader(ShaderID shaderID);
		MaterialID GetNextAvailableMaterialID() const;

		bool MaterialExists(MaterialID materialID);
		bool MaterialWithNameExists(const std::string& matName);

		bool GetShaderID(const std::string& shaderName, ShaderID& shaderID);
		bool FindOrCreateMaterialByName(const std::string& materialName, MaterialID& materialID);

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

		std::vector<JSONObject> SerializeAllMaterialsToJSON();

		void SetDynamicGeometryBufferDirty(u32 dynamicVertexBufferIndex);
		void SetStaticGeometryBufferDirty(u32 staticVertexBufferIndex);

		void ToggleWireframeOverlay();
		void ToggleWireframeSelectionOverlay();

		UIMesh* GetUIMesh();

		bool bUniformBufferWindowShowing = false;
		bool bGPUTimingsWindowShowing = false;

		static const u32 MAX_PARTICLE_COUNT = 65536;
		static const u32 PARTICLES_PER_DISPATCH = 256;
		static const u32 SSAO_NOISE_DIM = 4;

		// TODO: Store in map (string name -> TextureID)
		TextureID blankTextureID = InvalidTextureID; // 1x1 white pixel texture
		TextureID blankTextureArrID = InvalidTextureID; // 1x1 white pixel with texture array with one layer
		TextureID alphaBGTextureID = InvalidTextureID;

		TextureID pointLightIconID = InvalidTextureID;
		TextureID spotLightIconID = InvalidTextureID;
		TextureID areaLightIconID = InvalidTextureID;
		TextureID directionalLightIconID = InvalidTextureID;

		StringID previewedFont = InvalidStringID;

		MaterialID m_SpriteMatSSID = InvalidMaterialID;
		MaterialID m_SpriteMatWSID = InvalidMaterialID;
		MaterialID m_SpriteArrMatID = InvalidMaterialID;

		Texture* m_BlankTexture = nullptr;
		Texture* m_BlankTextureArr = nullptr;
		Texture* m_NoiseTexture = nullptr;

	protected:
		void LoadShaders();
		virtual void InitializeShaders(const std::vector<ShaderInfo>& shaderInfos) = 0;
		virtual bool LoadShaderCode(ShaderID shaderID) = 0;
		virtual void FillOutGBufferFrameBufferAttachments(std::vector<Pair<std::string, void*>>& outVec) = 0;
		virtual void RecreateShadowFrameBuffers() = 0;

		void EnqueueScreenSpaceSprites();
		void EnqueueWorldSpaceSprites();

		void GenerateGBuffer();

		void EnqueueScreenSpaceText();
		void EnqueueWorldSpaceText();

		void InitializeMaterials();

		std::string PickRandomSkyboxTexture();

		u32 UpdateTextBufferSS(std::vector<TextVertex2D>& outTextVertices);
		u32 UpdateTextBufferWS(std::vector<TextVertex3D>& outTextVertices);

		glm::vec4 GetSelectedObjectColourMultiplier() const;
		glm::mat4 GetPostProcessingMatrix() const;

		void GenerateSSAONoise(std::vector<glm::vec4>& noise);

		MaterialID CreateParticleSystemSimulationMaterial(const std::string& name);
		MaterialID CreateParticleSystemRenderingMaterial(const std::string& name);

		u8* m_LightData = nullptr;
		PointLightData* m_PointLightData = nullptr; // Points into m_LightData buffer
		SpotLightData* m_SpotLightData = nullptr; // Points into m_LightData buffer
		AreaLightData* m_AreaLightData = nullptr; // Points into m_LightData buffer
		i32 m_NumPointLightsEnabled = 0;
		i32 m_NumSpotLightsEnabled = 0;
		i32 m_NumAreaLightsEnabled = 0;
		DirectionalLight* m_DirectionalLight = nullptr;

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
		std::vector<SpriteQuadDrawInfo> m_QueuedSSSprites;
		std::vector<SpriteQuadDrawInfo> m_QueuedSSArrSprites;

		std::map<MaterialID, Material*> m_Materials;
		std::vector<Shader*> m_Shaders;

		// TODO: Use a mesh prefab here
		VertexBufferData m_Quad3DVertexBufferData;
		RenderID m_Quad3DRenderID = InvalidRenderID;
		RenderID m_Quad3DSSRenderID = InvalidRenderID;
		VertexBufferData m_FullScreenTriVertexBufferData;
		RenderID m_FullScreenTriRenderID = InvalidRenderID;

		RenderID m_GBufferQuadRenderID = InvalidRenderID;
		// Any editor objects which also require a game object wrapper
		std::vector<GameObject*> m_EditorObjects;

		bool m_bVSyncEnabled = true;
		PhysicsDebuggingSettings m_PhysicsDebuggingSettings;

		/* Objects that are created at bootup and stay active until shutdown, regardless of scene */
		std::vector<GameObject*> m_PersistentObjects;

		BitmapFont* m_CurrentFont = nullptr;

		PostProcessSettings m_PostProcessSettings;

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

		GameObject* m_Grid = nullptr;
		GameObject* m_WorldOrigin = nullptr;
		MaterialID m_GridMaterialID = InvalidMaterialID;
		MaterialID m_WorldAxisMaterialID = InvalidMaterialID;

		sec m_EditorStrSecRemaining = 0.0f;
		sec m_EditorStrSecDuration = 1.5f;
		real m_EditorStrFadeDurationPercent = 0.25f;
		std::string m_EditorMessage;

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

		static const SpecializationConstantID m_SSAOKernelSizeSpecializationID = 0;
		static const SpecializationConstantID m_TAASampleCountSpecializationID = 1;
		static const SpecializationConstantID m_ShaderQualityLevelSpecializationID = 2;
		static const SpecializationConstantID m_ShadowCascadeCountSpecializationID = 3;

		std::string m_RendererSettingsFilePathAbs;

		Mesh* m_SkyBoxMesh = nullptr;
		ShaderID m_SkyboxShaderID = InvalidShaderID;

		ShaderID m_UIShaderID = InvalidShaderID;

		glm::mat4 m_LastFrameViewProj;

		// Contains file paths for each file with a .hdr extension in the `resources/textures/hdri/` directory
		std::vector<std::string> m_AvailableHDRIs;

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

		static const u32 NUM_GPU_TIMINGS = 64;
		std::vector<std::array<real, NUM_GPU_TIMINGS>> m_TimestampHistograms;
		u32 m_TimestampHistogramIndex = 0;

		SSAOGenData m_SSAOGenData;
		SSAOBlurDataConstant m_SSAOBlurDataConstant;
		SSAOBlurDataDynamic m_SSAOBlurDataDynamic;
		i32 m_SSAOKernelSize = MAX_SSAO_KERNEL_SIZE;
		i32 m_SSAOBlurSamplePixelOffset;
		SSAOSamplingData m_SSAOSamplingData;
		glm::vec2u m_SSAORes;
		bool m_bSSAOBlurEnabled = true;
		bool m_bSSAOStateChanged = false;

		FT_Library m_FTLibrary;

		real m_TAA_ks[2];

		// TODO: Remove
		RenderBatchDirtyFlags m_DirtyFlagBits = (RenderBatchDirtyFlags)RenderBatchDirtyFlag::CLEAN;

		PhysicsDebugDrawBase* m_PhysicsDebugDrawer = nullptr;

		static std::array<glm::mat4, 6> s_CaptureViews;

		static const i32 LATEST_RENDERER_SETTINGS_FILE_VERSION = 1;
		i32 m_RendererSettingsFileVersion = 0;

		DirectoryWatcher* m_ShaderDirectoryWatcher = nullptr;

		std::vector<u32> m_DirtyStaticVertexBufferIndices;
		std::vector<u32> m_DirtyDynamicVertexAndIndexBufferIndices;

	private:
		Renderer& operator=(const Renderer&) = delete;
		Renderer(const Renderer&) = delete;

	};
} // namespace flex
