#include "stdafx.h"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLRenderer.h"
#include "GameContext.h"
#include "Window/Window.h"
#include "Logger.h"
#include "FreeCamera.h"

#include <algorithm>

using namespace glm;

GLRenderer::GLRenderer(GameContext& gameContext) :
	m_Program(gameContext.program)
{
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	
	glUseProgram(gameContext.program);

	//LoadAndBindGLTexture("resources/images/test2.jpg", m_TextureID);
	//glUniform1i(glGetUniformLocation(m_ProgramID, "texTest"), 0);
}

GLRenderer::~GLRenderer()
{
	for (size_t i = 0; i < m_RenderObjects.size(); i++)
	{
		glDeleteBuffers(1, &m_RenderObjects[i]->VBO);
		if (m_RenderObjects[i]->indexed)
		{
			glDeleteBuffers(1, &m_RenderObjects[i]->IBO);
		}

		delete m_RenderObjects[i];
	}
	m_RenderObjects.clear();

	glDeleteProgram(m_Program);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	glfwTerminate();
}

uint GLRenderer::Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices)
{
	const uint renderID = m_RenderObjects.size();

	RenderObject* object = new RenderObject();
	object->renderID = renderID;

	glGenVertexArrays(1, &object->VAO);
	glBindVertexArray(object->VAO);

	glGenBuffers(1, &object->VBO);
	glBindBuffer(GL_ARRAY_BUFFER, object->VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices->at(0)) * vertices->size(), vertices->data(), GL_STATIC_DRAW);

	object->vertices = vertices;

	uint posAttrib = glGetAttribLocation(gameContext.program, "in_Position");
	glEnableVertexAttribArray(posAttrib);
	glVertexAttribPointer(posAttrib, 3, GL_FLOAT, false, VertexPosCol::stride, 0);

	object->MVP = glGetUniformLocation(gameContext.program, "in_MVP");
	
	m_RenderObjects.push_back(object);

	glBindVertexArray(0);

	return renderID;
}

uint GLRenderer::Initialize(const GameContext& gameContext,  std::vector<VertexPosCol>* vertices, std::vector<uint>* indices)
{
	const uint renderID = Initialize(gameContext, vertices);
	
	RenderObject* object = GetRenderObject(renderID);

	object->indices = indices;
	object->indexed = true;

	glGenBuffers(1, &object->IBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, object->IBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices->at(0)) * indices->size(), indices->data(), GL_STATIC_DRAW);

	return renderID;
}

void GLRenderer::SetClearColor(float r, float g, float b)
{
	glClearColor(r, g, b, 1.0f);
}

void GLRenderer::PostInitialize()
{
}

void GLRenderer::Draw(const GameContext& gameContext, uint renderID)
{
	UNREFERENCED_PARAMETER(gameContext);

	RenderObject* renderObject = GetRenderObject(renderID);

	glBindVertexArray(renderObject->VAO);

	glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);

	if (renderObject->indexed)
	{
		glDrawElements(GL_TRIANGLES, renderObject->indices->size(), GL_UNSIGNED_INT, (void*)renderObject->indices->data());
	}
	else
	{
		glDrawArrays(GL_TRIANGLES, 0, renderObject->vertices->size());
	}

	glBindVertexArray(0);
}

void GLRenderer::OnWindowSize(int width, int height)
{
	glViewport(0, 0, width, height);
}

void GLRenderer::SetVSyncEnabled(bool enableVSync)
{
	m_VSyncEnabled = enableVSync;
	glfwSwapInterval(enableVSync ? 1 : 0);
}

void GLRenderer::Clear(int flags, const GameContext& gameContext)
{
	GLbitfield mask = 0;
	if ((int)flags & (int)ClearFlag::COLOR) mask |= GL_COLOR_BUFFER_BIT;
	if ((int)flags & (int)ClearFlag::DEPTH) mask |= GL_DEPTH_BUFFER_BIT;
	glClear(mask);
}

void GLRenderer::SwapBuffers(const GameContext& gameContext)
{
	glfwSwapBuffers(((GLWindowWrapper*)gameContext.window)->GetWindow());
}

void GLRenderer::UpdateTransformMatrix(const GameContext& gameContext, uint renderID, const glm::mat4x4& model)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	glm::mat4 MVP = gameContext.camera->GetViewProjection() * model;
	glUniformMatrix4fv(renderObject->MVP, 1, false, &MVP[0][0]);
}

int GLRenderer::GetShaderUniformLocation(uint program, const std::string uniformName)
{
	return glGetUniformLocation(program, uniformName.c_str());
}

void GLRenderer::SetUniform1f(uint location, float val)
{
	glUniform1f(location, val);
}

void GLRenderer::DescribeShaderVariable(uint renderID, uint program, const std::string& variableName, int size, Renderer::Type renderType, bool normalized, int stride, void* pointer)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	glBindVertexArray(renderObject->VAO);

	GLuint location = glGetAttribLocation(program, variableName.c_str());
	glEnableVertexAttribArray(location);
	GLenum glRenderType = TypeToGLType(renderType);
	glVertexAttribPointer(location, size, glRenderType, normalized, stride, pointer);

	glBindVertexArray(0);
}

void GLRenderer::Destroy(uint renderID)
{
	for (auto iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
	{
		if ((*iter)->renderID == renderID)
		{
			RenderObject* object = *iter;
			m_RenderObjects.erase(iter);
			delete object;

			return;
		}
	}
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
	else Logger::LogError("Unhandled Mode passed to GLRenderer: " + std::to_string((int)mode));

	return glMode;
}

GLRenderer::RenderObject* GLRenderer::GetRenderObject(int renderID)
{
	return m_RenderObjects[renderID];
}

#endif // COMPILE_OPEN_GL