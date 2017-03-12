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

	virtual void SetVSyncEnabled(bool enableVSync) override;
	virtual void Clear(int flags) override;
	virtual void SwapBuffers(const GameContext& gameContext) override;

	virtual void BindVertexArray(glm::uint vertexArrayObject) override;
	virtual void UseProgram(glm::uint program) override;
	virtual void BindBuffer(BufferTarget bufferTarget, glm::uint buffer) override;
	virtual void SetUniform1f(glm::uint uniform, float value) override;
	virtual void SetUniformMatrix4fv(glm::uint uniform, glm::uint count, bool transpose, float* values) override;
	virtual void EnableVertexAttribArray(glm::uint index) override;
	virtual void VertexAttribPointer(glm::uint index, int size, Type type, bool normalized, size_t stride, const void* pointer) override;
	virtual void GenVertexArrays(glm::uint count, glm::uint* arrays) override;
	virtual void GenBuffers(glm::uint count, glm::uint* buffers) override;
	virtual void BufferData(BufferTarget bufferTarget, glm::uint size, const void* data, UsageFlag usage) override;

	virtual void DrawArrays(Mode mode, glm::uint first, glm::uint count) override;
	virtual void DrawElements(Mode mode, glm::uint count, Type type, const void* indices) override;

	virtual void DeleteVertexArrays(glm::uint count, glm::uint* arrays) override;

	virtual int GetAttribLocation(glm::uint program, const char* name) override;
	virtual int GetUniformLocation(glm::uint program, const char* name) override;

private:
	static GLuint BufferTargetToGLTarget(BufferTarget bufferTarget);
	static GLenum TypeToGLType(Type type);
	static GLenum UsageFlagToGLUsageFlag(UsageFlag usage);
	static GLenum ModeToGLMode(Mode mode);

	bool m_VSyncEnabled;
	GLuint m_Program;

	GLRenderer(const GLRenderer&) = delete;
	GLRenderer& operator=(const GLRenderer&) = delete;

};