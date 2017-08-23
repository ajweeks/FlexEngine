#include "stdafx.h"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLRenderer.h"

#include <array>

#include <algorithm>
#include <utility>

#include <glm\gtc\type_ptr.hpp>
#include <glm\gtc\matrix_transform.hpp>

#include "FreeCamera.h"
#include "Graphics/GL/GLHelpers.h"
#include "Logger.h"
#include "ShaderUtils.h"
#include "Window/Window.h"

namespace flex
{
	GLRenderer::GLRenderer(GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);

		CheckGLErrorMessages();

		LoadShaders();

		CheckGLErrorMessages();

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
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

	glm::uint GLRenderer::InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo)
	{
		UNREFERENCED_PARAMETER(gameContext);

		const RenderID renderID = m_RenderObjects.size();

		RenderObject* renderObject = new RenderObject();
		m_RenderObjects.push_back(renderObject);
		renderObject->renderID = renderID;
		renderObject->shaderIndex = createInfo->shaderIndex;

		glUseProgram(m_LoadedShaders[renderObject->shaderIndex].program);

		glGenVertexArrays(1, &renderObject->VAO);
		glBindVertexArray(renderObject->VAO);
		CheckGLErrorMessages();

		glGenBuffers(1, &renderObject->VBO);
		glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)createInfo->vertexBufferData->BufferSize, createInfo->vertexBufferData->pDataStart, GL_STATIC_DRAW);
		CheckGLErrorMessages();

		renderObject->vertexBufferData = createInfo->vertexBufferData;

		renderObject->cullFace = CullFaceToGLMode(createInfo->cullFace);

		Shader* shader = &m_LoadedShaders[renderObject->shaderIndex];

		UniformInfo uniformInfo[] = {
			{ Uniform::Type::MODEL_MAT4,					&renderObject->uniformIDs.modelID,				"in_Model" },
			{ Uniform::Type::MODEL_INV_TRANSPOSE_MAT4,		&renderObject->uniformIDs.modelInvTranspose,	"in_ModelInvTranspose" },
			{ Uniform::Type::MODEL_VIEW_PROJECTION_MAT4,	&renderObject->uniformIDs.modelViewProjection,	"in_ModelViewProjection" },
			{ Uniform::Type::CAM_POS_VEC4,					&renderObject->uniformIDs.camPos,				"in_CamPos" },
			{ Uniform::Type::VIEW_DIR_VEC4,					&renderObject->uniformIDs.viewDir,				"in_ViewDir" },
			{ Uniform::Type::LIGHT_DIR_VEC4,				&renderObject->uniformIDs.lightDir,				"in_LightDir" },
			{ Uniform::Type::AMBIENT_COLOR_VEC4,			&renderObject->uniformIDs.ambientColor,			"in_AmbientColor" },
			{ Uniform::Type::SPECULAR_COLOR_VEC4,			&renderObject->uniformIDs.specularColor,		"in_SpecularColor" },
			{ Uniform::Type::USE_DIFFUSE_TEXTURE_INT,		&renderObject->uniformIDs.useDiffuseTexture,	"in_UseDiffuseTexture" },
			{ Uniform::Type::USE_NORMAL_TEXTURE_INT,		&renderObject->uniformIDs.useNormalTexture,		"in_UseNormalTexture" },
			{ Uniform::Type::USE_SPECULAR_TEXTURE_INT,		&renderObject->uniformIDs.useSpecularTexture,	"in_UseSpecularTexture" },
			{ Uniform::Type::USE_CUBEMAP_TEXTURE_INT,		&renderObject->uniformIDs.useCubemapTexture,	"in_UseCubemapTexture" },
		};

		const glm::uint uniformCount = sizeof(uniformInfo) / sizeof(uniformInfo[0]);

		for (size_t i = 0; i < uniformCount; ++i)
		{
			if (Uniform::HasUniform(shader->dynamicBufferUniforms, uniformInfo[i].type) ||
				Uniform::HasUniform(shader->constantBufferUniforms, uniformInfo[i].type))
			{
				*uniformInfo[i].id = glGetUniformLocation(shader->program, uniformInfo[i].name);
				if (*uniformInfo[i].id == -1) Logger::LogError(std::string(uniformInfo[i].name) + " was not found!");
			}
		}

		renderObject->diffuseTexturePath = createInfo->diffuseMapPath;
		renderObject->useDiffuseTexture = !renderObject->diffuseTexturePath.empty();

		renderObject->normalTexturePath = createInfo->normalMapPath;
		renderObject->useNormalTexture = !renderObject->normalTexturePath.empty();

		renderObject->specularTexturePath = createInfo->specularMapPath;
		renderObject->useSpecularTexture = !renderObject->specularTexturePath.empty();

		if (renderObject->useDiffuseTexture)
		{
			GenerateGLTexture(renderObject->VAO, renderObject->diffuseTextureID, createInfo->diffuseMapPath);
			int diffuseLocation = glGetUniformLocation(shader->program, "in_DiffuseTexture");
			glUniform1i(diffuseLocation, 0);
		}

		if (renderObject->useNormalTexture)
		{
			GenerateGLTexture(renderObject->VAO, renderObject->normalTextureID, createInfo->normalMapPath);
			int normalLocation = glGetUniformLocation(shader->program, "in_NormalTexture");
			glUniform1i(normalLocation, 1);
		}

		if (renderObject->useSpecularTexture)
		{
			GenerateGLTexture(renderObject->VAO, renderObject->specularTextureID, createInfo->specularMapPath);
			int specularLocation = glGetUniformLocation(shader->program, "in_SpecularTexture");
			glUniform1i(specularLocation, 2);
		}

		if (createInfo->indices != nullptr)
		{
			renderObject->indices = createInfo->indices;
			renderObject->indexed = true;

			glGenBuffers(1, &renderObject->IBO);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderObject->IBO);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(createInfo->indices->at(0)) * createInfo->indices->size()), createInfo->indices->data(), GL_STATIC_DRAW);
		}

		// Skybox
		if (!createInfo->cubeMapFilePaths[0].empty())
		{
			GenerateCubemapTextures(renderObject->diffuseTextureID, createInfo->cubeMapFilePaths);
			renderObject->useCubemapTexture = true;
		}

		glBindVertexArray(0);
		glUseProgram(0);

		return renderID;
	}

	void GLRenderer::SetTopologyMode(RenderID renderID, TopologyMode topology)
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
		int viewProjectionUBOLocation = glGetUniformBlockIndex(m_LoadedShaders[0].program, "ViewProjectionUBO");
		if (viewProjectionUBOLocation == -1)
		{
			Logger::LogWarning("Couldn't find ViewProjectionUBO in program " + std::to_string(m_LoadedShaders[0].program) + "!");
		}
		glm::uint simpleUBOBinding = 0;
		glUniformBlockBinding(m_LoadedShaders[0].program, viewProjectionUBOLocation, simpleUBOBinding);

		int viewProjectionCombinedUBOLocation = glGetUniformBlockIndex(m_LoadedShaders[1].program, "ViewProjectionCombinedUBO");
		if (viewProjectionCombinedUBOLocation == -1)
		{
			Logger::LogWarning("Couldn't find ViewProjectionCombinedUBO in program " + std::to_string(m_LoadedShaders[1].program) + "!");
		}
		glm::uint colorUBOBinding = 1;
		glUniformBlockBinding(m_LoadedShaders[1].program, viewProjectionCombinedUBOLocation, colorUBOBinding);

		glGenBuffers(1, &viewProjectionUBO);
		glBindBuffer(GL_UNIFORM_BUFFER, viewProjectionUBO);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(ViewProjectionUBO), NULL, GL_STATIC_DRAW);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
		glBindBufferRange(GL_UNIFORM_BUFFER, simpleUBOBinding, viewProjectionUBO, 0, sizeof(ViewProjectionUBO));

		glGenBuffers(1, &viewProjectionCombinedUBO);
		glBindBuffer(GL_UNIFORM_BUFFER, viewProjectionCombinedUBO);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(ViewProjectionCombinedUBO), NULL, GL_STATIC_DRAW);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
		glBindBufferRange(GL_UNIFORM_BUFFER, colorUBOBinding, viewProjectionCombinedUBO, 0, sizeof(ViewProjectionCombinedUBO));
	}

	void GLRenderer::Update(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);

		glm::mat4 view = gameContext.camera->GetView();
		glm::mat4 proj = gameContext.camera->GetProjection();

		ViewProjectionUBO viewProjection{ view, proj };
		glBindBuffer(GL_UNIFORM_BUFFER, viewProjectionUBO);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(viewProjection), &viewProjection, GL_STATIC_DRAW);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);

		ViewProjectionCombinedUBO viewProjectionCombined{ proj * view };
		glBindBuffer(GL_UNIFORM_BUFFER, viewProjectionCombinedUBO);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(viewProjectionCombined), &viewProjectionCombined, GL_STATIC_DRAW);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
	}

	void GLRenderer::Draw(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);

		// TODO: Only do this at startup and update when objects are added/removed?
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

				glBindVertexArray(renderObject->VAO);
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				CheckGLErrorMessages();

				glCullFace(renderObject->cullFace);

				UpdatePerObjectUniforms(renderObject->renderID, gameContext);

				std::vector<int> texures;

				texures.push_back(renderObject->useDiffuseTexture ? renderObject->diffuseTextureID : -1);
				texures.push_back(renderObject->useNormalTexture ? renderObject->normalTextureID : -1);
				texures.push_back(renderObject->useSpecularTexture ? renderObject->specularTextureID : -1);

				for (glm::uint k = 0; k < texures.size(); ++k)
				{
					if (texures[k] != -1)
					{
						GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)k);
						glActiveTexture(activeTexture);
						glBindTexture(GL_TEXTURE_2D, (GLuint)texures[k]);
						CheckGLErrorMessages();
					}
				}

				if (renderObject->useCubemapTexture)
				{
					glBindTexture(GL_TEXTURE_CUBE_MAP, renderObject->diffuseTextureID);
				}

				if (renderObject->indexed)
				{
					glDrawElements(renderObject->topology, (GLsizei)renderObject->indices->size(), GL_UNSIGNED_INT, (void*)renderObject->indices->data());
				}
				else
				{
					glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
				}
				CheckGLErrorMessages();
			}
		}

		glUseProgram(0);
		glBindVertexArray(0);
		CheckGLErrorMessages();
	}

	void GLRenderer::ReloadShaders(GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);

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
			{ RESOURCE_LOCATION + "shaders/GLSL/simple.vert", RESOURCE_LOCATION + "shaders/GLSL/simple.frag" },
			{ RESOURCE_LOCATION + "shaders/GLSL/color.vert", RESOURCE_LOCATION + "shaders/GLSL/color.frag" },
			// NOTE: Skybox shader should be kept last to keep other objects rendering in front
			{ RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/skybox.frag" },
		};

		const size_t shaderCount = shaderFilePaths.size();
		m_LoadedShaders.resize(shaderCount);

		glm::uint shaderIndex = 0;
		// Simple
		m_LoadedShaders[shaderIndex].constantBufferUniforms = Uniform::Type(
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
		++shaderIndex;

		// Color
		m_LoadedShaders[shaderIndex].constantBufferUniforms = Uniform::Type::NONE;

		m_LoadedShaders[shaderIndex].dynamicBufferUniforms = Uniform::Type(
			Uniform::Type::MODEL_MAT4);
		++shaderIndex;

		// Skybox
		m_LoadedShaders[shaderIndex].constantBufferUniforms = Uniform::Type::NONE;

		m_LoadedShaders[shaderIndex].dynamicBufferUniforms = Uniform::Type(
			Uniform::Type::USE_CUBEMAP_TEXTURE_INT |
			Uniform::Type::MODEL_MAT4);
		++shaderIndex;

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

	void GLRenderer::UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext)
	{
		RenderObject* renderObject = GetRenderObject(renderID);
		Shader* shader = &m_LoadedShaders[renderObject->shaderIndex];

		glm::mat4 model = renderObject->model;
		glm::mat4 modelInv = glm::inverse(renderObject->model);
		glm::mat4 proj = gameContext.camera->GetProjection();
		glm::mat4 view = gameContext.camera->GetView();
		glm::mat4 MVP = proj * view * model;

		// TODO: Move to uniform buffer
		glm::vec4 viewDir = glm::vec4(gameContext.camera->GetViewDirection(), 0.0f);
		glm::vec4 camPos = glm::vec4(gameContext.camera->GetPosition(), 0.0f);

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::MODEL_MAT4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::MODEL_MAT4))
		{
			glUniformMatrix4fv(renderObject->uniformIDs.modelID, 1, false, &model[0][0]);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::MODEL_INV_TRANSPOSE_MAT4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::MODEL_INV_TRANSPOSE_MAT4))
		{
			// OpenGL will transpose for us if we set the third param to true
			glUniformMatrix4fv(renderObject->uniformIDs.modelInvTranspose, 1, true, &modelInv[0][0]);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::VIEW_MAT4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::VIEW_MAT4))
		{
			Logger::LogWarning("Shader uniforms should not contain view matrix!");
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::VIEW_INV_MAT4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::VIEW_INV_MAT4))
		{
			Logger::LogWarning("Shader uniforms should not contain viewInv matrix!");

		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::PROJECTION_MAT4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::PROJECTION_MAT4))
		{
			Logger::LogWarning("Shader uniforms should not contain proj matrix!");
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::MODEL_VIEW_PROJECTION_MAT4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::MODEL_VIEW_PROJECTION_MAT4))
		{
			glUniformMatrix4fv(renderObject->uniformIDs.modelViewProjection, 1, false, &MVP[0][0]);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::VIEW_PROJECTION_MAT4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::VIEW_PROJECTION_MAT4))
		{
			Logger::LogWarning("Shader uniforms should not contain viewProj matrix!");
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::CAM_POS_VEC4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::CAM_POS_VEC4))
		{
			glUniform4f(renderObject->uniformIDs.camPos,
				camPos.x,
				camPos.y,
				camPos.z,
				camPos.w);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::VIEW_DIR_VEC4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::VIEW_DIR_VEC4))
		{
			glUniform4f(renderObject->uniformIDs.viewDir,
				viewDir.x,
				viewDir.y,
				viewDir.z,
				viewDir.w);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::LIGHT_DIR_VEC4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::LIGHT_DIR_VEC4))
		{
			glUniform4f(renderObject->uniformIDs.lightDir,
				m_SceneInfo.m_LightDir.x,
				m_SceneInfo.m_LightDir.y,
				m_SceneInfo.m_LightDir.z,
				m_SceneInfo.m_LightDir.w);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::AMBIENT_COLOR_VEC4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::AMBIENT_COLOR_VEC4))
		{
			glUniform4f(renderObject->uniformIDs.ambientColor,
				m_SceneInfo.m_AmbientColor.r,
				m_SceneInfo.m_AmbientColor.g,
				m_SceneInfo.m_AmbientColor.b,
				m_SceneInfo.m_AmbientColor.a);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::SPECULAR_COLOR_VEC4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::SPECULAR_COLOR_VEC4))
		{
			glUniform4f(renderObject->uniformIDs.specularColor,
				m_SceneInfo.m_SpecularColor.r,
				m_SceneInfo.m_SpecularColor.g,
				m_SceneInfo.m_SpecularColor.b,
				m_SceneInfo.m_SpecularColor.a);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_DIFFUSE_TEXTURE_INT) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_DIFFUSE_TEXTURE_INT))
		{
			glUniform1i(renderObject->uniformIDs.useDiffuseTexture, renderObject->useDiffuseTexture);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_NORMAL_TEXTURE_INT) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_NORMAL_TEXTURE_INT))
		{
			glUniform1i(renderObject->uniformIDs.useNormalTexture, renderObject->useNormalTexture);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_SPECULAR_TEXTURE_INT) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_SPECULAR_TEXTURE_INT))
		{
			glUniform1i(renderObject->uniformIDs.useSpecularTexture, renderObject->useSpecularTexture);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_CUBEMAP_TEXTURE_INT) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_CUBEMAP_TEXTURE_INT))
		{
			glUniform1i(renderObject->uniformIDs.useCubemapTexture, renderObject->useCubemapTexture);
			CheckGLErrorMessages();
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
		UNREFERENCED_PARAMETER(gameContext);

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

	void GLRenderer::UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model)
	{
		UNREFERENCED_PARAMETER(gameContext);

		RenderObject* renderObject = GetRenderObject(renderID);
		renderObject->model = model;
	}

	int GLRenderer::GetShaderUniformLocation(glm::uint program, const std::string uniformName)
	{
		int uniformLocation = glGetUniformLocation(program, uniformName.c_str());
		CheckGLErrorMessages();
		return uniformLocation;
	}

	void GLRenderer::SetUniform1f(int location, float val)
	{
		glUniform1f(location, val);
		CheckGLErrorMessages();
	}

	void GLRenderer::DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size,
		Renderer::Type renderType, bool normalized, int stride, void* pointer)
	{
		RenderObject* renderObject = GetRenderObject(renderID);

		glm::uint program = m_LoadedShaders[renderObject->shaderIndex].program;

		glBindVertexArray(renderObject->VAO);
		CheckGLErrorMessages();

		GLint location = glGetAttribLocation(program, variableName.c_str());
		if (location == -1)
		{
			Logger::LogWarning("Invalid shader variable name: " + variableName);
			glBindVertexArray(0);
			return;
		}
		glEnableVertexAttribArray((GLuint)location);

		GLenum glRenderType = TypeToGLType(renderType);
		glVertexAttribPointer((GLuint)location, size, glRenderType, (GLboolean)normalized, stride, pointer);
		CheckGLErrorMessages();

		glBindVertexArray(0);
	}

	void GLRenderer::Destroy(RenderID renderID)
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

	glm::uint GLRenderer::CullFaceToGLMode(CullFace cullFace)
	{
		switch (cullFace)
		{
		case CullFace::BACK: return GL_BACK;
		case CullFace::FRONT: return GL_FRONT;
		case CullFace::NONE: return GL_NONE; // TODO: This doesn't work, does it?
		default: return GL_FALSE;
		}
	}

	GLRenderer::RenderObject* GLRenderer::GetRenderObject(RenderID renderID)
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
} // namespace flex

#endif // COMPILE_OPEN_GL
