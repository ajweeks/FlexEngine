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

	struct u
	{
		Uniform::Type type;
		int* id;
		const GLchar* name;
	};

	u uniforms[] = {
		{ Uniform::Type::PROJECTION_MAT4,				&renderObject->projection,			"in_Projection" },
		{ Uniform::Type::VIEW_MAT4,						&renderObject->view,				"in_View" },
		{ Uniform::Type::VIEW_INV_MAT4,					&renderObject->viewInverse,			"in_ViewInverse" },
		{ Uniform::Type::VIEW_PROJECTION_MAT4,			&renderObject->viewProjection,		"in_ViewProjection" },
		{ Uniform::Type::MODEL_MAT4,					&renderObject->model,				"in_Model" },
		{ Uniform::Type::MODEL_INV_TRANSPOSE_MAT4,		&renderObject->modelInvTranspose,	"in_ModelInvTranspose" },
		{ Uniform::Type::MODEL_VIEW_PROJECTION_MAT4,	&renderObject->modelViewProjection,	"in_ModelViewProjection" },
		{ Uniform::Type::CAM_POS_VEC4,					&renderObject->camPos,				"in_CamPos" },
		{ Uniform::Type::VIEW_DIR_VEC4,					&renderObject->viewDir,				"in_ViewDir" },
		{ Uniform::Type::LIGHT_DIR_VEC4,				&renderObject->lightDir,			"in_LightDir" },
		{ Uniform::Type::AMBIENT_COLOR_VEC4,			&renderObject->ambientColor,		"in_AmbientColor" },
		{ Uniform::Type::SPECULAR_COLOR_VEC4,			&renderObject->specularColor,		"in_SpecularColor" },
		{ Uniform::Type::USE_DIFFUSE_TEXTURE_INT,		&renderObject->useDiffuseTexture,	"in_UseDiffuseTexture" },
		{ Uniform::Type::USE_NORMAL_TEXTURE_INT,		&renderObject->useNormalTexture,	"in_UseNormalTexture" },
		{ Uniform::Type::USE_SPECULAR_TEXTURE_INT,		&renderObject->useSpecularTexture,	"in_UseSpecularTexture" },
	};

	const glm::uint uniformCount = sizeof(uniforms) / sizeof(uniforms[0]);

	for (size_t i = 0; i < uniformCount; ++i)
	{
		if (Uniform::HasUniform(shader->dynamicBufferUniforms, uniforms[i].type) ||
			Uniform::HasUniform(shader->constantBufferUniforms, uniforms[i].type))
		{
			*uniforms[i].id = glGetUniformLocation(shader->program, uniforms[i].name);
			if (*uniforms[i].id == -1) Logger::LogError(std::string(uniforms[i].name) + " was not found!");
		}
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


	glUseProgram(0);
}

void GLRenderer::Update(const GameContext& gameContext)
{
	CheckGLErrorMessages();

	//glm::uint program0 = m_LoadedShaders[0].program;
	//glUseProgram(program0);
	//CheckGLErrorMessages();
	//
	//glUniform3f(m_LightDirID,
	//	m_SceneInfo.m_LightDir.x,
	//	m_SceneInfo.m_LightDir.y,
	//	m_SceneInfo.m_LightDir.z);
	//CheckGLErrorMessages();
	//
	//glUniform4f(m_AmbientColorID,
	//	m_SceneInfo.m_AmbientColor.r,
	//	m_SceneInfo.m_AmbientColor.g,
	//	m_SceneInfo.m_AmbientColor.b,
	//	m_SceneInfo.m_AmbientColor.a);
	//CheckGLErrorMessages();
	//
	//glUniform4f(m_SpecularColorID,
	//	m_SceneInfo.m_SpecularColor.r,
	//	m_SceneInfo.m_SpecularColor.g,
	//	m_SceneInfo.m_SpecularColor.b,
	//	m_SceneInfo.m_SpecularColor.a);
	//CheckGLErrorMessages();

	glUseProgram(0);
}

void GLRenderer::Draw(const GameContext& gameContext)
{
	UNREFERENCED_PARAMETER(gameContext);

	std::vector<std::vector<RenderObject*>> sortedRenderObjects;
	sortedRenderObjects.resize(m_LoadedShaders.size());
	for (size_t i = 0; i < sortedRenderObjects.size(); ++i)
	{
		for (size_t j = 0; j < m_RenderObjects.size(); ++j)
		{
			RenderObject* renderObject = GetRenderObject(j);
			if (renderObject->shaderIndex == i)
			{
				sortedRenderObjects[i].push_back(renderObject);
			}
		}
	}

	for (size_t i = 0; i < sortedRenderObjects.size(); ++i)
	{
		Shader* shader = &m_LoadedShaders[i];
		glUseProgram(shader->program);
		CheckGLErrorMessages();

		for (size_t j = 0; j < sortedRenderObjects[i].size(); ++j)
		{
			RenderObject* renderObject = sortedRenderObjects[i][j];

			std::vector<glm::uint> texures;

			if (Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_DIFFUSE_TEXTURE_INT))
			{
				texures.push_back(renderObject->diffuseMapID);
			}

			if (Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_NORMAL_TEXTURE_INT))
			{
				texures.push_back(renderObject->normalMapID);
			}

			if (Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_SPECULAR_TEXTURE_INT))
			{
				texures.push_back(renderObject->specularMapID);
			}

			for (int k = 0; k < texures.size(); ++k)
			{
				glActiveTexture(GL_TEXTURE0 + k);
				GLuint texture = texures[k];
				glBindTexture(GL_TEXTURE_2D, texures[k]);
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
		}

		glUseProgram(0);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	CheckGLErrorMessages();
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

	glm::uint shaderIndex = 0;
	m_LoadedShaders[shaderIndex].constantBufferUniforms = Uniform::Type(
		Uniform::Type::PROJECTION_MAT4 |
		Uniform::Type::VIEW_MAT4 |
		Uniform::Type::CAM_POS_VEC4 |
		Uniform::Type::LIGHT_DIR_VEC4 |
		Uniform::Type::AMBIENT_COLOR_VEC4 |
		Uniform::Type::SPECULAR_COLOR_VEC4 |
		Uniform::Type::USE_DIFFUSE_TEXTURE_INT |
		Uniform::Type::USE_NORMAL_TEXTURE_INT |
		Uniform::Type::USE_SPECULAR_TEXTURE_INT);

	m_LoadedShaders[shaderIndex].dynamicBufferUniforms = Uniform::Type(
		Uniform::Type::MODEL_MAT4 |
		Uniform::Type::MODEL_INV_TRANSPOSE_MAT4);

	// Color
	shaderIndex = 1;
	m_LoadedShaders[shaderIndex].constantBufferUniforms = Uniform::Type(
		Uniform::Type::VIEW_PROJECTION_MAT4);

	m_LoadedShaders[shaderIndex].dynamicBufferUniforms = Uniform::Type(
		Uniform::Type::MODEL_MAT4);

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

	glm::mat4 modelInv = glm::inverse(model);
	glm::mat4 proj = gameContext.camera->GetProjection();
	glm::mat4 view = gameContext.camera->GetView();
	glm::mat4 viewInv = glm::inverse(view);
	glm::mat4 viewProj = proj * view;
	glm::mat4 MVP = viewProj * model;
	glm::vec4 viewDir = glm::vec4(gameContext.camera->GetViewDirection(), 0.0f);
	glm::vec4 camPos = glm::vec4(gameContext.camera->GetPosition(), 0.0f);

	int useDiffuseTexture = 1;
	int useNormalTexture = 1;
	int useSpecularTexture = 1;

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::MODEL_MAT4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::MODEL_MAT4))
	{
		glUniformMatrix4fv(renderObject->model, 1, false, &model[0][0]);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::MODEL_INV_TRANSPOSE_MAT4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::MODEL_INV_TRANSPOSE_MAT4))
	{
		// OpenGL will transpose for us if we set the third param to true
		glUniformMatrix4fv(renderObject->modelInvTranspose, 1, true, &modelInv[0][0]);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::VIEW_MAT4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::VIEW_MAT4))
	{
		glUniformMatrix4fv(renderObject->view, 1, false, &view[0][0]);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::VIEW_INV_MAT4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::VIEW_INV_MAT4))
	{
		glUniformMatrix4fv(renderObject->view, 1, false, &viewInv[0][0]);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::PROJECTION_MAT4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::PROJECTION_MAT4))
	{
		glUniformMatrix4fv(renderObject->projection, 1, false, &proj[0][0]);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::MODEL_VIEW_PROJECTION_MAT4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::MODEL_VIEW_PROJECTION_MAT4))
	{
		glUniformMatrix4fv(renderObject->modelViewProjection, 1, false, &MVP[0][0]);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::VIEW_PROJECTION_MAT4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::VIEW_PROJECTION_MAT4))
	{
		glUniformMatrix4fv(renderObject->viewProjection, 1, false, &viewProj[0][0]);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::CAM_POS_VEC4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::CAM_POS_VEC4))
	{
		glUniform4f(renderObject->camPos,
			camPos.x,
			camPos.y,
			camPos.z,
			camPos.w);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::VIEW_DIR_VEC4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::VIEW_DIR_VEC4))
	{
		glUniform4f(renderObject->viewDir,
			viewDir.x,
			viewDir.y,
			viewDir.z,
			viewDir.w);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::LIGHT_DIR_VEC4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::LIGHT_DIR_VEC4))
	{
		glUniform4f(renderObject->lightDir,
			m_SceneInfo.m_LightDir.x,
			m_SceneInfo.m_LightDir.y,
			m_SceneInfo.m_LightDir.z,
			m_SceneInfo.m_LightDir.w);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::AMBIENT_COLOR_VEC4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::AMBIENT_COLOR_VEC4))
	{
		glUniform4f(renderObject->ambientColor,
			m_SceneInfo.m_AmbientColor.r,
			m_SceneInfo.m_AmbientColor.g,
			m_SceneInfo.m_AmbientColor.b,
			m_SceneInfo.m_AmbientColor.a);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::SPECULAR_COLOR_VEC4) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::SPECULAR_COLOR_VEC4))
	{
		glUniform4f(renderObject->specularColor,
			m_SceneInfo.m_SpecularColor.r,
			m_SceneInfo.m_SpecularColor.g,
			m_SceneInfo.m_SpecularColor.b,
			m_SceneInfo.m_SpecularColor.a);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_DIFFUSE_TEXTURE_INT) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_DIFFUSE_TEXTURE_INT))
	{
		glUniform1i(renderObject->useDiffuseTexture, useDiffuseTexture);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_NORMAL_TEXTURE_INT) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_NORMAL_TEXTURE_INT))
	{
		glUniform1i(renderObject->useNormalTexture, useNormalTexture);
		CheckGLErrorMessages();
	}

	if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_SPECULAR_TEXTURE_INT) ||
		Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_SPECULAR_TEXTURE_INT))
	{
		glUniform1i(renderObject->useSpecularTexture, useSpecularTexture);
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

#endif // COMPILE_OPEN_GL

