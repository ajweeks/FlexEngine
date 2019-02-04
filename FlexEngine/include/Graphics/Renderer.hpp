#pragma once

#include <string>
#include <vector>

#include "Physics/PhysicsDebuggingSettings.hpp"
#include "RendererTypes.hpp"

class btIDebugDraw;

namespace flex
{
	class MeshComponent;
	class BitmapFont;
	class GameObject;
	class PointLight;
	class DirectionalLight;

	class Renderer
	{
	public:
		Renderer();
		virtual ~Renderer();

		virtual void Initialize() = 0;
		virtual void PostInitialize() = 0;
		virtual void Destroy() = 0;

		virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace = InvalidMaterialID) = 0;
		virtual TextureID InitializeTexture(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) = 0;
		virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) = 0;
		virtual void PostInitializeRenderObject(RenderID renderID) = 0; // Only call when creating objects after calling PostInitialize()

		virtual void ClearMaterials() = 0;

		virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) = 0;
		virtual void SetClearColor(real r, real g, real b) = 0;

		virtual void Update() = 0;
		virtual void Draw() = 0;
		virtual void DrawImGuiRenderObjects() = 0;
		virtual void DrawImGuiSettings();

		virtual void DrawImGuiForRenderID(RenderID renderID) = 0;

		virtual void DrawUntexturedQuad(const glm::vec2& pos, AnchorPoint anchor, const glm::vec2& size, const glm::vec4& color) = 0;
		virtual void DrawUntexturedQuadRaw(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color) = 0;
		virtual void DrawSprite(const SpriteQuadDrawInfo& drawInfo) = 0;

		virtual void UpdateRenderObjectVertexData(RenderID renderID) = 0;

		virtual void ReloadShaders() = 0;
		virtual void LoadFonts(bool bForceRender) = 0;

		virtual void ReloadSkybox(bool bRandomizeTexture) = 0;

		virtual void OnWindowSizeChanged(i32 width, i32 height) = 0;

		virtual void OnSceneChanged() = 0;

		/*
		* Fills outInfo with an up-to-date version of the render object's info
		* Returns true if renderID refers to a valid render object
		*/
		virtual bool GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo) = 0;

		virtual void SetVSyncEnabled(bool enableVSync) = 0;

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
		virtual bool GetShaderID(const std::string& shaderName, ShaderID& shaderID) = 0;

		virtual void DestroyRenderObject(RenderID renderID) = 0;

		virtual void NewFrame() = 0;

		virtual void SetReflectionProbeMaterial(MaterialID reflectionProbeMaterialID);

		virtual btIDebugDraw* GetDebugDrawer() = 0;

		virtual void SetFont(BitmapFont* font) = 0;
		virtual void AddEditorString(const std::string& str) = 0;

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

		virtual void SaveSettingsToDisk(bool bSaveOverDefaults = false, bool bAddEditorStr = true) = 0;
		virtual void LoadSettingsFromDisk(bool bLoadDefaults = false) = 0;

		virtual real GetStringWidth(const std::string& str, BitmapFont* font, real letterSpacing, bool bNormalized) const = 0;
		virtual real GetStringHeight(const std::string& str, BitmapFont* font, bool bNormalized) const = 0;

		virtual void DrawAssetBrowserImGui(bool* bShowing) = 0;

		virtual void RecaptureReflectionProbe() = 0;

		virtual u32 GetTextureHandle(TextureID textureID) const = 0;

		// Call whenever a user-controlled field, such as visibility, changes to rebatch render objects
		virtual void RenderObjectStateChanged() = 0;

		void ToggleRenderGrid();
		bool IsRenderingGrid() const;
		virtual void SetRenderGrid(bool bRenderGrid);

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

		void SetDisplayBoundingVolumesEnabled(bool bEnabled);
		bool IsDisplayBoundingVolumesEnabled()const;

		PhysicsDebuggingSettings& GetPhysicsDebuggingSettings();

		bool RegisterDirectionalLight(DirectionalLight* dirLight);
		PointLightID RegisterPointLight(PointLight* pointLight);

		void RemoveDirectionalLight();
		void RemovePointLight(const PointLight* pointLight);
		void RemoveAllPointLights();

		DirectionalLight* GetDirectionalLight();
		PointLight* GetPointLight(PointLightID pointLight);
		i32 GetNumPointLights();

		i32 GetFramesRenderedCount() const;

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

		static const u32 MAX_TEXTURE_DIM = 65536;
		static const u32 MAX_POINT_LIGHT_COUNT = 4;

		BitmapFont* m_FntUbuntuCondensedSS = nullptr;
		BitmapFont* m_FntSourceCodeProWS = nullptr;
		BitmapFont* m_FntSourceCodeProSS = nullptr;
		BitmapFont* m_FntGantSS = nullptr;

	protected:
		std::vector<PointLight*> m_PointLights;
		DirectionalLight* m_DirectionalLight = nullptr;

		struct DrawCallInfo
		{
			bool bRenderToCubemap = false;
			bool bDeferred = false;
			bool bWireframe = false;
			bool bWriteToDepth = true;
			DepthTestFunc depthTestFunc = DepthTestFunc::LEQUAL;
			RenderID cubemapObjectRenderID = InvalidRenderID;
			MaterialID materialOverride = InvalidMaterialID;
		};

		MaterialID m_ReflectionProbeMaterialID = InvalidMaterialID; // Set by the user via SetReflecionProbeMaterial

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

		// Must be stored as member because ImGui will not make a copy
		std::string m_ImGuiIniFilepathStr;
		std::string m_ImGuiLogFilepathStr;

	private:
		Renderer& operator=(const Renderer&) = delete;
		Renderer(const Renderer&) = delete;

	};
} // namespace flex
