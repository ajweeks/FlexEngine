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

			virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) override;
			virtual RenderID InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(RenderID renderID) override;

			virtual void PostInitialize() override;

			virtual void Update(const GameContext& gameContext) override;
			virtual void Draw(const GameContext& gameContext) override;
			virtual void ReloadShaders(GameContext& gameContext) override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColor(float r, float g, float b) override;

			virtual void OnWindowSize(int width, int height) override;

			virtual void SetVSyncEnabled(bool enableVSync) override;
			virtual void Clear(int flags, const GameContext& gameContext) override;
			virtual void SwapBuffers(const GameContext& gameContext) override;

			virtual void UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model) override;

			virtual int GetShaderUniformLocation(glm::uint program, const std::string& uniformName) override;
			virtual void SetUniform1f(int location, float val) override;

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
			virtual void ImGui_Shutdown() override;

			// Use if you want to reset your rendering device without losing ImGui state.
			void ImGui_InvalidateDeviceObjects();
			bool ImGui_CreateDeviceObjects();

		private:
			bool ImGui_CreateFontsTexture();

			GLuint m_ImGuiFontTexture = 0;
			int m_ImGuiShaderHandle = 0;
			//int m_ImGuiShaderHandle = 0, m_ImGuiVertHandle = 0, m_ImGuiFragHandle = 0;
			int m_ImGuiAttribLocationTex = 0, m_ImGuiAttribLocationProjMtx = 0;
			int m_ImGuiAttribLocationPosition = 0, m_ImGuiAttribLocationUV = 0, m_ImGuiAttribLocationColor = 0;
			unsigned int m_ImGuiVboHandle = 0, m_ImGuiVaoHandle = 0, g_ElementsHandle = 0;

			std::vector<Shader> m_LoadedShaders;
			std::vector<Material> m_LoadedMaterials;
			std::vector<RenderObject*> m_RenderObjects;

			RenderObject* GetRenderObject(RenderID renderID);
			RenderID GetFirstAvailableRenderID() const;
			void InsertNewRenderObject(RenderObject* renderObject);
			void UnloadShaders();
			void LoadShaders();

			void UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext);

			bool m_VSyncEnabled;

			// TODO: Clean up
			glm::uint viewProjectionUBO;
			glm::uint viewProjectionCombinedUBO;

			GLRenderer(const GLRenderer&) = delete;
			GLRenderer& operator=(const GLRenderer&) = delete;
		};
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL