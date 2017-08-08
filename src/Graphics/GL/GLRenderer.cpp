#include "stdafx.h"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLRenderer.h"
#include "Graphics/GL/GLHelpers.h"
#include "GameContext.h"
#include "Window/Window.h"
#include "Logger.h"
#include "FreeCamera.h"
#include "VertexBufferData.h"
#include "ShaderUtils.h"

#include <algorithm>

using namespace glm;

GLRenderer::GLRenderer(GameContext& gameContext)
{
	gameContext.program = ShaderUtils::LoadShaders(
		"resources/shaders/GLSL/simple.vert", 
		"resources/shaders/GLSL/simple.frag");
	m_Program = gameContext.program;

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	
	glUseProgram(gameContext.program);
}

GLRenderer::~GLRenderer()
{
	auto iter = m_RenderObjects.begin();
	while (iter != m_RenderObjects.end())
	{
		iter = Destroy(iter);
	}
	m_RenderObjects.clear();

	glDeleteProgram(m_Program);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	glfwTerminate();
}

uint GLRenderer::Initialize(const GameContext& gameContext, VertexBufferData* vertexBufferData)
{
	const uint renderID = m_RenderObjects.size();

	RenderObject* renderObject = new RenderObject();
	renderObject->renderID = renderID;

	glGenVertexArrays(1, &renderObject->VAO);
	glBindVertexArray(renderObject->VAO);

	glGenBuffers(1, &renderObject->VBO);
	glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
	glBufferData(GL_ARRAY_BUFFER, vertexBufferData->BufferSize, vertexBufferData->pDataStart, GL_STATIC_DRAW);

	renderObject->vertexBufferData = vertexBufferData;

	uint posAttrib = glGetAttribLocation(gameContext.program, "in_Position");
	glEnableVertexAttribArray(posAttrib);
	glVertexAttribPointer(posAttrib, 3, GL_FLOAT, false, renderObject->vertexBufferData->VertexStride, 0);

	renderObject->model = glGetUniformLocation(gameContext.program, "in_Model");
	renderObject->view = glGetUniformLocation(gameContext.program, "in_View");
	renderObject->projection = glGetUniformLocation(gameContext.program, "in_Projection");
	renderObject->modelInvTranspose = glGetUniformLocation(gameContext.program, "in_ModelInvTranspose");
	
	m_RenderObjects.push_back(renderObject);

	glBindVertexArray(0);

	return renderID;
}

uint GLRenderer::Initialize(const GameContext& gameContext,  VertexBufferData* vertexBufferData, std::vector<uint>* indices)
{
	const uint renderID = Initialize(gameContext, vertexBufferData);
	
	RenderObject* renderObject = GetRenderObject(renderID);

	renderObject->indices = indices;
	renderObject->indexed = true;

	glGenBuffers(1, &renderObject->IBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderObject->IBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices->at(0)) * indices->size(), indices->data(), GL_STATIC_DRAW);

	return renderID;
}

void GLRenderer::SetTopologyMode(glm::uint renderID, TopologyMode topology)
{
	RenderObject* renderObject = GetRenderObject(renderID);
	GLenum glMode = TopologyModeToGLMode(topology);
	
	if (glMode == GL_INVALID_ENUM)
	{
		Logger::LogError("Unhandled TopologyMode passed to GLRenderer::SetTopologyMode: " + std::to_string((int)topology));
		renderObject->topology = GL_TRIANGLES;
	}
	else
	{
		renderObject->topology = glMode;
	}
}

void GLRenderer::SetClearColor(float r, float g, float b)
{
	glClearColor(r, g, b, 1.0f);
}

void GLRenderer::PostInitialize()
{
	lightDir = glGetUniformLocation(m_Program, "lightDir");
	ambientColor = glGetUniformLocation(m_Program, "ambientColor");
	specularColor = glGetUniformLocation(m_Program, "specularColor");
	camPos = glGetUniformLocation(m_Program, "camPos");

	glGenTextures(1, &textureMap);
	glBindTexture(GL_TEXTURE_2D, textureMap);

	GLFWimage image = LoadGLFWimage("resources/textures/work.jpg");
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, image.pixels);
	glGenerateMipmap(GL_TEXTURE_2D);
	DestroyGLFWimage(image);
}

void GLRenderer::Update(const GameContext& gameContext)
{
	glUniform3f(lightDir, 
		m_SceneInfo.m_LightDir.x,
		m_SceneInfo.m_LightDir.y,
		m_SceneInfo.m_LightDir.z);

	glUniform4f(ambientColor,
		m_SceneInfo.m_AmbientColor.r,
		m_SceneInfo.m_AmbientColor.g,
		m_SceneInfo.m_AmbientColor.b,
		m_SceneInfo.m_AmbientColor.a);

	glUniform4f(specularColor,
		m_SceneInfo.m_SpecularColor.r,
		m_SceneInfo.m_SpecularColor.g,
		m_SceneInfo.m_SpecularColor.b,
		m_SceneInfo.m_SpecularColor.a);

	glm::vec3 cameraPos = gameContext.camera->GetPosition();
	glUniform3f(camPos, 
		cameraPos.x, 
		cameraPos.y, 
		cameraPos.z);
}

void GLRenderer::Draw(const GameContext& gameContext, uint renderID)
{
	UNREFERENCED_PARAMETER(gameContext);

	RenderObject* renderObject = GetRenderObject(renderID);

	// TODO: Bind object's own texture
	glBindTexture(GL_TEXTURE_2D, textureMap);

	glBindVertexArray(renderObject->VAO);
	glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);

	if (renderObject->indexed)
	{
		glDrawElements(renderObject->topology, renderObject->indices->size(), GL_UNSIGNED_INT, (void*)renderObject->indices->data());
	}
	else
	{
		glDrawArrays(renderObject->topology, 0, renderObject->vertexBufferData->BufferSize);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

size_t GLRenderer::ReloadShaders(const GameContext& gameContext)
{
	glDeleteProgram(gameContext.program);
	m_Program = ShaderUtils::LoadShaders(
		"resources/shaders/GLSL/simple.vert",
		"resources/shaders/GLSL/simple.frag");
	return m_Program;
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

void GLRenderer::UpdateTransformMatrix(const GameContext& gameContext, uint renderID, const glm::mat4& model)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	glUniformMatrix4fv(renderObject->model, 1, false, &model[0][0]);

	glm::mat4 modelInv = glm::inverse(model);
	glm::mat3 modelInv3 = glm::mat3(modelInv);
	// OpenGL will transpose for us if we set the third param to true
	glUniformMatrix3fv(renderObject->modelInvTranspose, 1, true, &modelInv3[0][0]);

	glm::mat4 view = gameContext.camera->GetView();
	glUniformMatrix4fv(renderObject->view, 1, false, &view[0][0]);
	
	glm::mat4 projection = gameContext.camera->GetProjection();
	glUniformMatrix4fv(renderObject->projection, 1, false, &projection[0][0]);
}

int GLRenderer::GetShaderUniformLocation(uint program, const std::string uniformName)
{
	return glGetUniformLocation(program, uniformName.c_str());
}

void GLRenderer::SetUniform1f(uint location, float val)
{
	glUniform1f(location, val);
}

void GLRenderer::DescribeShaderVariable(uint renderID, uint program, const std::string& variableName, int size, 
	Renderer::Type renderType, bool normalized, int stride, void* pointer)
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
			Destroy(iter);
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

GLenum GLRenderer::TopologyModeToGLMode(TopologyMode topology)
{
	GLenum glMode = 0;

	switch (topology)
	{
	case TopologyMode::POINT_LIST: return GL_POINTS;
	case TopologyMode::LINE_LIST: return GL_LINES;
	case TopologyMode::LINE_LOOP: return GL_LINE_LOOP;
	case TopologyMode::LINE_STRIP: return GL_LINE_STRIP;
	case TopologyMode::TRIANGLE_LIST: return GL_TRIANGLES;
	case TopologyMode::TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
	case TopologyMode::TRIANGLE_FAN: return GL_TRIANGLE_FAN;
	default: return GL_INVALID_ENUM;
	}
}

GLRenderer::RenderObject* GLRenderer::GetRenderObject(int renderID)
{
	return m_RenderObjects[renderID];
}

GLRenderer::RenderObjectIter GLRenderer::Destroy(RenderObjectIter iter)
{
	RenderObject* pRenderObject = *iter;
	auto newIter = m_RenderObjects.erase(iter);

	glDeleteBuffers(1, &pRenderObject->VBO);
	if (pRenderObject->indexed)
	{
		glDeleteBuffers(1, &pRenderObject->IBO);
	}

	SafeDelete(pRenderObject);

	return newIter;
}

#endif // COMPILE_OPEN_GL