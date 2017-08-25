#pragma once
#if COMPILE_OPEN_GL

#include "Graphics/Renderer.h"

#include <imgui.h>

namespace flex
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

		// ImGUI functions
		virtual void ImGui_Init(Window* window) override;
		virtual void ImGui_NewFrame(const GameContext& gameContext) override;
		virtual void ImGui_Shutdown() override;

		// Use if you want to reset your rendering device without losing ImGui state.
		IMGUI_API void ImGui_InvalidateDeviceObjects();
		IMGUI_API bool ImGui_CreateDeviceObjects();

	private:
		static glm::uint BufferTargetToGLTarget(BufferTarget bufferTarget);
		static glm::uint TypeToGLType(Type type);
		static glm::uint UsageFlagToGLUsageFlag(UsageFlag usage);
		static glm::uint TopologyModeToGLMode(TopologyMode topology);
		static glm::uint CullFaceToGLMode(CullFace cullFace);

		// ImGUI private member functions
		bool ImGui_CreateFontsTexture();

		struct Shader
		{
			glm::uint program;
			glm::uint vertexShader;
			glm::uint fragmentShader;

			Uniform::Type constantBufferUniforms;
			Uniform::Type dynamicBufferUniforms;
		};

		std::vector<Shader> m_LoadedShaders;

		struct Material
		{
			std::string name;

			glm::uint shaderIndex;

			struct UniformIDs
			{
				int modelID;
				int modelInvTranspose;
				int modelViewProjection;
				int camPos;
				int viewDir;
				int lightDir;
				int ambientColor;
				int specularColor;
				int useDiffuseTexture;
				int useNormalTexture;
				int useSpecularTexture;
				int useCubemapTexture;
			};
			UniformIDs uniformIDs;

			bool useDiffuseTexture = false;
			std::string diffuseTexturePath;
			glm::uint diffuseTextureID;

			bool useSpecularTexture = false;
			std::string specularTexturePath;
			glm::uint specularTextureID;

			bool useNormalTexture = false;
			std::string normalTexturePath;
			glm::uint normalTextureID;

			std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT
			bool useCubemapTexture = false;
		};
		
		std::vector<Material> m_LoadedMaterials;

		struct RenderObject
		{
			RenderObject(RenderID renderID, std::string name = "");

			RenderID renderID;

			RenderObjectInfo info;

			glm::uint VAO;
			glm::uint VBO;
			glm::uint IBO;

			GLenum topology = GL_TRIANGLES;
			GLenum cullFace = GL_BACK;

			glm::uint vertexBuffer;
			VertexBufferData* vertexBufferData = nullptr;

			bool indexed = false;
			glm::uint indexBuffer;
			std::vector<glm::uint>* indices = nullptr;

			glm::mat4 model;

			glm::uint materialID;
		};

		typedef std::vector<RenderObject*>::iterator RenderObjectIter;

		RenderObject* GetRenderObject(RenderID renderID);
		RenderID GetFirstAvailableRenderID() const;
		void InsertNewRenderObject(RenderObject* renderObject);
		void UnloadShaders();
		void LoadShaders();

		void UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext);

		std::vector<RenderObject*> m_RenderObjects;

		bool m_VSyncEnabled;


		struct UniformInfo
		{
			Uniform::Type type;
			int* id;
			const GLchar* name;
		};

		struct ViewProjectionUBO
		{
			glm::mat4 view;
			glm::mat4 proj;
		};

		struct ViewProjectionCombinedUBO
		{
			glm::mat4 viewProj;
		};

		struct Skybox
		{
			glm::uint textureID;
		};

		// TODO: Clean up
		glm::uint viewProjectionUBO;
		glm::uint viewProjectionCombinedUBO;

		GLRenderer(const GLRenderer&) = delete;
		GLRenderer& operator=(const GLRenderer&) = delete;
	};


	void ImGui_RenderDrawLists(ImDrawData* draw_data);
	//static GLFWwindow*  g_Window = NULL;
	static GLuint       g_FontTexture = 0;
	static int          g_ShaderHandle = 0, g_VertHandle = 0, g_FragHandle = 0;
	static int          g_AttribLocationTex = 0, g_AttribLocationProjMtx = 0;
	static int          g_AttribLocationPosition = 0, g_AttribLocationUV = 0, g_AttribLocationColor = 0;
	static unsigned int g_VboHandle = 0, g_VaoHandle = 0, g_ElementsHandle = 0;


} // namespace flex

#endif // COMPILE_OPEN_GL