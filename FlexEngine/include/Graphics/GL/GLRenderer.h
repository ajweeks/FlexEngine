#pragma once
#if COMPILE_OPEN_GL

#include "Graphics/Renderer.h"

#include <imgui.h>

#include "GLHelpers.h"

namespace flex
{
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
			virtual void PostInitializeRenderObject(RenderID renderID) override;
			virtual DirectionalLightID InitializeDirectionalLight(const DirectionalLight& dirLight) override;
			virtual PointLightID InitializePointLight(const PointLight& pointLight) override;

			virtual DirectionalLight& GetDirectionalLight(DirectionalLightID dirLightID) override;
			virtual PointLight& GetPointLight(PointLightID pointLightID) override;
			virtual std::vector<PointLight>& GetAllPointLights() override;

			virtual void Update(const GameContext& gameContext) override;
			virtual void Draw(const GameContext& gameContext) override;
			virtual void ReloadShaders(GameContext& gameContext) override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColor(float r, float g, float b) override;

			virtual void OnWindowSize(int width, int height) override;

			virtual void SetVSyncEnabled(bool enableVSync) override;

			virtual void UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model) override;

			virtual void SetFloat(ShaderID shaderID, const std::string& valName, float val) override;
			virtual void SetVec2f(ShaderID shaderID, const std::string& vecName, const glm::vec2& vec) override;
			virtual void SetVec3f(ShaderID shaderID, const std::string& vecName, const glm::vec3& vec) override;
			virtual void SetVec4f(ShaderID shaderID, const std::string& vecName, const glm::vec4& vec) override;
			virtual void SetMat4f(ShaderID shaderID, const std::string& matName, const glm::mat4& mat) override;

			virtual glm::uint GetRenderObjectCount() const override;
			virtual glm::uint GetRenderObjectCapacity() const override;

			virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size,
				Renderer::Type renderType, bool normalized, int stride, void* pointer) override;

			virtual void Destroy(RenderID renderID) override;

			virtual void GetRenderObjectInfos(std::vector<RenderObjectInfo>& vec) override;

			// ImGui functions
			virtual void ImGui_Init(const GameContext& gameContext) override;
			virtual void ImGui_NewFrame(const GameContext& gameContext) override;
			virtual void ImGui_Render() override;
			virtual void ImGui_ReleaseRenderObjects() override;

		private:
			void ImGui_InvalidateDeviceObjects();
			bool ImGui_CreateDeviceObjects();
			bool ImGui_CreateFontsTexture();

			GLuint m_ImGuiFontTexture = 0;
			int m_ImGuiShaderHandle = 0;
			int m_ImGuiAttribLocationTex = 0, m_ImGuiAttribLocationProjMtx = 0;
			int m_ImGuiAttribLocationPosition = 0, m_ImGuiAttribLocationUV = 0, m_ImGuiAttribLocationColor = 0;
			unsigned int m_ImGuiVboHandle = 0, m_ImGuiVaoHandle = 0, g_ElementsHandle = 0;

			std::vector<Shader> m_Shaders;
			std::vector<Material> m_Materials;
			std::vector<RenderObject*> m_RenderObjects;
			DirectionalLight m_DirectionalLight;
			std::vector<PointLight> m_PointLights;

			RenderObject* GetRenderObject(RenderID renderID);
			RenderID GetFirstAvailableRenderID() const;
			void InsertNewRenderObject(RenderObject* renderObject);
			void UnloadShaders();
			void LoadShaders();
			
			void GenerateFrameBufferTexture(glm::uint* handle, int index, GLint internalFormat, GLenum format, const glm::vec2i& size);

			void UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext);

			bool m_VSyncEnabled;

			// TODO: Clean up (make more dynamic)
			glm::uint viewProjectionUBO;
			glm::uint viewProjectionCombinedUBO;

			RenderID m_GBufferQuadRenderID;
			VertexBufferData m_gBufferQuadVertexBufferData;
			Transform m_gBufferQuadTransform;
			glm::uint m_gBufferHandle;
			glm::uint m_gBuffer_PositionHandle;
			glm::uint m_gBuffer_NormalHandle;
			glm::uint m_gBuffer_DiffuseSpecularHandle;

			GLRenderer(const GLRenderer&) = delete;
			GLRenderer& operator=(const GLRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL