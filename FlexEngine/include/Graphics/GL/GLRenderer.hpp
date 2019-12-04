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

			virtual void ClearMaterials(bool bDestroyPersistentMats = false) override;

			virtual void Update() override;
			virtual void Draw() override;

			virtual void UpdateVertexData(RenderID renderID, VertexBufferData const* vertexBufferData) override;

			virtual void ReloadShaders() override;
			virtual void LoadFonts(bool bForceRender) override;

			virtual void ReloadSkybox(bool bRandomizeTexture) override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColor(real r, real g, real b) override;

			virtual void OnWindowSizeChanged(i32 width, i32 height) override;

			virtual void OnPreSceneChange() override;

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
			virtual bool FindOrCreateMaterialByName(const std::string& materialName, MaterialID& materialID) override;
			virtual MaterialID GetRenderObjectMaterialID(RenderID renderID) override;

			virtual std::vector<Pair<std::string, MaterialID>> GetValidMaterialNames() const override;

			virtual void DestroyRenderObject(RenderID renderID) override;

			virtual void NewFrame() override;

			virtual PhysicsDebugDrawBase* GetDebugDrawer() override;

			virtual void DrawStringSS(const std::string& str,
				const glm::vec4& color,
				AnchorPoint anchor,
				const glm::vec2& pos, // Positional offset from anchor
				real spacing,
				real scale = 1.0f) override;

			virtual void DrawStringWS(const std::string& str,
				const glm::vec4& color,
				const glm::vec3& pos,
				const glm::quat& rot,
				real spacing,
				real scale = 1.0f) override;

			virtual void DrawAssetBrowserImGui(bool* bShowing) override;
			virtual void DrawImGuiForRenderObject(RenderID renderID) override;

			virtual void RecaptureReflectionProbe() override;

			virtual void RenderObjectStateChanged() override;

		protected:
			virtual bool LoadFont(FontMetaData& fontMetaData, bool bForceRender) override;

			virtual bool LoadShaderCode(ShaderID shaderID) override;

			virtual void SetShaderCount(u32 shaderCount) override;

			virtual void RemoveMaterial(MaterialID materialID) override;

			virtual void FillOutGBufferFrameBufferAttachments(std::vector<Pair<std::string, void*>>& outVec) override;

			virtual void EnqueueScreenSpaceSprites() override;
			virtual void EnqueueWorldSpaceSprites() override;

		private:

			struct TextureHandle
			{
				u32 id = InvalidID;
				GLenum internalFormat = 0x0500; // GL_INVALID_ENUM
				GLenum format = 0x0500; // GL_INVALID_ENUM
				GLenum type = 0x0500; // GL_INVALID_ENUM
				u32 width = 0;
				u32 height = 0;
			};

			friend class GLPhysicsDebugDraw;

			void DestroyRenderObject(RenderID renderID, GLRenderObject* renderObject);

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

			// Draw all static geometry to the given render object's cubemap texture
			void CaptureSceneToCubemap(RenderID cubemapRenderID);
			void GenerateCubemapFromHDREquirectangular(MaterialID cubemapMaterialID, const std::string& environmentMapPath);
			void GeneratePrefilteredMapFromCubemap(MaterialID cubemapMaterialID);
			void GenerateIrradianceSamplerFromCubemap(MaterialID cubemapMaterialID);
			void GenerateBRDFLUT(u32 brdfLUTTextureID, i32 BRDFLUTSize);

			TextureID InitializeBlankTexture(GLenum internalFormat, GLenum format, GLenum type, const std::string& name, const glm::vec2& size);

			void CacheMaterialUniformLocations(MaterialID matID);

			void SwapBuffers();

			void DrawSpriteQuad(const SpriteQuadDrawInfo& drawInfo);
			void DrawTextSS();
			void DrawTextWS();

			void DrawRenderObjectBatch(const GLRenderObjectBatch& batchedRenderObjects, const DrawCallInfo& drawCallInfo);

			bool GetLoadedTexture(const std::string& filePath, GLTexture** texture);

			MaterialID GetNextAvailableMaterialID();

			GLRenderObject* GetRenderObject(RenderID renderID);
			RenderID GetNextAvailableRenderID() const;
			void InsertNewRenderObject(GLRenderObject* renderObject);
			void UnloadShaders();

			void GenerateFrameBufferTexture(u32* handle, i32 index, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void GenerateFrameBufferTexture(TextureHandle& handle, i32 index, const glm::vec2i& size);
			void GenerateFrameBufferTextureFromID(TextureID textureID, i32 index);

			void GenerateDepthBufferTexture(u32* handle, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void GenerateDepthBufferTexture(TextureHandle& handle, const glm::vec2i& size);

			void ResizeFrameBufferTexture(u32 handle, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void ResizeFrameBufferTexture(TextureHandle& handle, const glm::vec2i& size);
			void ResizeFrameBufferTextureFromID(TextureID textureID, const glm::vec2i& size);
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

			void CreateOffscreenFrameBuffer(u32* FBO, u32* RBO, TextureID textureID);

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

			EventReply OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers);
			KeyEventCallback<GLRenderer> m_KeyEventCallback;

			EventReply OnActionEvent(Action action);
			ActionCallback<GLRenderer> m_ActionCallback;

			std::map<MaterialID, GLMaterial> m_Materials;
			std::vector<GLRenderObject*> m_RenderObjects;

			std::vector<GLShader> m_Shaders;
			std::vector<GLTexture*> m_LoadedTextures;

			// TODO: Clean up (make more dynamic)
			u32 viewProjectionUBO = 0;
			u32 viewProjectionCombinedUBO = 0;

			u32 m_GBufferFrameBufferHandle = 0;
			TextureHandle m_GBufferDepthTextureHandle;
			// TODO: Resize all framebuffers automatically by inserting into container
			TextureID m_GBufferTexture0ID = InvalidTextureID; // normal + roughness
			TextureID m_GBufferTexture1ID = InvalidTextureID; // albedo + metallic

			TextureHandle m_ShadowMapTexture;
			std::array<u32, SHADOW_CASCADE_COUNT> m_ShadowMapFBOs;

			u32 m_SSAOFrameBuffer = 0;
			u32 m_SSAOBlurHFrameBuffer = 0;
			u32 m_SSAOBlurVFrameBuffer = 0;
			TextureHandle m_SSAOFBO;
			TextureHandle m_SSAOBlurHFBO;
			TextureHandle m_SSAOBlurVFBO;
			MaterialID m_SSAOMatID = InvalidMaterialID;
			MaterialID m_SSAOBlurHMatID = InvalidMaterialID;
			MaterialID m_SSAOBlurVMatID = InvalidMaterialID;

			GLTexture* m_NoiseTexture = nullptr;

			GLTexture* m_BRDFTexture = nullptr;

			// TODO: Add frame buffer abstraction which internally has a texture ID & an FBO & RBO
			TextureID m_OffscreenTexture0ID = InvalidTextureID;
			u32 m_Offscreen0FBO = 0;
			u32 m_Offscreen0RBO = 0;

			TextureID m_OffscreenTexture1ID = InvalidTextureID;
			u32 m_Offscreen1FBO = 0;
			u32 m_Offscreen1RBO = 0;

			GLenum m_OffscreenDepthBufferInternalFormat = 0x81A6; // GL_DEPTH_COMPONENT24;

			TextureID m_AlphaBGTextureID = InvalidTextureID;
			TextureID m_LoadingTextureID = InvalidTextureID;
			TextureID m_WorkTextureID = InvalidTextureID;

			TextureID m_PointLightIconID = InvalidTextureID;
			TextureID m_DirectionalLightIconID = InvalidTextureID;

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

			MaterialID m_BRDFMatID = InvalidMaterialID;

			std::list<GLRenderObjectBatch> m_DeferredRenderObjectBatches;
			std::list<GLRenderObjectBatch> m_ForwardRenderObjectBatches;
			GLRenderObjectBatch m_ShadowBatch;

			GLRenderObjectBatch m_DepthAwareEditorRenderObjectBatch;
			GLRenderObjectBatch m_DepthUnawareEditorRenderObjectBatch;

			AsynchronousTextureSave* screenshotAsyncTextureSave = nullptr;

			sec m_MonitorResCheckTimeRemaining = 0.0f;

			GLRenderer(const GLRenderer&) = delete;
			GLRenderer& operator=(const GLRenderer&) = delete;
			void GenerateSSAOMaterials();
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL