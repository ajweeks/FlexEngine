#pragma once
#if COMPILE_OPEN_GL

#include "Graphics/Renderer.hpp"

#include <imgui.h>

#include <map>

#include "Graphics/GL/GLHelpers.hpp"

namespace flex
{
	class MeshPrefab;

	namespace gl
	{
		class GLRenderer : public Renderer
		{
		public:
			GLRenderer(GameContext& gameContext);
			virtual ~GLRenderer();

			virtual void PostInitialize(const GameContext& gameContext) override;

			virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) override;
			virtual RenderID InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID) override;
			virtual DirectionalLightID InitializeDirectionalLight(const DirectionalLight& dirLight) override;
			virtual PointLightID InitializePointLight(const PointLight& pointLight) override;

			virtual DirectionalLight& GetDirectionalLight(DirectionalLightID dirLightID) override;
			virtual PointLight& GetPointLight(PointLightID pointLightID) override;

			virtual void Update(const GameContext& gameContext) override;
			virtual void Draw(const GameContext& gameContext) override;
			virtual void DrawImGuiItems(const GameContext& gameContext) override;

			virtual void ReloadShaders(GameContext& gameContext) override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColor(float r, float g, float b) override;

			virtual void OnWindowSize(int width, int height) override;
			
			virtual void SetRenderObjectVisible(RenderID renderID, bool visible) override;

			virtual void SetVSyncEnabled(bool enableVSync) override;

			virtual glm::uint GetRenderObjectCount() const override;
			virtual glm::uint GetRenderObjectCapacity() const override;

			virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size,
				Renderer::Type renderType, bool normalized, int stride, void* pointer) override;

			virtual void Destroy(RenderID renderID) override;

			// ImGui functions
			virtual void ImGui_Init(const GameContext& gameContext) override;
			virtual void ImGui_NewFrame(const GameContext& gameContext) override;
			virtual void ImGui_Render() override;
			virtual void ImGui_ReleaseRenderObjects() override;

		private:
			// TODO: Either use these functions or remove them
			void SetFloat(ShaderID shaderID, const std::string& valName, float val);
			void SetUInt(ShaderID shaderID, const std::string& valName, glm::uint val);
			void SetVec2f(ShaderID shaderID, const std::string& vecName, const glm::vec2& vec);
			void SetVec3f(ShaderID shaderID, const std::string& vecName, const glm::vec3& vec);
			void SetVec4f(ShaderID shaderID, const std::string& vecName, const glm::vec4& vec);
			void SetMat4f(ShaderID shaderID, const std::string& matName, const glm::mat4& mat);

			void GenerateSkybox(const GameContext& gameContext);
			void GenerateGBuffer(const GameContext& gameContext);

			void GenerateCubemapFromHDREquirectangular(const GameContext& gameContext, GLRenderObject* renderObject);
			void GeneratePrefilteredMapFromCubemap(const GameContext& gameContext, GLRenderObject* renderObject);
			void GenerateIrradianceSamplerFromCubemap(const GameContext& gameContext, GLRenderObject* renderObject);
			void GenerateBRDFLUT(const GameContext& gameContext, glm::uint brdfLUTTextureID, glm::uvec2 BRDFLUTSize);
			// Draw all static geometry to the given render object's cubemap texture
			void CaptureSceneToCubemap(const GameContext& gameContext, RenderID cubemapRenderID);

			void SwapBuffers(const GameContext& gameContext);

			struct DrawCallInfo
			{
				bool renderToCubemap = false;
				RenderID cubemapObjectRenderID;
				bool deferred;
			};

			void DrawRenderObjectBatch(const GameContext& gameContext, const std::vector<GLRenderObject*>& batchedRenderObjects, const DrawCallInfo& drawCallInfo);
			void DrawSpriteQuad(const GameContext& gameContext, glm::uint textureHandle, MaterialID materialID, bool flipVertically = false);

			bool GetLoadedTexture(const std::string& filePath, glm::uint& handle);

			MaterialID GetNextAvailableMaterialID();
			bool GetShaderID(const std::string& shaderName, ShaderID& shaderID);

			GLRenderObject* GetRenderObject(RenderID renderID);
			RenderID GetNextAvailableRenderID() const;
			void InsertNewRenderObject(GLRenderObject* renderObject);
			void UnloadShaders();
			void LoadShaders();

			void GenerateFrameBufferTexture(glm::uint* handle, int index, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void ResizeFrameBufferTexture(glm::uint handle, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void ResizeRenderBuffer(glm::uint handle, const glm::vec2i& size);

			void UpdateMaterialUniforms(const GameContext& gameContext, MaterialID materialID);
			void UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext);
			void UpdatePerObjectUniforms(MaterialID materialID, const glm::mat4& model, const GameContext& gameContext);

			void BatchRenderObjects(const GameContext& gameContext);
			void DrawDeferredObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void DrawGBufferQuad(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void DrawForwardObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void DrawOffscreenTexture(const GameContext& gameContext);
			void DrawUI();

			// Returns the next binding that would be used
			glm::uint BindTextures(Shader* shader, GLMaterial* glMaterial, glm::uint startingBinding = 0);
			// Returns the next binding that would be used
			glm::uint BindFrameBufferTextures(GLMaterial* glMaterial, glm::uint startingBinding = 0);
			// Returns the next binding that would be used
			glm::uint BindDeferredFrameBufferTextures(GLMaterial* glMaterial, glm::uint startingBinding = 0);

			void ImGui_InvalidateDeviceObjects();
			bool ImGui_CreateDeviceObjects();
			bool ImGui_CreateFontsTexture();

			GLuint m_ImGuiFontTexture = 0;
			int m_ImGuiShaderHandle = 0;
			int m_ImGuiAttribLocationTex = 0, m_ImGuiAttribLocationProjMtx = 0;
			int m_ImGuiAttribLocationPosition = 0, m_ImGuiAttribLocationUV = 0, m_ImGuiAttribLocationColor = 0;
			unsigned int m_ImGuiVboHandle = 0, m_ImGuiVaoHandle = 0, g_ElementsHandle = 0;
			
			std::map<MaterialID, GLMaterial> m_Materials;
			std::map<RenderID, GLRenderObject*> m_RenderObjects;

			bool m_VSyncEnabled;

			// TODO: Convert to map?
			std::vector<GLShader> m_Shaders;
			std::map<std::string, glm::uint> m_LoadedTextures; // Key is filepath, value is texture id

			// TODO: Clean up (make more dynamic)
			glm::uint viewProjectionUBO;
			glm::uint viewProjectionCombinedUBO;

			RenderID m_GBufferQuadRenderID;
			VertexBufferData m_gBufferQuadVertexBufferData;
			Transform m_gBufferQuadTransform;
			glm::uint m_gBufferHandle;
			glm::uint m_gBufferDepthHandle;

			struct FrameBufferHandle
			{
				glm::uint id;
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
			glm::uvec2 m_BRDFTextureSize;

			// Everything is drawn to this texture before being drawn to the default 
			// frame buffer through some post-processing effects
			FrameBufferHandle m_OffscreenTextureHandle; 
			glm::uint m_OffscreenFBO;
			glm::uint m_OffscreenRBO;

			FrameBufferHandle m_LoadingTextureHandle;
			// TODO: Use a mesh prefab here
			VertexBufferData m_SpriteQuadVertexBufferData;
			Transform m_SpriteQuadTransform;
			RenderID m_SpriteQuadRenderID;
			
			MaterialID m_SpriteMatID;
			MaterialID m_PostProcessMatID;

			glm::uint m_CaptureFBO;
			glm::uint m_CaptureRBO;

			glm::mat4 m_CaptureProjection;
			std::array<glm::mat4, 6> m_CaptureViews;

			MeshPrefab* m_SkyBoxMesh = nullptr;

			VertexBufferData m_1x1_NDC_QuadVertexBufferData;
			Transform m_1x1_NDC_QuadTransform;
			GLRenderObject* m_1x1_NDC_Quad = nullptr; // A 1x1 quad in NDC space

			std::vector<std::vector<GLRenderObject*>> m_DeferredRenderObjectBatches;
			std::vector<std::vector<GLRenderObject*>> m_ForwardRenderObjectBatches;

			GLRenderer(const GLRenderer&) = delete;
			GLRenderer& operator=(const GLRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL