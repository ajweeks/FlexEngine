#pragma once
#if COMPILE_OPEN_GL

#include "Graphics/Renderer.hpp"

#include "Callbacks/InputCallbacks.hpp"
#include "GLHelpers.hpp"
#include "Graphics/VertexBufferData.hpp"

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

			virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace = InvalidMaterialID) override;
			virtual TextureID InitializeTexture(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) override;
			virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(RenderID renderID) override;

			virtual void ClearMaterials(bool bDestroyEngineMats = false) override;

			virtual void Update() override;
			virtual void Draw() override;

			virtual void UpdateVertexData(RenderID renderID, VertexBufferData* vertexBufferData) override;

			virtual void DrawUntexturedQuad(const glm::vec2& pos, AnchorPoint anchor, const glm::vec2& size, const glm::vec4& color) override;
			virtual void DrawUntexturedQuadRaw(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color) override;
			virtual void DrawSprite(const SpriteQuadDrawInfo& drawInfo) override;

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
				bool bRaw = false) override;

			virtual void DrawStringWS(const std::string& str,
				const glm::vec4& color,
				const glm::vec3& pos,
				const glm::quat& rot,
				real spacing,
				bool bRaw = false) override;

			virtual void DrawAssetBrowserImGui(bool* bShowing) override;
			virtual void DrawImGuiForRenderObject(RenderID renderID) override;

			virtual void RecaptureReflectionProbe() override;

			virtual u32 GetTextureHandle(TextureID textureID) const override;

			virtual void RenderObjectStateChanged() override;

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

			void PhysicsDebugRender();

			void GenerateReflectionProbeMaps(RenderID cubemapRenderID, MaterialID materialID);
			void GenerateIrradianceSamplerMaps(MaterialID materialID);


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
			void DrawTextSS();
			void DrawTextWS();

			// Will attempt to find pre-rendered font at specified path, and
			// only render a new file if not present or if bForceRender is true
			bool LoadFont(BitmapFont** font,
						  i16 size,
						  const std::string& fontFilePath,
						  const std::string& renderedFontFilePath,
						  bool bForceRender,
						  bool bScreenSpace);

			void DrawRenderObjectBatch(const GLRenderObjectBatch& batchedRenderObjects, const DrawCallInfo& drawCallInfo);

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
			void ShadeDeferredObjects(const DrawCallInfo& drawCallInfo);
			void DrawForwardObjects(const DrawCallInfo& drawCallInfo);
			void DrawDepthAwareEditorObjects(const DrawCallInfo& drawCallInfo);
			void DrawDepthUnawareEditorObjects(const DrawCallInfo& drawCallInfo);
			void DrawSelectedObjectWireframe(const DrawCallInfo& drawCallInfo);
			void ApplyPostProcessing();

			// Returns the next binding that would be used
			u32 BindTextures(Shader* shader, GLMaterial* glMaterial, u32 startingBinding = 0);
			// Returns the next binding that would be used
			u32 BindFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding = 0);
			// Returns the next binding that would be used
			u32 BindDeferredFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding = 0);

			void CreateOffscreenFrameBuffer(u32* FBO, u32* RBO, const glm::vec2i& size, TextureHandle& handle);

			void RemoveMaterial(MaterialID materialID);

			// Returns true if object was duplicated
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

			u32 GetActiveRenderObjectCount() const;

			void ComputeDirLightViewProj(glm::mat4& outView, glm::mat4& outProj);

			EventReply OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers);
			KeyEventCallback<GLRenderer> m_KeyEventCallback;

			EventReply OnActionEvent(Action action);
			ActionCallback<GLRenderer> m_ActionCallback;

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
			TextureHandle m_gBuffer_AlbedoAOHandle;

			TextureHandle m_ShadowMapTexture;
			u32 m_ShadowMapFBO = 0;
			i32 m_ShadowMapSize = 4096;
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

			GLenum m_OffscreenDepthBufferInternalFormat = 0x81A6; // GL_DEPTH_COMPONENT24;

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

			u32 m_TextQuadSS_VAO = 0;
			u32 m_TextQuadSS_VBO = 0;
			VertexBufferData m_TextQuadsSSVertexBufferData;

			u32 m_TextQuadWS_VAO = 0;
			u32 m_TextQuadWS_VBO = 0;
			VertexBufferData m_TextQuadsWSVertexBufferData;

			u32 m_CaptureFBO = 0;
			u32 m_CaptureRBO = 0;
			GLenum m_CaptureDepthInternalFormat = 0x81A5; // GL_DEPTH_COMPONENT16;

			glm::vec3 m_ClearColor = { 1.0f, 0.0f, 1.0f };

			glm::mat4 m_CaptureProjection;
			std::array<glm::mat4, 6> m_CaptureViews;

			GameObject* m_SkyBoxMesh = nullptr;

			VertexBufferData m_1x1_NDC_QuadVertexBufferData;
			GLRenderObject* m_1x1_NDC_Quad = nullptr; // A 1x1 quad in NDC space

			std::list<GLRenderObjectBatch> m_DeferredRenderObjectBatches;
			std::list<GLRenderObjectBatch> m_ForwardRenderObjectBatches;

			GLRenderObjectBatch m_DepthAwareEditorRenderObjectBatch;
			GLRenderObjectBatch m_DepthUnawareEditorRenderObjectBatch;

			GLPhysicsDebugDraw* m_PhysicsDebugDrawer = nullptr;

			AsynchronousTextureSave* screenshotAsyncTextureSave = nullptr;

			sec m_MonitorResCheckTimeRemaining = 0.0f;

			GLRenderer(const GLRenderer&) = delete;
			GLRenderer& operator=(const GLRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL