#pragma once

IGNORE_WARNINGS_PUSH
#include "LinearMath/btIDebugDraw.h"
IGNORE_WARNINGS_POP

#include "Physics/PhysicsDebuggingSettings.hpp"
#include "RendererTypes.hpp"

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

	class PhysicsDebugDrawBase : public btIDebugDraw
	{
	public:
		virtual void DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& color) = 0;
	};

	class Renderer
	{
	public:
		Renderer();
		virtual ~Renderer();

		virtual void Initialize();
		virtual void PostInitialize() = 0;
		virtual void Destroy();

		virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace = InvalidMaterialID) = 0;
		virtual TextureID InitializeTexture(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) = 0;
		virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) = 0;
		virtual void PostInitializeRenderObject(RenderID renderID) = 0; // Only call when creating objects after calling PostInitialize()

		virtual void ClearMaterials(bool bDestroyEngineMats = false) = 0;

		virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) = 0;
		virtual void SetClearColor(real r, real g, real b) = 0;

		virtual void Update() = 0;
		virtual void Draw() = 0;
		virtual void DrawImGuiRenderObjects() = 0;
		virtual void DrawImGuiSettings();

		virtual void UpdateVertexData(RenderID renderID, VertexBufferData* vertexBufferData) = 0;

		void DrawImGuiForGameObjectWithValidRenderID(GameObject* gameObject);

		virtual void DrawUntexturedQuad(const glm::vec2& pos, AnchorPoint anchor, const glm::vec2& size, const glm::vec4& color) = 0;
		virtual void DrawUntexturedQuadRaw(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color) = 0;
		virtual void DrawSprite(const SpriteQuadDrawInfo& drawInfo) = 0;

		virtual void UpdateRenderObjectVertexData(RenderID renderID) = 0;

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
			bool bRaw = false) = 0;

		virtual void DrawStringWS(const std::string& str,
			const glm::vec4& color,
			const glm::vec3& pos,
			const glm::quat& rot,
			real spacing,
			bool bRaw = false) = 0;

		virtual void DrawAssetBrowserImGui(bool* bShowing) = 0;
		virtual void DrawImGuiForRenderObject(RenderID renderID) = 0;

		virtual void RecaptureReflectionProbe() = 0;

		virtual u32 GetTextureHandle(TextureID textureID) const = 0;

		// Call whenever a user-controlled field, such as visibility, changes to rebatch render objects
		virtual void RenderObjectStateChanged() = 0;

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

		void SetPostProcessingEnabled(bool bEnabled);
		bool IsPostProcessingEnabled() const;

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
			bool bEnabled = true;

			real saturation = 1.0f;
			glm::vec3 brightness;
			glm::vec3 offset;
			bool bEnableFXAA = true;
			bool bEnableFXAADEBUGShowEdges = false;
		};

		PostProcessSettings& GetPostProcessSettings();
		
	protected:
		// If the object gets deleted this frame *gameObjectRef gets set to nullptr
		void DoCreateGameObjectButton(const char* buttonName, const char* popupName);

		// Returns true if the parent-child tree changed during this call
		bool DrawImGuiGameObjectNameAndChildren(GameObject* gameObject);

		bool LoadFontMetrics(const std::vector<char>& fileMemory, const std::string& fontFilePath,FT_Library& ft, BitmapFont** font,
			i16 size, bool bScreenSpace, std::map<i32, struct FontMetric*>* outCharacters,
			std::array<glm::vec2i, 4>* outMaxPositions, FT_Face* outFace);

		std::string PickRandomSkyboxTexture();

		void UpdateTextBufferSS(std::vector<TextVertex2D>& outTextVertices);
		void UpdateTextBufferWS(std::vector<TextVertex3D>& outTextVertices);

		PointLightData* m_PointLights;
		i32 m_NumPointLightsEnabled = 0;
		DirectionalLight* m_DirectionalLight = nullptr;

		struct DrawCallInfo
		{
			bool bRenderToCubemap = false;
			bool bDeferred = false;
			bool bWireframe = false;
			bool bWriteToDepth = true;
			// NOTE: DepthTestFunc only supported in GL, Vulkan requires specification at pipeline creation time
			DepthTestFunc depthTestFunc = DepthTestFunc::GEQUAL;
			RenderID cubemapObjectRenderID = InvalidRenderID;
			MaterialID materialOverride = InvalidMaterialID;
		};

		MaterialID m_ReflectionProbeMaterialID = InvalidMaterialID; // Set by the user via SetReflecionProbeMaterial

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

		bool m_bPostProcessingEnabled = true;
		bool m_bDisplayBoundingVolumes = false;

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
		sec m_EditorStrSecDuration = 1.15f;
		real m_EditorStrFadeDurationPercent = 0.25f;
		std::string m_EditorMessage;

		MaterialID m_SpriteMatID = InvalidMaterialID;
		MaterialID m_FontMatSSID = InvalidMaterialID;
		MaterialID m_FontMatWSID = InvalidMaterialID;
		MaterialID m_ShadowMatID = InvalidMaterialID;
		MaterialID m_PostProcessMatID = InvalidMaterialID;
		MaterialID m_PostFXAAMatID = InvalidMaterialID;
		MaterialID m_SelectedObjectMatID = InvalidMaterialID;

		std::string m_FontImageExtension = ".png";
		struct FontMetaData
		{
			FontMetaData()
			{}

			FontMetaData(const std::string& filePath, const std::string& renderedTextureFilePath, i16 size,
				bool bScreenSpace, BitmapFont* bitmapFont) :
				filePath(filePath),
				renderedTextureFilePath(renderedTextureFilePath),
				size(size),
				bScreenSpace(bScreenSpace),
				bitmapFont(bitmapFont)
			{
			}

			std::string filePath;
			std::string renderedTextureFilePath;
			i16 size;
			bool bScreenSpace;
			BitmapFont* bitmapFont = nullptr;
		};
		std::map<StringID, FontMetaData> m_Fonts;

		std::string m_DefaultSettingsFilePathAbs;
		std::string m_SettingsFilePathAbs;
		std::string m_FontsFilePathAbs;

		// Must be 12 chars or less
		const char* m_GameObjectPayloadCStr = "gameobject";
		const char* m_MaterialPayloadCStr = "material";
		const char* m_MeshPayloadCStr = "mesh";

		// Contains file paths for each file with a .hdr extension in the `resources/textures/hdri/` directory
		std::vector<std::string> m_AvailableHDRIs;

	private:
		Renderer& operator=(const Renderer&) = delete;
		Renderer(const Renderer&) = delete;

	};
} // namespace flex
