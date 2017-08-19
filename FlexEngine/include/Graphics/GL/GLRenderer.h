#pragma once
#if COMPILE_OPEN_GL

#include "Graphics/Renderer.h"

namespace flex
{
	class GLRenderer : public Renderer
	{
	public:
		GLRenderer(GameContext& gameContext);
		virtual ~GLRenderer();

		virtual RenderID InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) override;

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

		virtual int GetShaderUniformLocation(RenderID program, const std::string uniformName) override;
		virtual void SetUniform1f(int location, float val) override;

		virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size,
			Renderer::Type renderType, bool normalized, int stride, void* pointer) override;

		virtual void Destroy(RenderID renderID) override;

	private:
		static glm::uint BufferTargetToGLTarget(BufferTarget bufferTarget);
		static glm::uint TypeToGLType(Type type);
		static glm::uint UsageFlagToGLUsageFlag(UsageFlag usage);
		static glm::uint TopologyModeToGLMode(TopologyMode topology);
		static glm::uint CullFaceToGLMode(CullFace cullFace);

		struct RenderObject
		{
			RenderID renderID;

			glm::uint VAO;
			glm::uint VBO;
			glm::uint IBO;

			GLenum topology = GL_TRIANGLES;

			glm::uint vertexBuffer;
			VertexBufferData* vertexBufferData = nullptr;

			bool indexed;
			glm::uint indexBuffer;
			std::vector<glm::uint>* indices = nullptr;

			glm::mat4 model;

			// Uniform IDs
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
			};
			UniformIDs uniformIDs;

			glm::uint shaderIndex;

			bool useDiffuseTexture;
			std::string diffuseTexturePath;
			glm::uint diffuseTextureID;

			bool useSpecularTexture;
			std::string specularTexturePath;
			glm::uint specularTextureID;

			bool useNormalTexture;
			std::string normalTexturePath;
			glm::uint normalTextureID;

			GLenum cullFace = GL_BACK;
		};

		typedef std::vector<RenderObject*>::iterator RenderObjectIter;

		RenderObject* GetRenderObject(RenderID renderID);
		RenderObjectIter Destroy(RenderObjectIter iter);
		void UnloadShaders();
		void LoadShaders();

		void UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext);

		std::vector<RenderObject*> m_RenderObjects;

		bool m_VSyncEnabled;

		struct Shader
		{
			glm::uint program;
			glm::uint vertexShader;
			glm::uint fragmentShader;

			Uniform::Type constantBufferUniforms;
			Uniform::Type dynamicBufferUniforms;
		};

		std::vector<Shader> m_LoadedShaders;

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

		Skybox m_Skybox;

		// TODO: Clean up
		glm::uint viewProjectionUBO;
		glm::uint viewProjectionCombinedUBO;

		GLRenderer(const GLRenderer&) = delete;
		GLRenderer& operator=(const GLRenderer&) = delete;
	};
} // namespace flex

#endif // COMPILE_OPEN_GL