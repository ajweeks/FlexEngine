#pragma once
#if COMPILE_OPEN_GL

#include "Graphics/Renderer.hpp"

#include <map>

#include "Graphics/GL/GLHelpers.hpp"

namespace flex
{
	class MeshComponent;
	class GameObject;
	class BitmapFont;

	namespace gl
	{
		class GLPhysicsDebugDraw;

		class GLRenderer : public Renderer
		{
		public:
			GLRenderer(GameContext& gameContext);
			virtual ~GLRenderer();

			virtual void Initialize(const GameContext& gameContext) override;
			virtual void PostInitialize(const GameContext& gameContext) override;
			virtual void Destroy() override;

			virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) override;
			virtual RenderID InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID) override;

			virtual void ClearRenderObjects() override;
			virtual void ClearMaterials() override;

			virtual void Update(const GameContext& gameContext) override;
			virtual void Draw(const GameContext& gameContext) override;
			virtual void DrawImGuiItems(const GameContext& gameContext) override;

			virtual void UpdateRenderObjectVertexData(RenderID renderID) override;

			virtual void ReloadShaders(GameContext& gameContext) override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColor(real r, real g, real b) override;

			virtual void OnWindowSizeChanged(i32 width, i32 height) override;
			
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

		private:
			friend class GLPhysicsDebugDraw;

			void DestroyRenderObject(RenderID renderID, GLRenderObject* renderObject);

			void ImGuiRender();
			void DrawImGuiForGameObjectAndChildren(GameObject* gameObject);

			void PhysicsDebugRender(const GameContext& gameContext);

			// TODO: Either use these functions or remove them
			void SetFloat(ShaderID shaderID, const std::string& valName, real val);
			void SetUInt(ShaderID shaderID, const std::string& valName, u32 val);
			void SetVec2f(ShaderID shaderID, const std::string& vecName, const glm::vec2& vec);
			void SetVec3f(ShaderID shaderID, const std::string& vecName, const glm::vec3& vec);
			void SetVec4f(ShaderID shaderID, const std::string& vecName, const glm::vec4& vec);
			void SetMat4f(ShaderID shaderID, const std::string& matName, const glm::mat4& mat);

			void GenerateGBuffer(const GameContext& gameContext);

			// Draw all static geometry to the given render object's cubemap texture
			void CaptureSceneToCubemap(const GameContext& gameContext, RenderID cubemapRenderID);
			void GenerateCubemapFromHDREquirectangular(const GameContext& gameContext, MaterialID cubemapMaterialID, const std::string& environmentMapPath);
			void GeneratePrefilteredMapFromCubemap(const GameContext& gameContext, MaterialID cubemapMaterialID);
			void GenerateIrradianceSamplerFromCubemap(const GameContext& gameContext, MaterialID cubemapMaterialID);
			void GenerateBRDFLUT(const GameContext& gameContext, u32 brdfLUTTextureID, glm::vec2 BRDFLUTSize);

			void SwapBuffers(const GameContext& gameContext);

			void DrawSpriteQuad(const GameContext& gameContext, u32 textureHandle, 
								MaterialID materialID,
								const glm::vec3& posOff, const glm::quat& rotationOff, const glm::vec3& scaleOff,
								AnchorPoint anchor,
								const glm::vec4& color);
			void DrawSprites(const GameContext& gameContext);
			void DrawText(const GameContext& gameContext);
			void DrawRenderObjectBatch(const GameContext& gameContext, const std::vector<GLRenderObject*>& batchedRenderObjects, const DrawCallInfo& drawCallInfo);

			bool GetLoadedTexture(const std::string& filePath, u32& handle);

			MaterialID GetNextAvailableMaterialID();

			GLRenderObject* GetRenderObject(RenderID renderID);
			RenderID GetNextAvailableRenderID() const;
			void InsertNewRenderObject(GLRenderObject* renderObject);
			void UnloadShaders();
			void LoadShaders();

			void GenerateFrameBufferTexture(u32* handle, i32 index, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void ResizeFrameBufferTexture(u32 handle, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void ResizeRenderBuffer(u32 handle, const glm::vec2i& size);

			void UpdateMaterialUniforms(const GameContext& gameContext, MaterialID materialID);
			void UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext);
			void UpdatePerObjectUniforms(MaterialID materialID, const glm::mat4& model, const GameContext& gameContext);

			void BatchRenderObjects(const GameContext& gameContext);
			void DrawDeferredObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void DrawGBufferQuad(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void DrawForwardObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void DrawOffscreenTexture(const GameContext& gameContext);

			// Returns the next binding that would be used
			u32 BindTextures(Shader* shader, GLMaterial* glMaterial, u32 startingBinding = 0);
			// Returns the next binding that would be used
			u32 BindFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding = 0);
			// Returns the next binding that would be used
			u32 BindDeferredFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding = 0);

			bool LoadFont(const GameContext& gameContext, const std::string& filePath, i32 size);

			std::map<MaterialID, GLMaterial> m_Materials;
			std::vector<GLRenderObject*> m_RenderObjects;

			// TODO: Convert to map?
			std::vector<GLShader> m_Shaders;
			std::map<std::string, u32> m_LoadedTextures; // Key is filepath, value is texture id

			// TODO: Clean up (make more dynamic)
			u32 viewProjectionUBO = 0;
			u32 viewProjectionCombinedUBO = 0;

			RenderID m_GBufferQuadRenderID = InvalidRenderID;
			VertexBufferData m_gBufferQuadVertexBufferData;
			u32 m_gBufferHandle = 0;
			u32 m_gBufferDepthHandle = 0;

			struct FrameBufferHandle
			{
				u32 id;
				GLenum format;
				GLenum internalFormat;
				GLenum type;
			};

			// TODO: Resize all framebuffers automatically by inserting into container
			// TODO: Remove ??
			FrameBufferHandle m_gBuffer_PositionMetallicHandle;
			FrameBufferHandle m_gBuffer_NormalRoughnessHandle;
			FrameBufferHandle m_gBuffer_DiffuseAOHandle;

			FrameBufferHandle m_BRDFTextureHandle;
			glm::vec2 m_BRDFTextureSize;

			// Everything is drawn to this texture before being drawn to the default 
			// frame buffer through some post-processing effects
			FrameBufferHandle m_OffscreenTextureHandle; 
			u32 m_OffscreenFBO = 0;
			u32 m_OffscreenRBO = 0;

			FrameBufferHandle m_LoadingTextureHandle;
			FrameBufferHandle m_WorkTextureHandle;

			// TODO: Use a mesh prefab here
			VertexBufferData m_SpriteQuadVertexBufferData;
			RenderID m_SpriteQuadRenderID;
			
			MaterialID m_SpriteMatID = InvalidMaterialID;
			MaterialID m_PostProcessMatID = InvalidMaterialID;

			u32 m_CaptureFBO = 0;
			u32 m_CaptureRBO = 0;

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

			GLPhysicsDebugDraw* m_PhysicsDebugDrawer = nullptr;




			FT_Library ft;
			BitmapFont* m_FntSourceCodePro = nullptr;

			GLRenderer(const GLRenderer&) = delete;
			GLRenderer& operator=(const GLRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL