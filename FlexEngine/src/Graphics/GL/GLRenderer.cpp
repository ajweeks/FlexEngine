#include "stdafx.h"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLRenderer.h"

#include <array>
#include <algorithm>
#include <string>
#include <utility>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "FreeCamera.h"
#include "Graphics/GL/GLHelpers.h"
#include "Logger.h"
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

		for (size_t i = 0; i < m_RenderObjects.size(); ++i)
		{
			Destroy(i);
			CheckGLErrorMessages();
		}
		m_RenderObjects.clear();
		CheckGLErrorMessages();

		glfwTerminate();
	}

	MaterialID GLRenderer::InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo)
	{
		UNREFERENCED_PARAMETER(gameContext);

		CheckGLErrorMessages();

		Material mat = {};

		mat.shaderIndex = createInfo->shaderIndex;
		mat.name = createInfo->name;
		mat.diffuseTexturePath = createInfo->diffuseTexturePath;
		mat.normalTexturePath = createInfo->normalTexturePath;
		mat.specularTexturePath = createInfo->specularTexturePath;
		mat.cubeMapFilePaths = createInfo->cubeMapFilePaths;

		UniformInfo uniformInfo[] = {
			{ Uniform::Type::MODEL_MAT4,					&mat.uniformIDs.modelID,				"in_Model" },
			{ Uniform::Type::MODEL_INV_TRANSPOSE_MAT4,		&mat.uniformIDs.modelInvTranspose,		"in_ModelInvTranspose" },
			{ Uniform::Type::MODEL_VIEW_PROJECTION_MAT4,	&mat.uniformIDs.modelViewProjection,	"in_ModelViewProjection" },
			{ Uniform::Type::CAM_POS_VEC4,					&mat.uniformIDs.camPos,					"in_CamPos" },
			{ Uniform::Type::VIEW_DIR_VEC4,					&mat.uniformIDs.viewDir,				"in_ViewDir" },
			{ Uniform::Type::LIGHT_DIR_VEC4,				&mat.uniformIDs.lightDir,				"in_LightDir" },
			{ Uniform::Type::AMBIENT_COLOR_VEC4,			&mat.uniformIDs.ambientColor,			"in_AmbientColor" },
			{ Uniform::Type::SPECULAR_COLOR_VEC4,			&mat.uniformIDs.specularColor,			"in_SpecularColor" },
			{ Uniform::Type::USE_DIFFUSE_TEXTURE_INT,		&mat.uniformIDs.useDiffuseTexture,		"in_UseDiffuseTexture" },
			{ Uniform::Type::USE_NORMAL_TEXTURE_INT,		&mat.uniformIDs.useNormalTexture,		"in_UseNormalTexture" },
			{ Uniform::Type::USE_SPECULAR_TEXTURE_INT,		&mat.uniformIDs.useSpecularTexture,		"in_UseSpecularTexture" },
			{ Uniform::Type::USE_CUBEMAP_TEXTURE_INT,		&mat.uniformIDs.useCubemapTexture,		"in_UseCubemapTexture" },
		};

		const glm::uint uniformCount = sizeof(uniformInfo) / sizeof(uniformInfo[0]);

		Shader& shader = m_LoadedShaders[mat.shaderIndex];

		for (size_t i = 0; i < uniformCount; ++i)
		{
			if (Uniform::HasUniform(shader.dynamicBufferUniforms, uniformInfo[i].type) ||
				Uniform::HasUniform(shader.constantBufferUniforms, uniformInfo[i].type))
			{
				*uniformInfo[i].id = glGetUniformLocation(shader.program, uniformInfo[i].name);
				if (*uniformInfo[i].id == -1) Logger::LogError(std::string(uniformInfo[i].name) + " was not found!");
			}
		}

		CheckGLErrorMessages();

		glUseProgram(shader.program);
		CheckGLErrorMessages();

		mat.diffuseTexturePath = createInfo->diffuseTexturePath;
		mat.useDiffuseTexture = !createInfo->diffuseTexturePath.empty();

		mat.normalTexturePath = createInfo->normalTexturePath;
		mat.useNormalTexture = !createInfo->normalTexturePath.empty();

		mat.specularTexturePath = createInfo->specularTexturePath;
		mat.useSpecularTexture = !createInfo->specularTexturePath.empty();

		if (mat.useDiffuseTexture)
		{
			GenerateGLTexture(mat.diffuseTextureID, createInfo->diffuseTexturePath);
			int diffuseLocation = glGetUniformLocation(shader.program, "in_DiffuseTexture");
			CheckGLErrorMessages();
			if (diffuseLocation == -1)
			{
				Logger::LogWarning("in_DiffuseTexture was not found in shader!");
			}
			else
			{
				glUniform1i(diffuseLocation, 0);
			}
			CheckGLErrorMessages();
		}

		if (mat.useNormalTexture)
		{
			GenerateGLTexture(mat.normalTextureID, createInfo->normalTexturePath);
			int normalLocation = glGetUniformLocation(shader.program, "in_NormalTexture");
			CheckGLErrorMessages();
			if (normalLocation == -1)
			{
				Logger::LogWarning("in_NormalTexture was not found in shader!");
			}
			else
			{
				glUniform1i(normalLocation, 1);
			}
			CheckGLErrorMessages();
		}

		if (mat.useSpecularTexture)
		{
			GenerateGLTexture(mat.specularTextureID, createInfo->specularTexturePath);
			int specularLocation = glGetUniformLocation(shader.program, "in_SpecularTexture");
			CheckGLErrorMessages();
			if (specularLocation == -1)
			{
				Logger::LogWarning("in_SpecularTexture was not found in shader!");
			}
			else
			{
				glUniform1i(specularLocation, 2);
			}
			CheckGLErrorMessages();
		}

		// Skybox
		if (!createInfo->cubeMapFilePaths[0].empty())
		{
			GenerateCubemapTextures(mat.diffuseTextureID, mat.cubeMapFilePaths);
			mat.useCubemapTexture = true;
		}

		glUseProgram(0);

		m_LoadedMaterials.push_back(mat);

		return m_LoadedMaterials.size() - 1;
	}

	glm::uint GLRenderer::InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo)
	{
		UNREFERENCED_PARAMETER(gameContext);

		const RenderID renderID = GetFirstAvailableRenderID();

		RenderObject* renderObject = new RenderObject(renderID);
		InsertNewRenderObject(renderObject);
		renderObject->materialID = createInfo->materialID;
		renderObject->cullFace = CullFaceToGLMode(createInfo->cullFace);

		renderObject->info = {};
		renderObject->info.materialName = m_LoadedMaterials[renderObject->materialID].name;
		renderObject->info.name = createInfo->name;

		if (m_LoadedMaterials.empty()) Logger::LogError("No materials have been created!");
		if (renderObject->materialID >= m_LoadedMaterials.size()) Logger::LogError("uninitialized material!");

		Material& material = m_LoadedMaterials[renderObject->materialID];

		Shader& shader = m_LoadedShaders[material.shaderIndex];
		glUseProgram(shader.program);
		CheckGLErrorMessages();

		glGenVertexArrays(1, &renderObject->VAO);
		glBindVertexArray(renderObject->VAO);
		CheckGLErrorMessages();

		glGenBuffers(1, &renderObject->VBO);
		glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)createInfo->vertexBufferData->BufferSize, createInfo->vertexBufferData->pDataStart, GL_STATIC_DRAW);
		CheckGLErrorMessages();

		renderObject->vertexBufferData = createInfo->vertexBufferData;
		
		if (createInfo->indices != nullptr)
		{
			renderObject->indices = createInfo->indices;
			renderObject->indexed = true;

			glGenBuffers(1, &renderObject->IBO);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderObject->IBO);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(createInfo->indices->at(0)) * createInfo->indices->size()), createInfo->indices->data(), GL_STATIC_DRAW);
		}

		glBindVertexArray(0);
		glUseProgram(0);

		return renderID;
	}

	void GLRenderer::PostInitializeRenderObject(RenderID renderID)
	{
		UNREFERENCED_PARAMETER(renderID);
	}

	void GLRenderer::SetTopologyMode(RenderID renderID, TopologyMode topology)
	{
		RenderObject* renderObject = GetRenderObject(renderID);
		if (!renderObject) return;

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
		// One vector per material containing all objects that use that material
		std::vector<std::vector<RenderObject*>> sortedRenderObjects;
		sortedRenderObjects.resize(m_LoadedMaterials.size());
		for (size_t i = 0; i < sortedRenderObjects.size(); ++i)
		{
			for (size_t j = 0; j < m_RenderObjects.size(); ++j)
			{
				RenderObject* renderObject = GetRenderObject(j);
				if (renderObject && renderObject->materialID == i)
				{
					sortedRenderObjects[i].push_back(renderObject);
				}
			}
		}

		for (size_t i = 0; i < sortedRenderObjects.size(); ++i)
		{
			Material* material = &m_LoadedMaterials[i];
			Shader* shader = &m_LoadedShaders[material->shaderIndex];
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

				texures.push_back(material->useDiffuseTexture ? material->diffuseTextureID : -1);
				texures.push_back(material->useNormalTexture ? material->normalTextureID : -1);
				texures.push_back(material->useSpecularTexture ? material->specularTextureID : -1);

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

				if (material->useCubemapTexture)
				{
					glBindTexture(GL_TEXTURE_CUBE_MAP, material->diffuseTextureID);
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
			CheckGLErrorMessages();

			// Load shaders located at shaderFilePaths[i] and store ID into m_LoadedShaders[i]
			ShaderUtils::LoadShaders(
				m_LoadedShaders[i].program,
				shaderFilePaths[i].first, m_LoadedShaders[i].vertexShader,
				shaderFilePaths[i].second, m_LoadedShaders[i].fragmentShader);

			ShaderUtils::LinkProgram(m_LoadedShaders[i].program);
		}
		CheckGLErrorMessages();
	}

	void GLRenderer::UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext)
	{
		RenderObject* renderObject = GetRenderObject(renderID);
		if (!renderObject) return;

		Material* material = &m_LoadedMaterials[renderObject->materialID];
		Shader* shader = &m_LoadedShaders[material->shaderIndex];

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
			glUniformMatrix4fv(material->uniformIDs.modelID, 1, false, &model[0][0]);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::MODEL_INV_TRANSPOSE_MAT4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::MODEL_INV_TRANSPOSE_MAT4))
		{
			// OpenGL will transpose for us if we set the third param to true
			glUniformMatrix4fv(material->uniformIDs.modelInvTranspose, 1, true, &modelInv[0][0]);
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
			glUniformMatrix4fv(material->uniformIDs.modelViewProjection, 1, false, &MVP[0][0]);
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
			glUniform4f(material->uniformIDs.camPos,
				camPos.x,
				camPos.y,
				camPos.z,
				camPos.w);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::VIEW_DIR_VEC4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::VIEW_DIR_VEC4))
		{
			glUniform4f(material->uniformIDs.viewDir,
				viewDir.x,
				viewDir.y,
				viewDir.z,
				viewDir.w);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::LIGHT_DIR_VEC4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::LIGHT_DIR_VEC4))
		{
			glUniform4f(material->uniformIDs.lightDir,
				m_SceneInfo.m_LightDir.x,
				m_SceneInfo.m_LightDir.y,
				m_SceneInfo.m_LightDir.z,
				m_SceneInfo.m_LightDir.w);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::AMBIENT_COLOR_VEC4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::AMBIENT_COLOR_VEC4))
		{
			glUniform4f(material->uniformIDs.ambientColor,
				m_SceneInfo.m_AmbientColor.r,
				m_SceneInfo.m_AmbientColor.g,
				m_SceneInfo.m_AmbientColor.b,
				m_SceneInfo.m_AmbientColor.a);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::SPECULAR_COLOR_VEC4) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::SPECULAR_COLOR_VEC4))
		{
			glUniform4f(material->uniformIDs.specularColor,
				m_SceneInfo.m_SpecularColor.r,
				m_SceneInfo.m_SpecularColor.g,
				m_SceneInfo.m_SpecularColor.b,
				m_SceneInfo.m_SpecularColor.a);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_DIFFUSE_TEXTURE_INT) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_DIFFUSE_TEXTURE_INT))
		{
			glUniform1i(material->uniformIDs.useDiffuseTexture, material->useDiffuseTexture);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_NORMAL_TEXTURE_INT) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_NORMAL_TEXTURE_INT))
		{
			glUniform1i(material->uniformIDs.useNormalTexture, material->useNormalTexture);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_SPECULAR_TEXTURE_INT) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_SPECULAR_TEXTURE_INT))
		{
			glUniform1i(material->uniformIDs.useSpecularTexture, material->useSpecularTexture);
			CheckGLErrorMessages();
		}

		if (Uniform::HasUniform(shader->dynamicBufferUniforms, Uniform::Type::USE_CUBEMAP_TEXTURE_INT) ||
			Uniform::HasUniform(shader->constantBufferUniforms, Uniform::Type::USE_CUBEMAP_TEXTURE_INT))
		{
			glUniform1i(material->uniformIDs.useCubemapTexture, material->useCubemapTexture);
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
		CheckGLErrorMessages();
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
		if (!renderObject) return;

		renderObject->model = model;
	}

	int GLRenderer::GetShaderUniformLocation(glm::uint program, const std::string& uniformName)
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

	glm::uint GLRenderer::GetRenderObjectCount() const
	{
		glm::uint count = 0;

		for (size_t i = 0; i < m_RenderObjects.size(); ++i)
		{
			if (m_RenderObjects[i]) ++count;
		}

		return count;
	}

	glm::uint GLRenderer::GetRenderObjectCapacity() const
	{
		return m_RenderObjects.size();
	}

	void GLRenderer::DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size,
		Renderer::Type renderType, bool normalized, int stride, void* pointer)
	{
		RenderObject* renderObject = GetRenderObject(renderID);
		if (!renderObject) return;

		Material* material = &m_LoadedMaterials[renderObject->materialID];
		glm::uint program = m_LoadedShaders[material->shaderIndex].program;

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
		RenderObject* renderObject = GetRenderObject(renderID);
		if (!renderObject) return;

		m_RenderObjects[renderObject->renderID] = nullptr;

		glDeleteBuffers(1, &renderObject->VBO);
		if (renderObject->indexed)
		{
			glDeleteBuffers(1, &renderObject->IBO);
		}

		SafeDelete(renderObject);
	}

	void GLRenderer::GetRenderObjectInfos(std::vector<RenderObjectInfo>& vec)
	{
		vec.clear();
		vec.resize(GetRenderObjectCount());

		for (size_t i = 0; i < m_RenderObjects.size(); ++i)
		{
			vec[i] = m_RenderObjects[i]->info;
		}
	}


	// This is the main rendering function that you have to implement and provide to ImGui (via setting up 'RenderDrawListsFn' in the ImGuiIO structure)
	void ImGui_RenderDrawLists(ImDrawData* draw_data)
	{
		// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
		ImGuiIO& io = ImGui::GetIO();
		int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
		int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
		if (fb_width == 0 || fb_height == 0)
			return;
		draw_data->ScaleClipRects(io.DisplayFramebufferScale);

		// Backup GL state
		GLenum last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&last_active_texture);
		glActiveTexture(GL_TEXTURE0);
		GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
		GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
		GLint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
		GLint last_element_array_buffer; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &last_element_array_buffer);
		GLint last_vertex_array; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);
		GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
		GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
		GLenum last_blend_src_rgb; glGetIntegerv(GL_BLEND_SRC_RGB, (GLint*)&last_blend_src_rgb);
		GLenum last_blend_dst_rgb; glGetIntegerv(GL_BLEND_DST_RGB, (GLint*)&last_blend_dst_rgb);
		GLenum last_blend_src_alpha; glGetIntegerv(GL_BLEND_SRC_ALPHA, (GLint*)&last_blend_src_alpha);
		GLenum last_blend_dst_alpha; glGetIntegerv(GL_BLEND_DST_ALPHA, (GLint*)&last_blend_dst_alpha);
		GLenum last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, (GLint*)&last_blend_equation_rgb);
		GLenum last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, (GLint*)&last_blend_equation_alpha);
		GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
		GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
		GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
		GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);

		// Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_SCISSOR_TEST);

		// Setup viewport, orthographic projection matrix
		glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
		const float ortho_projection[4][4] =
		{
			{ 2.0f / io.DisplaySize.x, 0.0f,                   0.0f, 0.0f },
			{ 0.0f,                  2.0f / -io.DisplaySize.y, 0.0f, 0.0f },
			{ 0.0f,                  0.0f,                  -1.0f, 0.0f },
			{ -1.0f,                  1.0f,                   0.0f, 1.0f },
		};
		glUseProgram(g_ShaderHandle);
		glUniform1i(g_AttribLocationTex, 0);
		glUniformMatrix4fv(g_AttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);
		glBindVertexArray(g_VaoHandle);

		for (int n = 0; n < draw_data->CmdListsCount; ++n)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[n];
			const ImDrawIdx* idx_buffer_offset = 0;

			glBindBuffer(GL_ARRAY_BUFFER, g_VboHandle);
			glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ElementsHandle);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

			for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i)
			{
				const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
				if (pcmd->UserCallback)
				{
					pcmd->UserCallback(cmd_list, pcmd);
				}
				else
				{
					glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
					glScissor((int)pcmd->ClipRect.x, (int)(fb_height - pcmd->ClipRect.w), (int)(pcmd->ClipRect.z - pcmd->ClipRect.x), (int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
					glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer_offset);
				}
				idx_buffer_offset += pcmd->ElemCount;
			}
		}

		// Restore modified GL state
		glUseProgram(last_program);
		glBindTexture(GL_TEXTURE_2D, last_texture);
		glActiveTexture(last_active_texture);
		glBindVertexArray(last_vertex_array);
		glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, last_element_array_buffer);
		glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
		glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha);
		if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
		if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
		if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
		if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
		glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
		glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
	}

	// TODO: Add to GLFW window
	static const char* ImGui_GetClipboardText(void* user_data)
	{
		return glfwGetClipboardString((GLFWwindow*)user_data);
	}

	static void ImGui_SetClipboardText(void* user_data, const char* text)
	{
		glfwSetClipboardString((GLFWwindow*)user_data, text);
	}

	bool GLRenderer::ImGui_CreateFontsTexture()
	{
		// Build texture atlas
		ImGuiIO& io = ImGui::GetIO();
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

		// Upload texture to graphics system
		GLint last_texture;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
		glGenTextures(1, &g_FontTexture); CheckGLErrorMessages();
		glBindTexture(GL_TEXTURE_2D, g_FontTexture); CheckGLErrorMessages();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

		// Store our identifier
		io.Fonts->TexID = (void *)(intptr_t)g_FontTexture;

		// Restore state
		glBindTexture(GL_TEXTURE_2D, last_texture); CheckGLErrorMessages();

		return true;
	}

	void GLRenderer::ImGui_Init(Window* /* window */)
	{
		ImGuiIO& io = ImGui::GetIO();

		io.RenderDrawListsFn = ImGui_RenderDrawLists;

		io.SetClipboardTextFn = ImGui_SetClipboardText;
		io.GetClipboardTextFn = ImGui_GetClipboardText;

		// TODO:
		//io.ClipboardUserData = g_Window;
#ifdef _WIN32
		//io.ImeWindowHandle = glfwGetWin32Window(g_Window);
#endif
	}

	void GLRenderer::ImGui_Shutdown()
	{
		ImGui_InvalidateDeviceObjects();
		ImGui::Shutdown();
	}

	void GLRenderer::ImGui_NewFrame(const GameContext& gameContext)
	{
		if (!g_FontTexture)
			ImGui_CreateDeviceObjects();

		ImGuiIO& io = ImGui::GetIO();

		glm::vec2i windowSize = gameContext.window->GetSize();
		glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
		io.DisplaySize = ImVec2((float)windowSize.x, (float)windowSize.y);
		io.DisplayFramebufferScale = ImVec2(
			windowSize.x > 0 ? ((float)frameBufferSize.x / windowSize.x) : 0, 
			windowSize.y > 0 ? ((float)frameBufferSize.y / windowSize.y) : 0);

		io.DeltaTime = gameContext.deltaTime;

		ImGui::NewFrame();
	}

	bool GLRenderer::ImGui_CreateDeviceObjects()
	{
		// Backup GL state
		GLint last_texture, last_array_buffer, last_vertex_array;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);

		const GLchar *vertex_shader =
			"#version 330\n"
			"uniform mat4 ProjMtx;\n"
			"in vec2 Position;\n"
			"in vec2 UV;\n"
			"in vec4 Color;\n"
			"out vec2 Frag_UV;\n"
			"out vec4 Frag_Color;\n"
			"void main()\n"
			"{\n"
			"	Frag_UV = UV;\n"
			"	Frag_Color = Color;\n"
			"	gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
			"}\n";

		const GLchar* fragment_shader =
			"#version 330\n"
			"uniform sampler2D Texture;\n"
			"in vec2 Frag_UV;\n"
			"in vec4 Frag_Color;\n"
			"out vec4 Out_Color;\n"
			"void main()\n"
			"{\n"
			"	Out_Color = Frag_Color * texture( Texture, Frag_UV.st);\n"
			"}\n";

		g_ShaderHandle = glCreateProgram(); CheckGLErrorMessages();
		g_VertHandle = glCreateShader(GL_VERTEX_SHADER); CheckGLErrorMessages();
		g_FragHandle = glCreateShader(GL_FRAGMENT_SHADER); CheckGLErrorMessages();
		glShaderSource(g_VertHandle, 1, &vertex_shader, 0); CheckGLErrorMessages();
		glShaderSource(g_FragHandle, 1, &fragment_shader, 0); CheckGLErrorMessages();
		glCompileShader(g_VertHandle); CheckGLErrorMessages();
		glCompileShader(g_FragHandle); CheckGLErrorMessages();
		glAttachShader(g_ShaderHandle, g_VertHandle); CheckGLErrorMessages();
		glAttachShader(g_ShaderHandle, g_FragHandle); CheckGLErrorMessages();
		glLinkProgram(g_ShaderHandle); CheckGLErrorMessages();

		g_AttribLocationTex = glGetUniformLocation(g_ShaderHandle, "Texture"); CheckGLErrorMessages();
		g_AttribLocationProjMtx = glGetUniformLocation(g_ShaderHandle, "ProjMtx"); CheckGLErrorMessages();
		g_AttribLocationPosition = glGetAttribLocation(g_ShaderHandle, "Position"); CheckGLErrorMessages();
		g_AttribLocationUV = glGetAttribLocation(g_ShaderHandle, "UV"); CheckGLErrorMessages();
		g_AttribLocationColor = glGetAttribLocation(g_ShaderHandle, "Color"); CheckGLErrorMessages();

		glGenBuffers(1, &g_VboHandle);
		glGenBuffers(1, &g_ElementsHandle);

		glGenVertexArrays(1, &g_VaoHandle); CheckGLErrorMessages();
		glBindVertexArray(g_VaoHandle); CheckGLErrorMessages();
		glBindBuffer(GL_ARRAY_BUFFER, g_VboHandle); CheckGLErrorMessages();
		glEnableVertexAttribArray(g_AttribLocationPosition); CheckGLErrorMessages();
		glEnableVertexAttribArray(g_AttribLocationUV); CheckGLErrorMessages();
		glEnableVertexAttribArray(g_AttribLocationColor); CheckGLErrorMessages();

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
		glVertexAttribPointer(g_AttribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, pos));
		glVertexAttribPointer(g_AttribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, uv));
		glVertexAttribPointer(g_AttribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, col));
#undef OFFSETOF

		ImGui_CreateFontsTexture();

		// Restore modified GL state
		glBindTexture(GL_TEXTURE_2D, last_texture); CheckGLErrorMessages();
		glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer); CheckGLErrorMessages();
		glBindVertexArray(last_vertex_array); CheckGLErrorMessages();

		return true;
	}

	void GLRenderer::ImGui_InvalidateDeviceObjects()
	{
		if (g_VaoHandle) glDeleteVertexArrays(1, &g_VaoHandle);
		if (g_VboHandle) glDeleteBuffers(1, &g_VboHandle);
		if (g_ElementsHandle) glDeleteBuffers(1, &g_ElementsHandle);
		g_VaoHandle = g_VboHandle = g_ElementsHandle = 0;
	
		if (g_ShaderHandle && g_VertHandle) glDetachShader(g_ShaderHandle, g_VertHandle);
		if (g_VertHandle) glDeleteShader(g_VertHandle);
		g_VertHandle = 0;
	
		if (g_ShaderHandle && g_FragHandle) glDetachShader(g_ShaderHandle, g_FragHandle);
		if (g_FragHandle) glDeleteShader(g_FragHandle);
		g_FragHandle = 0;
	
		if (g_ShaderHandle) glDeleteProgram(g_ShaderHandle);
		g_ShaderHandle = 0;
	
		if (g_FontTexture)
		{
			glDeleteTextures(1, &g_FontTexture);
			ImGui::GetIO().Fonts->TexID = 0;
			g_FontTexture = 0;
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

	void GLRenderer::InsertNewRenderObject(RenderObject* renderObject)
	{
		if (renderObject->renderID < m_RenderObjects.size())
		{
			assert(m_RenderObjects[renderObject->renderID] == nullptr);
			m_RenderObjects[renderObject->renderID] = renderObject;
		}
		else
		{
			m_RenderObjects.push_back(renderObject);
		}
	}

	RenderID GLRenderer::GetFirstAvailableRenderID() const
	{
		for (size_t i = 0; i < m_RenderObjects.size(); ++i)
		{
			if (!m_RenderObjects[i]) return i;
		}

		return m_RenderObjects.size();
	}


	GLRenderer::RenderObject::RenderObject(RenderID renderID, std::string name) :
		renderID(renderID)
	{
		info = {};
		info.name = name;
	}
} // namespace flex

#endif // COMPILE_OPEN_GL
