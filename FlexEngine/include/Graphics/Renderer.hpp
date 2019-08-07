#pragma once

IGNORE_WARNINGS_PUSH
#include "LinearMath/btIDebugDraw.h"
IGNORE_WARNINGS_POP

#include "Physics/PhysicsDebuggingSettings.hpp"
#include "RendererTypes.hpp"
#include "VertexBufferData.hpp"

class btIDebugDraw;
struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_  *FT_Library;
typedef struct FT_FaceRec_*  FT_Face;

namespace flex
{
	class BitmapFont;
	class DirectionalLight;
	class GameObject;
	class MeshComponent;
	class PointLight;
	struct TextCache;
	struct FontMetaData;
	struct FontMetric;

	class PhysicsDebugDrawBase : public btIDebugDraw
	{
	public:
		virtual void Initialize() = 0;
		virtual void Destroy() = 0;
		virtual void DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& color) = 0;

		void UpdateDebugMode();
		void ClearLines();

		virtual void flushLines() override;

	protected:
		virtual void Draw() = 0;

		struct LineSegment
		{
			btVector3 start;
			btVector3 end;
			// TODO: Support opacity
			btVector3 color;
		};

		// Gets filled each frame by calls to drawLine, then emptied after debugDrawWorld()
		std::vector<LineSegment> m_LineSegments;
		std::vector<LineSegment> m_pLineSegments;

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

		virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace = InvalidMaterialID) = 0;
		virtual TextureID InitializeTexture(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) = 0;
		virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) = 0;
		virtual void PostInitializeRenderObject(RenderID renderID) = 0; // Only call when creating objects after calling PostInitialize()

		virtual void ClearMaterials(bool bDestroyPersistentMats = false) = 0;

		virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) = 0;
		virtual void SetClearColor(real r, real g, real b) = 0;

		virtual void Update();
		virtual void Draw() = 0;
		virtual void DrawImGuiMisc();
		virtual void DrawImGuiWindows();

		virtual void UpdateVertexData(RenderID renderID, VertexBufferData* vertexBufferData) = 0;

		void DrawImGuiForGameObjectWithValidRenderID(GameObject* gameObject);

		virtual void ReloadShaders() = 0;
		virtual void LoadFonts(bool bForceRender) = 0;

		virtual void ReloadSkybox(bool bRandomizeTexture) = 0;

		virtual void OnWindowSizeChanged(i32 width, i32 height) = 0;

		virtual void OnPreSceneChange() = 0;
		virtual void OnPostSceneChange() = 0; // Called once scene has been loaded and all objects have been inited (and post inited)

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

		virtual void SetSkyboxMesh(GameObject* skyboxMesh) = 0;
		virtual GameObject* GetSkyboxMesh() = 0;

		virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) = 0;

		virtual Material& GetMaterial(MaterialID matID) = 0;
		virtual Shader& GetShader(ShaderID shaderID)  = 0;

		virtual bool GetMaterialID(const std::string& materialName, MaterialID& materialID) = 0;
		virtual MaterialID GetMaterialID(RenderID renderID) = 0;
		virtual bool GetShaderID(const std::string& shaderName, ShaderID& shaderID) = 0;

		virtual std::vector<Pair<std::string, MaterialID>> GetValidMaterialNames() const = 0;

		virtual void DestroyRenderObject(RenderID renderID) = 0;

		virtual void NewFrame() = 0;

		virtual void SetReflectionProbeMaterial(MaterialID reflectionProbeMaterialID);

		virtual PhysicsDebugDrawBase* GetDebugDrawer() = 0;

		virtual void DrawStringSS(const std::string& str,
			const glm::vec4& color,
			AnchorPoint anchor,
			const glm::vec2& pos,
			real spacing,
			real scale = 1.0f) = 0;

		virtual void DrawStringWS(const std::string& str,
			const glm::vec4& color,
			const glm::vec3& pos,
			const glm::quat& rot,
			real spacing,
			real scale = 1.0f) = 0;

		virtual void DrawAssetBrowserImGui(bool* bShowing) = 0;
		virtual void DrawImGuiForRenderObject(RenderID renderID) = 0;

		virtual void RecaptureReflectionProbe() = 0;

		virtual u32 GetTextureHandle(TextureID textureID) const = 0;

		// Call whenever a user-controlled field, such as visibility, changes to rebatch render objects
		virtual void RenderObjectStateChanged() = 0;

		void DrawImGuiRenderObjects();
		void DrawImGuiSettings();

		real GetStringWidth(const std::string& str, BitmapFont* font, real letterSpacing, bool bNormalized) const;
		real GetStringHeight(const std::string& str, BitmapFont* font, bool bNormalized) const;

		real GetStringWidth(const TextCache& textCache, BitmapFont* font) const;
		real GetStringHeight(const TextCache& textCache, BitmapFont* font) const;

		void SaveSettingsToDisk(bool bSaveOverDefaults = false, bool bAddEditorStr = true);
		void LoadSettingsFromDisk(bool bLoadDefaults = false);

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

		void EnqueueUntexturedQuad(const glm::vec2& pos, AnchorPoint anchor, const glm::vec2& size, const glm::vec4& color);
		void EnqueueUntexturedQuadRaw(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color);
		void EnqueueSprite(const SpriteQuadDrawInfo& drawInfo);

		void ToggleRenderGrid();
		bool IsRenderingGrid() const;
		void SetRenderGrid(bool bRenderGrid);

		void SetDisplayBoundingVolumesEnabled(bool bEnabled);
		bool IsDisplayBoundingVolumesEnabled()const;

		PhysicsDebuggingSettings& GetPhysicsDebuggingSettings();

		bool RegisterDirectionalLight(DirectionalLight* dirLight);
		PointLightID RegisterPointLight(PointLightData* pointLightData);
		void UpdatePointLightData(PointLightID ID, PointLightData* data);

		void RemoveDirectionalLight();
		void RemovePointLight(PointLightID ID);
		void RemoveAllPointLights();

		DirLightData* GetDirectionalLight();
		PointLightData* GetPointLight(PointLightID ID);
		i32 GetNumPointLights();

		i32 GetFramesRenderedCount() const;

		BitmapFont* SetFont(StringID fontID);
		// Draws the given string in the center of the screen for a short period of time
		// Passing an empty string will immediately clear the current string
		void AddEditorString(const std::string& str);

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

		bool IsTAAEnabled() const;

		i32 GetTAASampleCount() const;

		bool bFontWindowShowing = false;
		bool bUniformBufferWindowShowing = false;
		bool bGPUTimingsWindowShowing = false;

	protected:
		virtual void LoadShaders();
		virtual bool LoadShaderCode(ShaderID shaderID) = 0;
		virtual void SetShaderCount(u32 shaderCount) = 0;
		virtual void RemoveMaterial(MaterialID materialID) = 0;
		virtual void FillOutGBufferFrameBufferAttachments(std::vector<Pair<std::string, void*>>& outVec) = 0;

		// Will attempt to find pre-rendered font at specified path, and
		// only render a new file if not present or if bForceRender is true
		// Returns true if succeeded
		virtual bool LoadFont(FontMetaData& fontMetaData, bool bForceRender) = 0;

		virtual void EnqueueScreenSpaceSprites();
		virtual void EnqueueWorldSpaceSprites();

		// If the object gets deleted this frame *gameObjectRef gets set to nullptr
		void DoCreateGameObjectButton(const char* buttonName, const char* popupName);

		// Returns true if the parent-child tree changed during this call
		bool DrawImGuiGameObjectNameAndChildren(GameObject* gameObject);

		void GenerateGBuffer();

		void EnqueueScreenSpaceText();
		void EnqueueWorldSpaceText();

		bool LoadFontMetrics(const std::vector<char>& fileMemory,
			FT_Library& ft,
			FontMetaData& metaData,
			std::map<i32, FontMetric*>* outCharacters,
			std::array<glm::vec2i, 4>* outMaxPositions,
			FT_Face* outFace);

		void InitializeMaterials();

		std::string PickRandomSkyboxTexture();

		void UpdateTextBufferSS(std::vector<TextVertex2D>& outTextVertices);
		void UpdateTextBufferWS(std::vector<TextVertex3D>& outTextVertices);

		glm::vec4 GetSelectedObjectColorMultiplier() const;
		glm::mat4 GetPostProcessingMatrix() const;

		void GenerateGBufferVertexBuffer(bool bFlipV);
		void GenerateSSAONoise(std::vector<glm::vec4>& noise);

		std::vector<Shader> m_BaseShaders;

		PointLightData* m_PointLights = nullptr;
		i32 m_NumPointLightsEnabled = 0;
		DirectionalLight* m_DirectionalLight = nullptr;

		struct DrawCallInfo
		{
			bool bRenderToCubemap = false;
			bool bDeferred = false;
			bool bWireframe = false;
			bool bWriteToDepth = true;
			bool bRenderingShadows = false;

			MaterialID materialIDOverride = InvalidMaterialID;

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

		//MaterialID m_CubemapGBufferMaterialID = InvalidMaterialID;

		// TODO: Make tweakable at runtime
		const u32 SHADOW_CASCADE_RES = 2048;
		glm::mat4 m_ShadowLightViewMats[NUM_SHADOW_CASCADES];
		glm::mat4 m_ShadowLightProjMats[NUM_SHADOW_CASCADES];

		// Filled every frame
		std::vector<SpriteQuadDrawInfo> m_QueuedWSSprites;
		std::vector<SpriteQuadDrawInfo> m_QueuedSSSprites;
		std::vector<SpriteQuadDrawInfo> m_QueuedSSArrSprites;

		// TODO: Use a mesh prefab here
		VertexBufferData m_Quad3DVertexBufferData;
		RenderID m_Quad3DRenderID = InvalidRenderID;
		RenderID m_Quad3DSSRenderID = InvalidRenderID;
		VertexBufferData m_FullScreenTriVertexBufferData;
		RenderID m_FullScreenTriRenderID = InvalidRenderID;

		// TODO: Use full screen tri for gbuffer?
		RenderID m_GBufferQuadRenderID = InvalidRenderID;
		VertexBufferData m_gBufferQuadVertexBufferData;

		// Any editor objects which also require a game object wrapper
		std::vector<GameObject*> m_EditorObjects;

		bool m_bVSyncEnabled = true;
		PhysicsDebuggingSettings m_PhysicsDebuggingSettings;

		/* Objects that are created at bootup and stay active until shutdown, regardless of scene */
		std::vector<GameObject*> m_PersistentObjects;

		BitmapFont* m_CurrentFont = nullptr;
		// TODO: Separate fonts from font buffers
		std::vector<BitmapFont*> m_FontsSS;
		std::vector<BitmapFont*> m_FontsWS;

		PostProcessSettings m_PostProcessSettings;

		bool m_bDisplayBoundingVolumes = false;
		bool m_bDisplayShadowCascadePreview = false;

		bool m_bRenderGrid = true;

		u32 m_FramesRendered = 0;

		bool m_bRebatchRenderObjects = true;

		bool m_bCaptureScreenshot = false;
		bool m_bCaptureReflectionProbes = false;

		GameObject* m_Grid = nullptr;
		GameObject* m_WorldOrigin = nullptr;
		MaterialID m_GridMaterialID = InvalidMaterialID;
		MaterialID m_WorldAxisMaterialID = InvalidMaterialID;

		sec m_EditorStrSecRemaining = 0.0f;
		sec m_EditorStrSecDuration = 1.5f;
		real m_EditorStrFadeDurationPercent = 0.25f;
		std::string m_EditorMessage;

		MaterialID m_ReflectionProbeMaterialID = InvalidMaterialID; // Set by the user via SetReflecionProbeMaterial

		MaterialID m_SpriteMatSSID = InvalidMaterialID;
		MaterialID m_SpriteMatWSID = InvalidMaterialID;
		MaterialID m_SpriteArrMatID = InvalidMaterialID;
		MaterialID m_FontMatSSID = InvalidMaterialID;
		MaterialID m_FontMatWSID = InvalidMaterialID;
		MaterialID m_ShadowMaterialID = InvalidMaterialID;
		MaterialID m_PostProcessMatID = InvalidMaterialID;
		MaterialID m_PostFXAAMatID = InvalidMaterialID;
		MaterialID m_SelectedObjectMatID = InvalidMaterialID;
		MaterialID m_TAAResolveMaterialID = InvalidMaterialID;
		//MaterialID m_UIMaterialID = InvalidMaterialID;
		MaterialID m_GammaCorrectMaterialID = InvalidMaterialID;
		MaterialID m_PlaceholderMaterialID = InvalidMaterialID;

		std::string m_FontImageExtension = ".png";

		std::map<StringID, FontMetaData> m_Fonts;

		std::string m_DefaultSettingsFilePathAbs;
		std::string m_SettingsFilePathAbs;
		std::string m_FontsFilePathAbs;

		// Must be 12 chars or less
		const char* m_GameObjectPayloadCStr = "gameobject";
		const char* m_MaterialPayloadCStr = "material";
		const char* m_MeshPayloadCStr = "mesh";

		GameObject* m_SkyBoxMesh = nullptr;

		// Contains file paths for each file with a .hdr extension in the `resources/textures/hdri/` directory
		std::vector<std::string> m_AvailableHDRIs;

		static const u32 SSAO_NOISE_DIM = 4;

		ShadowSamplingData m_ShadowSamplingData;

		SSAOGenData m_SSAOGenData;
		SSAOBlurDataConstant m_SSAOBlurDataConstant;
		SSAOBlurDataDynamic m_SSAOBlurDataDynamic;
		i32 m_SSAOKernelSize = MAX_SSAO_KERNEL_SIZE;
		i32 m_SSAOBlurSamplePixelOffset;
		SSAOSamplingData m_SSAOSamplingData;
		glm::vec2u m_SSAORes;
		bool m_bSSAOBlurEnabled = true;
		bool m_bSSAOStateChanged = false;

		bool m_bEnableTAA = true;

		i32 m_TAASampleCount = 8;
		bool m_bTAAStateChanged = false;

		FXAAData m_FXAAData;

		i32 m_DebugMode = 0;

		PhysicsDebugDrawBase* m_PhysicsDebugDrawer = nullptr;

	private:
		Renderer& operator=(const Renderer&) = delete;
		Renderer(const Renderer&) = delete;

	};
} // namespace flex
