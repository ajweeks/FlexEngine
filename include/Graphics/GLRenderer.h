#pragma once

#include "Renderer.h"

#include <glad\glad.h>
#include <GLFW\glfw3.h>

struct GameContext;

class GLRenderer : public Renderer
{
public:
	GLRenderer(GameContext& gameContext);
	virtual ~GLRenderer();

	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices) override;
	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices,
		std::vector<glm::uint>* indices) override;

	virtual void Draw(glm::uint renderID) override;

	virtual void SetVSyncEnabled(bool enableVSync) override;
	virtual void Clear(int flags) override;
	virtual void SwapBuffers(const GameContext& gameContext) override;

	virtual void UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4x4& model) override;

	virtual int GetShaderUniformLocation(glm::uint program, const std::string uniformName) override;
	virtual void SetUniform1f(glm::uint location, float val) override;

	virtual void DescribeShaderVariable(glm::uint renderID, glm::uint program, const std::string& variableName, int size, 
		Renderer::Type renderType, bool normalized, int stride, void* pointer) override;

	virtual void Destroy(glm::uint renderID) override;

private:
	static GLuint BufferTargetToGLTarget(BufferTarget bufferTarget);
	static GLenum TypeToGLType(Type type);
	static GLenum UsageFlagToGLUsageFlag(UsageFlag usage);
	static GLenum ModeToGLMode(Mode mode);

	struct RenderObject
	{
		GLuint renderID;

		GLuint VAO;
		GLuint VBO;
		GLuint IBO;

		GLuint vertexBuffer;
		std::vector<VertexPosCol>* vertices = nullptr;

		bool indexed;
		GLuint indexBuffer;
		std::vector<glm::uint>* indices = nullptr;

		GLuint MVP;
	};

	RenderObject* GetRenderObject(int renderID);

	// TODO: use sorted data type (map)
	std::vector<RenderObject*> m_RenderObjects;

	bool m_VSyncEnabled;
	GLuint m_Program;

	GLRenderer(const GLRenderer&) = delete;
	GLRenderer& operator=(const GLRenderer&) = delete;

};