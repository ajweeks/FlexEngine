
#include "Graphics/GLRenderer.h"
#include "GameContext.h"
#include "Window/Window.h"
#include "Logger.h"

#include <glad\glad.h>
#include <GLFW\glfw3.h>

using namespace glm;

GLRenderer::GLRenderer(GameContext& gameContext) :
	Renderer(gameContext),
	m_Program(gameContext.program)
{
	glClearColor(0.05f, 0.1f, 0.25f, 1.0f);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	
	glUseProgram(gameContext.program);

	//LoadAndBindGLTexture("resources/images/test2.jpg", m_TextureID);
	//glUniform1i(glGetUniformLocation(m_ProgramID, "texTest"), 0);
}

GLRenderer::~GLRenderer()
{
	glDeleteProgram(m_Program);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	glfwTerminate();
}

void GLRenderer::SetVSyncEnabled(bool enableVSync)
{
	m_VSyncEnabled = enableVSync;
	glfwSwapInterval(enableVSync ? 1 : 0);
}

void GLRenderer::Clear(int flags)
{
	GLbitfield mask = 0;
	if ((int)flags & (int)ClearFlag::COLOR) mask |= GL_COLOR_BUFFER_BIT;
	if ((int)flags & (int)ClearFlag::DEPTH) mask |= GL_DEPTH_BUFFER_BIT;
	glClear(mask);
}

void GLRenderer::SwapBuffers(const GameContext& gameContext)
{
	glfwSwapBuffers(gameContext.window->IsGLFWWindow());
}

void GLRenderer::BindVertexArray(uint vertexArrayObject)
{
	glBindVertexArray(vertexArrayObject);
}

void GLRenderer::UseProgram(uint program)
{
	glUseProgram(program);
}

void GLRenderer::BindBuffer(BufferTarget bufferTarget, uint buffer)
{
	const GLuint glTarget = BufferTargetToGLTarget(bufferTarget);
	glBindBuffer(glTarget, buffer);
}

void GLRenderer::SetUniform1f(uint uniform, float value)
{
	glUniform1f(uniform, value);
}

void GLRenderer::SetUniformMatrix4fv(uint uniform, uint count, bool transpose, float* values)
{
	glUniformMatrix4fv(uniform, count, transpose, values);
}

void GLRenderer::DrawArrays(Mode mode, uint first, uint count)
{
	const GLenum glMode = ModeToGLMode(mode);

	glDrawArrays(glMode, first, count);
}

void GLRenderer::DrawElements(Mode mode, uint count, Type type, const void* indices)
{
	const GLenum glMode = ModeToGLMode(mode);
	const GLenum glType = TypeToGLType(type);

	glDrawElements(glMode, count, glType, indices);
}

void GLRenderer::EnableVertexAttribArray(uint index)
{
	glEnableVertexAttribArray(index);
}

void GLRenderer::VertexAttribPointer(uint index, int size, Type type, bool normalized, size_t stride, const void* pointer)
{
	const GLenum glType = TypeToGLType(type);

	glVertexAttribPointer(index, size, glType, normalized, stride, pointer);
}

void GLRenderer::GenVertexArrays(glm::uint count, glm::uint* arrays)
{
	glGenVertexArrays(count, arrays);
}

void GLRenderer::GenBuffers(uint count, uint* buffers)
{
	glGenBuffers(count, (GLuint*)buffers);
}

void GLRenderer::BufferData(BufferTarget bufferTarget, uint size, const void* data, UsageFlag usage)
{
	const GLuint glTarget = BufferTargetToGLTarget(bufferTarget);
	const GLenum glUsage = UsageFlagToGLUsageFlag(usage);

	glBufferData(glTarget, size, data, glUsage);
}

void GLRenderer::DeleteVertexArrays(glm::uint count, glm::uint* arrays)
{
	glDeleteVertexArrays(count, arrays);
}

int GLRenderer::GetAttribLocation(uint program, const char* name)
{
	return glGetAttribLocation(program, name);
}

int GLRenderer::GetUniformLocation(uint program, const char* name)
{
	return glGetUniformLocation(program, name);
}

GLuint GLRenderer::BufferTargetToGLTarget(BufferTarget bufferTarget)
{
	GLuint glTarget = 0;
	if (bufferTarget == BufferTarget::ARRAY_BUFFER) glTarget = GL_ARRAY_BUFFER;
	else if (bufferTarget == BufferTarget::ELEMENT_ARRAY_BUFFER) glTarget = GL_ELEMENT_ARRAY_BUFFER;
	else Logger::LogError("Unhandled BufferTarget passed to GLRenderer: " + std::to_string((int)bufferTarget));

	return glTarget;
}

GLenum GLRenderer::TypeToGLType(Type type)
{
	GLenum glType = 0;
	if (type == Type::BYTE) glType = GL_BYTE;
	else if (type == Type::UNSIGNED_BYTE) glType = GL_UNSIGNED_BYTE;
	else if (type == Type::SHORT) glType = GL_SHORT;
	else if (type == Type::UNSIGNED_SHORT) glType = GL_UNSIGNED_SHORT;
	else if (type == Type::INT) glType = GL_INT;
	else if (type == Type::UNSIGNED_INT) glType = GL_UNSIGNED_INT;
	else if (type == Type::FLOAT) glType = GL_FLOAT;
	else if (type == Type::DOUBLE) glType = GL_DOUBLE;
	else Logger::LogError("Unhandled Type passed to GLRenderer: " + std::to_string((int)type));

	return glType;
}

GLenum GLRenderer::UsageFlagToGLUsageFlag(UsageFlag usage)
{
	GLenum glUsage = 0;
	if (usage == UsageFlag::STATIC_DRAW) glUsage = GL_STATIC_DRAW;
	else if (usage == UsageFlag::DYNAMIC_DRAW) glUsage = GL_DYNAMIC_DRAW;
	else Logger::LogError("Unhandled usage flag passed to GLRenderer: " + std::to_string((int)usage));

	return glUsage;
}

GLenum GLRenderer::ModeToGLMode(Mode mode)
{
	GLenum glMode = 0;
	if (mode == Mode::POINTS) glMode = GL_POINTS;
	else if (mode == Mode::LINES) glMode = GL_LINES;
	else if (mode == Mode::LINE_LOOP) glMode = GL_LINE_LOOP;
	else if (mode == Mode::LINE_STRIP) glMode = GL_LINE_STRIP;
	else if (mode == Mode::TRIANGLES) glMode = GL_TRIANGLES;
	else if (mode == Mode::TRIANGLE_STRIP) glMode = GL_TRIANGLE_STRIP;
	else if (mode == Mode::TRIANGLE_FAN) glMode = GL_TRIANGLE_FAN;
	else if (mode == Mode::QUADS) glMode = GL_QUADS;
	else Logger::LogError("Unhandled Mode passed to GLRenderer: " + std::to_string((int)mode));

	return glMode;
}
