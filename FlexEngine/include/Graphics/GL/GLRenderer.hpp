#pragma once
#if COMPILE_OPEN_GL

#include "Graphics/Renderer.hpp"

#include <map>

#include "GLHelpers.hpp"
#include "Helpers.hpp"
#include "Types.hpp"
#include "VertexBufferData.hpp"

namespace flex
{
	class MeshComponent;
	class GameObject;

	namespace gl
	{
		class GLPhysicsDebugDraw;

		class GLRenderer : public Renderer
		{
		public:
			GLRenderer();
			virtual ~GLRenderer();

			virtual void Initialize() override;
			virtual void PostInitialize() override;
			virtual void Destroy() override;

			virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo) override;
			virtual TextureID InitializeTexture(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) override;
			virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(RenderID renderID) override;

			virtual void ClearRenderObjects() override;
			virtual void ClearMaterials() override;

			virtual void Update() override;
			virtual void Draw() override;
			virtual void DrawImGuiItems() override;

			virtual void DrawUntexturedQuad(const glm::vec2& pos, AnchorPoint anchor, const glm::vec2& size, const glm::vec4& color) override;
			virtual void DrawUntexturedQuadRaw(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color) override;
			virtual void DrawSprite(const SpriteQuadDrawInfo& drawInfo) override;

			virtual void UpdateRenderObjectVertexData(RenderID renderID) override;

			virtual void ReloadShaders() override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColor(real r, real g, real b) override;

			virtual void OnWindowSizeChanged(i32 width, i32 height) override;

			virtual void OnSceneChanged() override;

			virtual bool GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo) override;

			virtual void SetVSyncEnabled(bool enableVSync) override;
			virtual bool GetVSyncEnabled() override;

			virtual u32 GetRenderObjectCount() const override;
			virtual u32 GetRenderObjectCapacity() const override;
			u32 GetActiveRenderObjectCount() const;

			virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size, DataType dataType, bool normalized, i32 stride, void* pointer) override;

			virtual void SetSkyboxMesh(GameObject* skyboxMesh) override;
			virtual GameObject* GetSkyboxMesh() override;
			virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) override;

			virtual Material& GetMaterial(MaterialID materialID) override;
			virtual Shader& GetShader(ShaderID shaderID) override;

			virtual bool GetShaderID(const std::string& shaderName, ShaderID& shaderID) override;
			virtual bool GetMaterialID(const std::string& materialName, MaterialID& materialID) override;

			virtual void DestroyRenderObject(RenderID renderID) override;

			virtual void NewFrame() override;

			virtual btIDebugDraw* GetDebugDrawer() override;

			virtual void SetFont(BitmapFont* font) override;
			// Draws the given string in the center of the screen for a short period of time
			// Passing an empty string will immediately clear the current string
			virtual void AddEditorString(const std::string& str) override;
			virtual void DrawString(const std::string& str,
									const glm::vec4& color,
									AnchorPoint anchor,
									const glm::vec2& pos, // Positional offset from anchor
									real spacing,
									bool bRaw,
									const std::vector<glm::vec2>& letterYOffsets) override; // Horizontal per-char spacing

			virtual void SaveSettingsToDisk(bool bSaveOverDefaults = false, bool bAddEditorStr = true) override;
			virtual void LoadSettingsFromDisk(bool bLoadDefaults = false) override;

			virtual real GetStringWidth(const std::string& str, BitmapFont* font, real letterSpacing, bool bNormalized) const override;
			virtual real GetStringHeight(const std::string& str, BitmapFont* font, bool bNormalized) const override;

			virtual void DrawAssetBrowserImGui() override;

			virtual void RecaptureReflectionProbe() override;

			virtual u32 GetTextureHandle(TextureID textureID) const override;

			virtual void RenderObjectStateChanged() override;

			virtual void SetRenderGrid(bool bRenderGrid) override;

			real GetStringWidth(const TextCache& textCache, BitmapFont* font) const;
			real GetStringHeight(const TextCache& textCache, BitmapFont* font) const;

			void ComputeDirLightViewProj(glm::mat4& outView, glm::mat4& outProj);

		private:
			struct TextureHandle
			{
				u32 id;
				GLenum internalFormat;
				GLenum format;
				GLenum type;
			};

			friend class GLPhysicsDebugDraw;

			void DestroyRenderObject(RenderID renderID, GLRenderObject* renderObject);

			void DrawGameObjectImGui(GameObject* gameObject);
			/*
			* Returns true if the parent-child tree changed during this call
			*/
			bool DrawGameObjectNameAndChildren(GameObject* gameObject);

			void PhysicsDebugRender();


			// TODO: Either use these functions or remove them
			void SetFloat(ShaderID shaderID, const char* valName, real val);
			void SetInt(ShaderID shaderID, const char* valName, i32 val);
			void SetUInt(ShaderID shaderID, const char* valName, u32 val);
			void SetVec2f(ShaderID shaderID, const char* vecName, const glm::vec2& vec);
			void SetVec3f(ShaderID shaderID, const char* vecName, const glm::vec3& vec);
			void SetVec4f(ShaderID shaderID, const char* vecName, const glm::vec4& vec);
			void SetMat4f(ShaderID shaderID, const char* matName, const glm::mat4& mat);

			void GenerateGBufferVertexBuffer();
			void GenerateGBuffer();

			// Draw all static geometry to the given render object's cubemap texture
			void CaptureSceneToCubemap(RenderID cubemapRenderID);
			void GenerateCubemapFromHDREquirectangular(MaterialID cubemapMaterialID, const std::string& environmentMapPath);
			void GeneratePrefilteredMapFromCubemap(MaterialID cubemapMaterialID);
			void GenerateIrradianceSamplerFromCubemap(MaterialID cubemapMaterialID);
			void GenerateBRDFLUT(u32 brdfLUTTextureID, i32 BRDFLUTSize);

			void SwapBuffers();

			void DrawSpriteQuad(const SpriteQuadDrawInfo& drawInfo);
			void DrawScreenSpaceSprites();
			void DrawWorldSpaceSprites();
			void DrawText();

			// Will attempt to find pre-rendered font at renderedFontFilePath, and only if non-existent
			// render a new file
			bool LoadFont(BitmapFont** font,
						  i16 size,
						  const std::string& fontFilePath,
						  const std::string& renderedFontFilePath);

			void UpdateTextBuffer();

			void DrawRenderObjectBatch(const std::vector<GLRenderObject*>& batchedRenderObjects, const DrawCallInfo& drawCallInfo);

			bool GetLoadedTexture(const std::string& filePath, GLTexture** texture);

			MaterialID GetNextAvailableMaterialID();

			GLRenderObject* GetRenderObject(RenderID renderID);
			RenderID GetNextAvailableRenderID() const;
			void InsertNewRenderObject(GLRenderObject* renderObject);
			void UnloadShaders();
			void LoadShaders();

			void GenerateFrameBufferTexture(u32* handle, i32 index, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void ResizeFrameBufferTexture(u32 handle, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void ResizeRenderBuffer(u32 handle, const glm::vec2i& size, GLenum internalFormat);

			void UpdateAllMaterialUniforms();
			void UpdatePerObjectUniforms(RenderID renderID, GLMaterial* materialOverride = nullptr);
			void UpdatePerObjectUniforms(GLMaterial* material, const glm::mat4& model);

			void BatchRenderObjects();
			void DrawShadowDepthMaps();
			void DrawDeferredObjects(const DrawCallInfo& drawCallInfo);
			// Draws the GBuffer quad, or the GBuffer cube if rendering to a cubemap
			void DrawGBufferContents(const DrawCallInfo& drawCallInfo);
			void DrawForwardObjects(const DrawCallInfo& drawCallInfo);
			void DrawEditorObjects(const DrawCallInfo& drawCallInfo);
			void DrawOffscreenTexture();

			// Returns the next binding that would be used
			u32 BindTextures(Shader* shader, GLMaterial* glMaterial, u32 startingBinding = 0);
			// Returns the next binding that would be used
			u32 BindFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding = 0);
			// Returns the next binding that would be used
			u32 BindDeferredFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding = 0);

			void CreateOffscreenFrameBuffer(u32* FBO, u32* RBO, const glm::vec2i& size, TextureHandle& handle);

			void RemoveMaterial(MaterialID materialID);

			// If the object gets deleted this frame *gameObjectRef gets set to nullptr
			void DoGameObjectContextMenu(GameObject** gameObjectRef);
			void DoCreateGameObjectButton(const char* buttonName, const char* popupName);
			// Returns true if object was duplicated
			bool DoDuplicateGameObjectButton(GameObject* objectToCopy, const char* buttonName);
			bool DoTextureSelector(const char* label, const std::vector<GLTexture*>& textures, i32* selectedIndex, bool* bGenerateSampler);
			void ImGuiUpdateTextureIndexOrMaterial(bool bUpdateTextureMaterial,
											  const std::string& texturePath,
											  std::string& matTexturePath,
											  GLTexture* texture,
											  i32 i,
											  i32* textureIndex,
											  u32* samplerID);
			void DoTexturePreviewTooltip(GLTexture* texture);

			void DrawLoadingTextureQuad();

			std::map<MaterialID, GLMaterial> m_Materials;
			std::vector<GLRenderObject*> m_RenderObjects;

			std::vector<GLShader> m_Shaders;
			std::vector<GLTexture*> m_LoadedTextures;

			// Filled every frame
			std::vector<SpriteQuadDrawInfo> m_QueuedWSSprites;
			std::vector<SpriteQuadDrawInfo> m_QueuedSSSprites;

			// TODO: Clean up (make more dynamic)
			u32 viewProjectionUBO = 0;
			u32 viewProjectionCombinedUBO = 0;

			RenderID m_GBufferQuadRenderID = InvalidRenderID;
			VertexBufferData m_gBufferQuadVertexBufferData;
			u32 m_gBufferHandle = 0;
			u32 m_gBufferDepthHandle = 0;

			// TODO: Resize all framebuffers automatically by inserting into container
			// TODO: Remove ??
			TextureHandle m_gBuffer_PositionMetallicHandle;
			TextureHandle m_gBuffer_NormalRoughnessHandle;
			TextureHandle m_gBuffer_DiffuseAOHandle;

			TextureHandle m_ShadowMapTexture;
			u32 m_ShadowMapFBO = 0;
			i32 m_ShadowMapSize = 1024;
			MaterialID m_ShadowMaterialID = InvalidMaterialID;

			GLTexture* m_BRDFTexture = nullptr;

			// Everything is drawn to this texture before being drawn to the default
			// frame buffer through some post-processing effects
			TextureHandle m_OffscreenTexture0Handle;
			u32 m_Offscreen0FBO = 0;
			u32 m_Offscreen0RBO = 0;

			TextureHandle m_OffscreenTexture1Handle;
			u32 m_Offscreen1FBO = 0;
			u32 m_Offscreen1RBO = 0;

			GLenum m_OffscreenDepthBufferInternalFormat = GL_DEPTH_COMPONENT24;

			TextureID m_AlphaBGTextureID = InvalidTextureID;
			TextureID m_LoadingTextureID = InvalidTextureID;
			TextureID m_WorkTextureID = InvalidTextureID;

			TextureID m_PointLightIconID = InvalidTextureID;
			TextureID m_DirectionalLightIconID = InvalidTextureID;

			// TODO: Use a mesh prefab here
			VertexBufferData m_Quad3DVertexBufferData;
			RenderID m_Quad3DRenderID;
			VertexBufferData m_Quad2DVertexBufferData;
			RenderID m_Quad2DRenderID;

			struct TextVertex
			{
				glm::vec2 pos;
				glm::vec2 uv;
				glm::vec4 color;
				glm::vec4 charSizePixelsCharSizeNorm; // RG: char size in pixels, BA: char size in [0, 1] in screenspace
				i32 channel; // uses extra ints slot
			};

			u32 m_TextQuadVBO = 0;
			u32 m_TextQuadVAO = 0;
			VertexBufferData m_TextQuadsVertexBufferData;

			sec m_EditorStrSecRemaining = 0.0f;
			sec m_EditorStrSecDuration = 1.15f;
			real m_EditorStrFadeDurationPercent = 0.25f;
			std::string m_EditorMessage;

			MaterialID m_SpriteMatID = InvalidMaterialID;
			MaterialID m_FontMatID = InvalidMaterialID;
			MaterialID m_ShadowMatID = InvalidMaterialID;
			MaterialID m_PostProcessMatID = InvalidMaterialID;
			MaterialID m_PostFXAAMatID = InvalidMaterialID;
			MaterialID m_SelectedObjectMatID = InvalidMaterialID;

			u32 m_CaptureFBO = 0;
			u32 m_CaptureRBO = 0;
			GLenum m_CaptureDepthInternalFormat = GL_DEPTH_COMPONENT16;

			glm::vec3 m_ClearColor = { 1.0f, 0.0f, 1.0f };

			glm::mat4 m_CaptureProjection;
			std::array<glm::mat4, 6> m_CaptureViews;

			GameObject* m_SkyBoxMesh = nullptr;

			VertexBufferData m_1x1_NDC_QuadVertexBufferData;
			Transform m_1x1_NDC_QuadTransform;
			GLRenderObject* m_1x1_NDC_Quad = nullptr; // A 1x1 quad in NDC space

			// The transform to be used for all objects who don't specify one in their
			// create info. Always set to identity.
			//Transform m_DefaultTransform;

			std::vector<std::vector<GLRenderObject*>> m_DeferredRenderObjectBatches;
			std::vector<std::vector<GLRenderObject*>> m_ForwardRenderObjectBatches;
			// All render objects which have "editorObject" set to true

			// Any editor objects which also require a game object wrapper
			std::vector<GameObject*> m_EditorObjects;
			std::vector<GLRenderObject*> m_EditorRenderObjectBatch;

			GLPhysicsDebugDraw* m_PhysicsDebugDrawer = nullptr;

			FT_Library ft;

			std::string m_DefaultSettingsFilePathAbs;
			std::string m_SettingsFilePathAbs;

			// Must be 12 chars or less
			const char* m_GameObjectPayloadCStr = "gameobject";
			const char* m_MaterialPayloadCStr = "material";
			const char* m_MeshPayloadCStr = "mesh";

			bool m_bShowingAssetBrowser = false;
			bool m_bRebatchRenderObjects = true;

			GameObject* m_Grid = nullptr;
			GameObject* m_WorldOrigin = nullptr;
			MaterialID m_GridMaterialID = InvalidMaterialID;
			MaterialID m_WorldAxisMaterialID = InvalidMaterialID;

			GLRenderer(const GLRenderer&) = delete;
			GLRenderer& operator=(const GLRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL