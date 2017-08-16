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
#include <utility>

using namespace glm;

GLRenderer::GLRenderer(GameContext& gameContext)
{
	CheckGLErrorMessages();

	LoadShaders();

	CheckGLErrorMessages();

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	CheckGLErrorMessages();

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	CheckGLErrorMessages();
}

GLRenderer::~GLRenderer()
{
	CheckGLErrorMessages();

	auto iter = m_RenderObjects.begin();
	while (iter != m_RenderObjects.end())
	{
		iter = Destroy(iter);
		CheckGLErrorMessages();
	}
	m_RenderObjects.clear();
	CheckGLErrorMessages();

	glfwTerminate();
}

glm::uint GLRenderer::Initialize(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo)
{
	const uint renderID = m_RenderObjects.size();

	RenderObject* renderObject = new RenderObject();
	m_RenderObjects.push_back(renderObject);
	renderObject->renderID = renderID;
	renderObject->shaderIndex = createInfo->shaderIndex;

	glGenVertexArrays(1, &renderObject->VAO);
	glBindVertexArray(renderObject->VAO);
	CheckGLErrorMessages();

	glGenBuffers(1, &renderObject->VBO);
	glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
	glBufferData(GL_ARRAY_BUFFER, createInfo->vertexBufferData->BufferSize, createInfo->vertexBufferData->pDataStart, GL_STATIC_DRAW);
	CheckGLErrorMessages();

	renderObject->vertexBufferData = createInfo->vertexBufferData;

	Shader* shader = &m_LoadedShaders[renderObject->shaderIndex];

	// TODO: Make this more flexible
	if (renderObject->shaderIndex == 0)
	{
		renderObject->model = glGetUniformLocation(shader->program, "in_Model");
		if (renderObject->model == -1) Logger::LogError("in_Model was not found!");

		renderObject->view = glGetUniformLocation(shader->program, "in_View");
		if (renderObject->view == -1) Logger::LogError("in_View was not found!");

		renderObject->projection = glGetUniformLocation(shader->program, "in_Projection");
		if (renderObject->projection == -1) Logger::LogError("in_Projection was not found!");

		renderObject->modelInvTranspose = glGetUniformLocation(shader->program, "in_ModelInvTranspose");
		if (renderObject->modelInvTranspose == -1) Logger::LogError("in_ModelInvTranspose was not found!");
	}
	else if (renderObject->shaderIndex == 1)
	{
		renderObject->MVP = glGetUniformLocation(shader->program, "in_MVP");
		if (renderObject->MVP == -1) Logger::LogError("in_MVP was not found!");
	}

	renderObject->diffuseMapPath = createInfo->diffuseMapPath;
	renderObject->specularMapPath = createInfo->specularMapPath;
	renderObject->normalMapPath = createInfo->normalMapPath;

	if (!createInfo->diffuseMapPath.empty()) GenerateGLTexture(renderObject->VAO, renderObject->diffuseMapID, createInfo->diffuseMapPath);
	if (!createInfo->specularMapPath.empty()) GenerateGLTexture(renderObject->VAO, renderObject->specularMapID, createInfo->specularMapPath);
	if (!createInfo->normalMapPath.empty()) GenerateGLTexture(renderObject->VAO, renderObject->normalMapID, createInfo->normalMapPath);

	if (createInfo->indices != nullptr)
	{
		renderObject->indices = createInfo->indices;
		renderObject->indexed = true;

		glGenBuffers(1, &renderObject->IBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderObject->IBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(createInfo->indices->at(0)) * createInfo->indices->size(), createInfo->indices->data(), GL_STATIC_DRAW);
	}
	
	glBindVertexArray(0);
	CheckGLErrorMessages();

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
	CheckGLErrorMessages();
}

void GLRenderer::PostInitialize()
{
	// TODO: Find out what each shader needs and give it to them programmatically
	glm::uint program0 = m_LoadedShaders[0].program;
	glUseProgram(program0);

	m_LightDir = glGetUniformLocation(program0, "lightDir");
	CheckGLErrorMessages();
	m_AmbientColor = glGetUniformLocation(program0, "ambientColor");
	CheckGLErrorMessages();
	m_SpecularColor = glGetUniformLocation(program0, "specularColor");
	CheckGLErrorMessages();
	m_CamPos = glGetUniformLocation(program0, "camPos");
	CheckGLErrorMessages();

	int diffuseMapLocation = glGetUniformLocation(program0, "diffuseMap");
	glUniform1i(diffuseMapLocation, 0);
	CheckGLErrorMessages();
	
	int normalMapLocation = glGetUniformLocation(program0, "normalMap");
	glUniform1i(normalMapLocation, 1);
	CheckGLErrorMessages();

	int specularMapLocation = glGetUniformLocation(program0, "specularMap");
	glUniform1i(specularMapLocation, 2);
	CheckGLErrorMessages();
	
	int useDiffuseTextureLocation = glGetUniformLocation(program0, "useDiffuseTexture");
	if (useDiffuseTextureLocation == -1)
	{
		Logger::LogError("Couldn't find uniform \"useDiffuseTexture\" in shader!");
	}
	else
	{
		glUniform1i(useDiffuseTextureLocation, 1);
	}
	CheckGLErrorMessages();

	int useNormalTextureLocation = glGetUniformLocation(program0, "useNormalTexture");
	if (useNormalTextureLocation == -1)
	{
		Logger::LogError("Couldn't find uniform \"useNormalTexture\" in shader!");
	}
	else
	{
		glUniform1i(useNormalTextureLocation, 1);
	}
	CheckGLErrorMessages();

	int useSpecularTextureLocation = glGetUniformLocation(program0, "useSpecularTexture");
	if (useSpecularTextureLocation == -1)
	{
		Logger::LogError("Couldn't find uniform \"useSpecularTexture\" in shader!");
	}
	else
	{
		glUniform1i(useSpecularTextureLocation, 1);
	}
	CheckGLErrorMessages();

	glUseProgram(0);
}

void GLRenderer::Update(const GameContext& gameContext)
{
	CheckGLErrorMessages();

	glm::uint program0 = m_LoadedShaders[0].program;
	glUseProgram(program0);
	CheckGLErrorMessages();

	glUniform3f(m_LightDir, 
		m_SceneInfo.m_LightDir.x,
		m_SceneInfo.m_LightDir.y,
		m_SceneInfo.m_LightDir.z);
	CheckGLErrorMessages();

	glUniform4f(m_AmbientColor,
		m_SceneInfo.m_AmbientColor.r,
		m_SceneInfo.m_AmbientColor.g,
		m_SceneInfo.m_AmbientColor.b,
		m_SceneInfo.m_AmbientColor.a);
	CheckGLErrorMessages();

	glUniform4f(m_SpecularColor,
		m_SceneInfo.m_SpecularColor.r,
		m_SceneInfo.m_SpecularColor.g,
		m_SceneInfo.m_SpecularColor.b,
		m_SceneInfo.m_SpecularColor.a);
	CheckGLErrorMessages();

	glm::vec3 cameraPos = gameContext.camera->GetPosition();
	glUniform3f(m_CamPos,
		cameraPos.x, 
		cameraPos.y, 
		cameraPos.z);
	CheckGLErrorMessages();

	glUseProgram(0);
}

void GLRenderer::Draw(const GameContext& gameContext)
{
	UNREFERENCED_PARAMETER(gameContext);

	for (size_t i = 0; i < m_RenderObjects.size(); ++i)
	{
		RenderObject* renderObject = GetRenderObject(i);

		// TODO: Batch draw calls
		Shader* shader = &m_LoadedShaders[renderObject->shaderIndex];
		glUseProgram(shader->program);
		CheckGLErrorMessages();

		std::vector<glm::uint> texures;
		renderObject->GetTextures(texures);
		for (int j = 0; j < 3; ++j)
		{
			glActiveTexture(GL_TEXTURE0 + j);
			GLuint texture = texures[j];
			glBindTexture(GL_TEXTURE_2D, texures[j]);
			CheckGLErrorMessages();
		}

		glBindVertexArray(renderObject->VAO);
		glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
		CheckGLErrorMessages();

		if (renderObject->indexed)
		{
			glDrawElements(renderObject->topology, renderObject->indices->size(), GL_UNSIGNED_INT, (void*)renderObject->indices->data());
		}
		else
		{
			glDrawArrays(renderObject->topology, 0, renderObject->vertexBufferData->BufferSize);
		}
		CheckGLErrorMessages();

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		CheckGLErrorMessages();

		glUseProgram(0);
	}
}

void GLRenderer::ReloadShaders(GameContext& gameContext)
{
	UnloadShaders();
	LoadShaders();

	CheckGLErrorMessages();
}

void GLRenderer::UnloadShaders()
{
	const size_t shaderCount = m_LoadedShaders.size();
	for (size_t i = 0; i < shaderCount; ++i)
	{
		glDeleteProgram(m_LoadedShaders[i].program);
		CheckGLErrorMessages();
	}
	m_LoadedShaders.clear();
}

void GLRenderer::LoadShaders()
{
	std::vector<std::pair<std::string, std::string>> shaderFilePaths = {
		{ "resources/shaders/GLSL/simple.vert", "resources/shaders/GLSL/simple.frag" },
		{ "resources/shaders/GLSL/color.vert", "resources/shaders/GLSL/color.frag" },
	};

	const size_t shaderCount = shaderFilePaths.size();
	m_LoadedShaders.resize(shaderCount);

	for (size_t i = 0; i < shaderCount; ++i)
	{
		m_LoadedShaders[i].program = glCreateProgram();
		// Load shaders located at shaderFilePaths[i] and store ID into m_LoadedShaders[i]
		ShaderUtils::LoadShaders(
			m_LoadedShaders[i].program,
			shaderFilePaths[i].first, m_LoadedShaders[i].vertexShader,
			shaderFilePaths[i].second, m_LoadedShaders[i].fragmentShader);

		ShaderUtils::LinkProgram(m_LoadedShaders[i].program);
	}
}

void GLRenderer::OnWindowSize(int width, int height)
{
	glViewport(0, 0, width, height);
	CheckGLErrorMessages();
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
	CheckGLErrorMessages();
}

void GLRenderer::SwapBuffers(const GameContext& gameContext)
{
	glfwSwapBuffers(((GLWindowWrapper*)gameContext.window)->GetWindow());
}

void GLRenderer::UpdateTransformMatrix(const GameContext& gameContext, uint renderID, const glm::mat4& model)
{
	RenderObject* renderObject = GetRenderObject(renderID);
	Shader* shader = &m_LoadedShaders[renderObject->shaderIndex];
	glUseProgram(shader->program);

	// TODO: Make more dynamic by using uniform element enums
	if (renderObject->shaderIndex == 0)
	{
		glm::mat4 modelInv = glm::inverse(model);
		glm::mat3 modelInv3 = glm::mat3(modelInv);
		glUniformMatrix4fv(renderObject->model, 1, false, &model[0][0]);
		CheckGLErrorMessages();

		// OpenGL will transpose for us if we set the third param to true
		glUniformMatrix3fv(renderObject->modelInvTranspose, 1, true, &modelInv3[0][0]);
		CheckGLErrorMessages();

		glm::mat4 view = gameContext.camera->GetView();
		glUniformMatrix4fv(renderObject->view, 1, false, &view[0][0]);
		CheckGLErrorMessages();

		glm::mat4 projection = gameContext.camera->GetProjection();
		glUniformMatrix4fv(renderObject->projection, 1, false, &projection[0][0]);
		CheckGLErrorMessages();
	}
	else if (renderObject->shaderIndex == 1)
	{
		glm::mat4 proj = gameContext.camera->GetProjection();
		glm::mat4 view = gameContext.camera->GetView();
		glm::mat4 MVP = proj * view * model;
		glUniformMatrix4fv(renderObject->MVP, 1, false, &MVP[0][0]);
		CheckGLErrorMessages();
	}

	glUseProgram(0);
}

int GLRenderer::GetShaderUniformLocation(uint program, const std::string uniformName)
{
	int uniformLocation = glGetUniformLocation(program, uniformName.c_str());
	CheckGLErrorMessages();
	return uniformLocation;
}

void GLRenderer::SetUniform1f(uint location, float val)
{
	glUniform1f(location, val);
	CheckGLErrorMessages();
}

glm::uint GLRenderer::GetProgram(glm::uint renderID)
{
	RenderObject* renderObject = GetRenderObject(renderID);
	Shader* shader = &m_LoadedShaders[renderObject->shaderIndex];
	return shader->program;
}

void GLRenderer::DescribeShaderVariable(uint renderID, uint program, const std::string& variableName, int size,
	Renderer::Type renderType, bool normalized, int stride, void* pointer)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	glBindVertexArray(renderObject->VAO);
	CheckGLErrorMessages();

	GLint location = glGetAttribLocation(program, variableName.c_str());
	if (location == -1)
	{
		Logger::LogWarning("Invalid shader variable name: " + variableName);
		glBindVertexArray(0);
		return;
	}
	glEnableVertexAttribArray(location);

	GLenum glRenderType = TypeToGLType(renderType);
	glVertexAttribPointer(location, size, glRenderType, normalized, stride, pointer);
	CheckGLErrorMessages();

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

void GLRenderer::RenderObject::GetTextures(std::vector<glm::uint>& textures)
{
	textures = { diffuseMapID, specularMapID, normalMapID };
}
#endif // COMPILE_OPEN_GL

