#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLRenderer.hpp"

#include <array>
#include <algorithm>
#include <string>
#include <utility>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "FreeCamera.hpp"
#include "Graphics/GL/GLHelpers.hpp"
#include "Logger.hpp"
#include "Window/Window.hpp"
#include "Window/GLFWWindowWrapper.hpp"
#include "VertexAttribute.hpp"

namespace flex
{
	namespace gl
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

			// Equirectangular Cube
			glGenFramebuffers(1, &m_CaptureFBO);
			glGenRenderbuffers(1, &m_CaptureRBO);
			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_CaptureRBO);
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

			m_gBufferQuadVertexBufferData.Destroy();

			glfwTerminate();
		}

		MaterialID GLRenderer::InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			CheckGLErrorMessages();

			GLMaterial mat = {};
			mat.material = {};

			if (!GetShader(createInfo->shaderName, mat.material.shaderID))
			{
				if (createInfo->shaderName.empty())
				{
					Logger::LogError("Material's shader not set! MaterialCreateInfo::shaderName must be filled in");
				}
				else
				{
					Logger::LogError("Material's shader not set! Shader name " + createInfo->shaderName + " not found");
				}
			}
			mat.material.name = createInfo->name;
			mat.material.diffuseTexturePath = createInfo->diffuseTexturePath;
			mat.material.normalTexturePath = createInfo->normalTexturePath;
			mat.material.specularTexturePath = createInfo->specularTexturePath;
			mat.material.cubeMapFilePaths = createInfo->cubeMapFilePaths;

			// TODO: Is this really needed? (do things dynamically instead?)
			UniformInfo uniformInfo[] = {
				{ "model", 							&mat.uniformIDs.model },
				{ "modelInvTranspose", 				&mat.uniformIDs.modelInvTranspose },
				{ "modelViewProjection",			&mat.uniformIDs.modelViewProjection },
				{ "view", 							&mat.uniformIDs.view },
				{ "viewInv", 						&mat.uniformIDs.viewInv },
				{ "viewProjection", 				&mat.uniformIDs.viewProjection },
				{ "projection", 					&mat.uniformIDs.projection },
				{ "camPos", 						&mat.uniformIDs.camPos },
				{ "useDiffuseSampler", 				&mat.uniformIDs.useDiffuseTexture },
				{ "material.useNormalSampler", 		&mat.uniformIDs.useNormalTexture },
				{ "useSpecularSampler", 			&mat.uniformIDs.useSpecularTexture },
				{ "useCubemapSampler", 				&mat.uniformIDs.useCubemapTexture },
				{ "useAlbedoSampler", 				&mat.uniformIDs.useAlbedoSampler },
				{ "constAlbedo", 					&mat.uniformIDs.constAlbedo },
				{ "useMetallicSampler", 			&mat.uniformIDs.useMetallicSampler },
				{ "constMetallic", 					&mat.uniformIDs.constMetallic },
				{ "useRoughnessSampler", 			&mat.uniformIDs.useRoughnessSampler },
				{ "constRoughness", 				&mat.uniformIDs.constRoughness },
				{ "useAOSampler",					&mat.uniformIDs.useAOSampler },
				{ "constAO",						&mat.uniformIDs.constAO },
				{ "equirectangularSampler",			&mat.uniformIDs.equirectangularSampler },
			};

			const glm::uint uniformCount = sizeof(uniformInfo) / sizeof(uniformInfo[0]);

			Shader& shader = m_Shaders[mat.material.shaderID];

			for (size_t i = 0; i < uniformCount; ++i)
			{
				if (shader.dynamicBufferUniforms.HasUniform(uniformInfo[i].name) ||
					shader.constantBufferUniforms.HasUniform(uniformInfo[i].name))
				{
					*uniformInfo[i].id = glGetUniformLocation(shader.program, uniformInfo[i].name);
					if (*uniformInfo[i].id == -1) Logger::LogWarning(std::string(uniformInfo[i].name) + " was not found for material " + createInfo->name + " (shader " + createInfo->shaderName + ")");
				}
			}

			CheckGLErrorMessages();

			glUseProgram(shader.program);
			CheckGLErrorMessages();

			mat.material.diffuseTexturePath = createInfo->diffuseTexturePath;
			mat.material.useDiffuseSampler = !createInfo->diffuseTexturePath.empty();

			mat.material.normalTexturePath = createInfo->normalTexturePath;
			mat.material.useNormalSampler = !createInfo->normalTexturePath.empty();

			mat.material.specularTexturePath = createInfo->specularTexturePath;
			mat.material.useSpecularSampler = !createInfo->specularTexturePath.empty();

			mat.material.usePositionFrameBufferSampler = createInfo->usePositionFrameBufferSampler;
			mat.material.useNormalFrameBufferSampler = createInfo->useNormalFrameBufferSampler;
			mat.material.useDiffuseSpecularFrameBufferSampler = createInfo->useDiffuseSpecularFrameBufferSampler;

			mat.material.useCubemapSampler = createInfo->useCubemapSampler || !createInfo->cubeMapFilePaths[0].empty();

			mat.material.constAlbedo = glm::vec4(createInfo->constAlbedo, 0);
			mat.material.albedoTexturePath = createInfo->albedoTexturePath;
			mat.material.useAlbedoSampler = createInfo->useAlbedoSampler;

			mat.material.constMetallic = createInfo->constMetallic;
			mat.material.metallicTexturePath = createInfo->metallicTexturePath;
			mat.material.useMetallicSampler = createInfo->useMetallicSampler;

			mat.material.constRoughness = createInfo->constRoughness;
			mat.material.roughnessTexturePath = createInfo->roughnessTexturePath;
			mat.material.useRoughnessSampler = createInfo->useRoughnessSampler;

			mat.material.constAO = createInfo->constAO;
			mat.material.aoTexturePath = createInfo->aoTexturePath;
			mat.material.useAOSampler = createInfo->useAOSampler;

			mat.material.useEquirectangularSampler = createInfo->useEquirectangularSampler;
			mat.material.equirectangularTexturePath = createInfo->equirectangularTexturePath;

			struct SamplerCreateInfo
			{
				bool create;
				glm::uint* id;
				std::string filepath;
				std::string textureName;
				std::function<bool(glm::uint&, const std::string&)> createFunction;
			};

			SamplerCreateInfo samplerCreateInfos[] =
			{
				{ mat.material.useAlbedoSampler, &mat.albedoSamplerID, createInfo->albedoTexturePath, "albedoSampler", GenerateGLTexture },
				{ mat.material.useMetallicSampler, &mat.metallicSamplerID, createInfo->metallicTexturePath, "metallicSampler", GenerateGLTexture },
				{ mat.material.useRoughnessSampler, &mat.roughnessSamplerID, createInfo->roughnessTexturePath, "roughnessSampler" , GenerateGLTexture },
				{ mat.material.useAOSampler, &mat.aoSamplerID, createInfo->aoTexturePath, "aoSampler", GenerateGLTexture },
				{ mat.material.useDiffuseSampler, &mat.diffuseSamplerID, createInfo->diffuseTexturePath, "diffuseSampler", GenerateGLTexture },
				{ mat.material.useNormalSampler, &mat.normalSamplerID, createInfo->normalTexturePath, "normalSampler", GenerateGLTexture },
				{ mat.material.useSpecularSampler, &mat.specularSamplerID, createInfo->specularTexturePath, "specularSampler", GenerateGLTexture },
				{ mat.material.useEquirectangularSampler, &mat.hdrTextureID, createInfo->equirectangularTexturePath, "equirectangularSampler", GenerateHDRGLTexture },
			};

			int binding = 0;

			for (SamplerCreateInfo& samplerCreateInfo : samplerCreateInfos)
			{
				if (samplerCreateInfo.create)
				{
					samplerCreateInfo.createFunction(*samplerCreateInfo.id, samplerCreateInfo.filepath);
					int uniformLocation = glGetUniformLocation(shader.program, samplerCreateInfo.textureName.c_str());
					CheckGLErrorMessages();
					if (uniformLocation == -1)
					{
						Logger::LogWarning(samplerCreateInfo.textureName + " was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].name + ")");
					}
					else
					{
						glUniform1i(uniformLocation, binding);
						++binding;
					}
					CheckGLErrorMessages();
				}
			}

			if (mat.material.usePositionFrameBufferSampler)
			{
				mat.positionFrameBufferSamplerID = createInfo->positionFrameBufferSamplerID;
				int positionLocation = glGetUniformLocation(shader.program, "positionFrameBufferSampler");
				CheckGLErrorMessages();
				if (positionLocation == -1)
				{
					Logger::LogWarning("positionFrameBufferSampler was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].name + ")");
				}
				else
				{
					glUniform1i(positionLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (mat.material.useNormalFrameBufferSampler)
			{
				mat.normalFrameBufferSamplerID = createInfo->normalFrameBufferSamplerID;
				int normalLocation = glGetUniformLocation(shader.program, "normalFrameBufferSampler");
				CheckGLErrorMessages();
				if (normalLocation == -1)
				{
					Logger::LogWarning("normalFrameBufferSampler was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].name + ")");
				}
				else
				{
					glUniform1i(normalLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (mat.material.useDiffuseSpecularFrameBufferSampler)
			{
				mat.diffuseSpecularFrameBufferSamplerID = createInfo->diffuseSpecularFrameBufferSamplerID;
				int diffuseSpecularLocation = glGetUniformLocation(shader.program, "diffuseSpecularFrameBufferSampler");
				CheckGLErrorMessages();
				if (diffuseSpecularLocation == -1)
				{
					Logger::LogWarning("diffuseSpecularFrameBufferSampler was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].name + ")");
				}
				else
				{
					glUniform1i(diffuseSpecularLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			// Skybox
			if (!createInfo->cubeMapFilePaths[0].empty())
			{
				GenerateGLCubemapTextures(mat.cubemapSamplerID, mat.material.cubeMapFilePaths);
			}

			if (createInfo->useCubemapSampler)
			{
				GenerateGLCubemap_Empty(mat.cubemapSamplerID, 512, 512);
			}

			glUseProgram(0);

			m_Materials.push_back(mat);

			return m_Materials.size() - 1;
		}

		glm::uint GLRenderer::InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			const RenderID renderID = GetFirstAvailableRenderID();

			RenderObject* renderObject = new RenderObject(renderID, createInfo->name);
			InsertNewRenderObject(renderObject);
			renderObject->materialID = createInfo->materialID;
			renderObject->cullFace = CullFaceToGLMode(createInfo->cullFace);

			renderObject->info = {};
			renderObject->info.materialName = m_Materials[renderObject->materialID].material.name;
			renderObject->info.name = createInfo->name;
			renderObject->info.transform = createInfo->transform;

			if (m_Materials.empty()) Logger::LogError("No materials have been created!");
			if (renderObject->materialID >= m_Materials.size()) Logger::LogError("uninitialized material!");

			GLMaterial& material = m_Materials[renderObject->materialID];

			Shader& shader = m_Shaders[material.material.shaderID];
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

		void GLRenderer::PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID)
		{
			RenderObject* renderObject = GetRenderObject(renderID);
			GLMaterial* renderObjectMaterial = &m_Materials[renderObject->materialID];

			// This material uses a generated cubemap, generate it now
			if (renderObjectMaterial->material.cubeMapFilePaths[0].empty() && renderObjectMaterial->material.useCubemapSampler)
			{
				MaterialCreateInfo equirectangularToCubeMatCreateInfo = {};
				equirectangularToCubeMatCreateInfo.shaderName = "equirectangular_to_cube";
				equirectangularToCubeMatCreateInfo.useEquirectangularSampler = true;
				equirectangularToCubeMatCreateInfo.equirectangularTexturePath = RESOURCE_LOCATION + "textures/skyboxes/Protospace_B/Protospace_B_Ref.hdr";
				MaterialID equirectangularToCubeMatID = InitializeMaterial(gameContext, &equirectangularToCubeMatCreateInfo);
				GLMaterial* equirectangularToCubeMaterial = &m_Materials[equirectangularToCubeMatID];
				ShaderID equirectangularToCubemapShaderID = equirectangularToCubeMaterial->material.shaderID;
				Shader* equirectangularToCubeShader = &m_Shaders[equirectangularToCubemapShaderID];

				// Convert HDR equirectangular texture to cubemap using six snapshots
				glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
				glm::mat4 captureViews[] =
				{
					glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
					glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
					glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
					glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
					glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
					glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
				};
				
				glUseProgram(equirectangularToCubeShader->program);
				CheckGLErrorMessages();

				renderObject->model = glm::mat4(1.0f);
				UpdatePerObjectUniforms(renderID, gameContext);

				glUniformMatrix4fv(equirectangularToCubeMaterial->uniformIDs.projection, 1, false, &captureProjection[0][0]);
				CheckGLErrorMessages();

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, equirectangularToCubeMaterial->hdrTextureID);
				CheckGLErrorMessages();

				glUniform1i(equirectangularToCubeMaterial->uniformIDs.equirectangularSampler, 0);
				CheckGLErrorMessages();

				glViewport(0, 0, 512, 512);
				CheckGLErrorMessages();
				glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				CheckGLErrorMessages();
				for (unsigned int i = 0; i < 6; ++i)
				{
					glUniformMatrix4fv(equirectangularToCubeMaterial->uniformIDs.view, 1, false, &captureViews[i][0][0]);
					CheckGLErrorMessages();
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, renderObjectMaterial->cubemapSamplerID, 0);
					CheckGLErrorMessages();
					glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
					CheckGLErrorMessages();

					glBindVertexArray(renderObject->VAO);
					glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
					CheckGLErrorMessages();
					
					glCullFace(renderObject->cullFace);
					glDrawArrays(GL_TRIANGLES, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
					CheckGLErrorMessages();
				}
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glBindRenderbuffer(GL_RENDERBUFFER, 0);

				glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
				glViewport(0, 0, frameBufferSize.x, frameBufferSize.y);
			}
		}

		DirectionalLightID GLRenderer::InitializeDirectionalLight(const DirectionalLight& dirLight)
		{
			m_DirectionalLight = dirLight;
			return 0;
		}

		PointLightID GLRenderer::InitializePointLight(const PointLight& pointLight)
		{
			m_PointLights.push_back(pointLight);
			return m_PointLights.size() - 1;
		}

		Renderer::DirectionalLight& GLRenderer::GetDirectionalLight(DirectionalLightID dirLightID)
		{
			// TODO: Add support for multiple directional lights
			UNREFERENCED_PARAMETER(dirLightID);
			return m_DirectionalLight;
		}

		Renderer::PointLight& GLRenderer::GetPointLight(PointLightID pointLightID)
		{
			return m_PointLights[pointLightID];
		}

		std::vector<Renderer::PointLight>& GLRenderer::GetAllPointLights()
		{
			return m_PointLights;
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

		void GLRenderer::PostInitialize(const GameContext& gameContext)
		{
			// G-buffer objects
			glGenFramebuffers(1, &m_gBufferHandle);
			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

			GenerateFrameBufferTexture(&m_gBuffer_PositionHandle, 0, GL_RGB16F, GL_RGB, frameBufferSize);
			GenerateFrameBufferTexture(&m_gBuffer_NormalHandle, 1, GL_RGB16F, GL_RGB, frameBufferSize);
			GenerateFrameBufferTexture(&m_gBuffer_DiffuseSpecularHandle, 2, GL_RGBA, GL_RGBA, frameBufferSize);

			constexpr int numBuffers = 3;
			unsigned int attachments[numBuffers] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
			glDrawBuffers(numBuffers, attachments);

			// Create and attach depth buffer
			glGenRenderbuffers(1, &m_gBufferDepthHandle);
			glBindRenderbuffer(GL_RENDERBUFFER, m_gBufferDepthHandle);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, frameBufferSize.x, frameBufferSize.y);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_gBufferDepthHandle);

			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				Logger::LogError("Framebuffer not complete!");
			}
			glBindFramebuffer(GL_FRAMEBUFFER, 0);


			MaterialCreateInfo gBufferMaterialCreateInfo = {};
			gBufferMaterialCreateInfo.name = "GBuffer material";
			gBufferMaterialCreateInfo.shaderName = "deferred_combine";

			gBufferMaterialCreateInfo.usePositionFrameBufferSampler = true;
			gBufferMaterialCreateInfo.positionFrameBufferSamplerID = m_gBuffer_PositionHandle;

			gBufferMaterialCreateInfo.useNormalFrameBufferSampler = true;
			gBufferMaterialCreateInfo.normalFrameBufferSamplerID = m_gBuffer_NormalHandle;

			gBufferMaterialCreateInfo.useDiffuseSpecularFrameBufferSampler = true;
			gBufferMaterialCreateInfo.diffuseSpecularFrameBufferSamplerID = m_gBuffer_DiffuseSpecularHandle;

			MaterialID gBufferMatID = InitializeMaterial(gameContext, &gBufferMaterialCreateInfo);

			VertexBufferData::CreateInfo gBufferQuadVertexBufferDataCreateInfo = {};

			gBufferQuadVertexBufferDataCreateInfo.positions_3D = {
				glm::vec3(-1.0f,  1.0f, 0.0f),
				glm::vec3(-1.0f, -1.0f, 0.0f),
				glm::vec3(1.0f,  1.0f, 0.0f),

				glm::vec3(1.0f, -1.0f, 0.0f),
				glm::vec3(1.0f,  1.0f, 0.0f),
				glm::vec3(-1.0f, -1.0f, 0.0f),
			};

			gBufferQuadVertexBufferDataCreateInfo.texCoords_UV = {
				glm::vec2(0.0f, 1.0f),
				glm::vec2(0.0f, 0.0f),
				glm::vec2(1.0f, 1.0f),

				glm::vec2(1.0f, 0.0f),
				glm::vec2(1.0f, 1.0f),
				glm::vec2(0.0f, 0.0f),
			};

			gBufferQuadVertexBufferDataCreateInfo.attributes = (glm::uint)VertexAttribute::POSITION | (glm::uint)VertexAttribute::UV;

			m_gBufferQuadVertexBufferData.Initialize(&gBufferQuadVertexBufferDataCreateInfo);

			RenderObjectCreateInfo gBufferQuadCreateInfo = {};
			gBufferQuadCreateInfo.name = "G Buffer Quad";
			gBufferQuadCreateInfo.materialID = gBufferMatID;
			m_gBufferQuadTransform = {}; // Identity transform
			gBufferQuadCreateInfo.transform = &m_gBufferQuadTransform;
			gBufferQuadCreateInfo.vertexBufferData = &m_gBufferQuadVertexBufferData;

			m_GBufferQuadRenderID = InitializeRenderObject(gameContext, &gBufferQuadCreateInfo);

			m_gBufferQuadVertexBufferData.DescribeShaderVariables(this, m_GBufferQuadRenderID);
			CheckGLErrorMessages();
		}

		void GLRenderer::GenerateFrameBufferTexture(glm::uint* handle, int index, GLint internalFormat, GLenum format, const glm::vec2i& size)
		{
			glGenTextures(1, handle);
			glBindTexture(GL_TEXTURE_2D, *handle);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.x, size.y, 0, format, GL_FLOAT, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, *handle, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			CheckGLErrorMessages();
		}

		void GLRenderer::ResizeFrameBufferTexture(glm::uint handle, int index, GLint internalFormat, GLenum format, const glm::vec2i& size)
		{
			UNREFERENCED_PARAMETER(index);

			glBindTexture(GL_TEXTURE_2D, handle);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.x, size.y, 0, format, GL_FLOAT, NULL);
			CheckGLErrorMessages();
		}

		void GLRenderer::ResizeRenderBuffer(glm::uint handle, const glm::vec2i& size)
		{
			glBindRenderbuffer(GL_RENDERBUFFER, handle);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, size.x, size.y);
		}

		void GLRenderer::Update(const GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);
			CheckGLErrorMessages();
		}

		void GLRenderer::DrawRenderObjectBatch(const std::vector<RenderObject*>& batchedRenderObjects, const GameContext& gameContext)
		{
			assert(!batchedRenderObjects.empty());

			GLMaterial* material = &m_Materials[batchedRenderObjects[0]->materialID];
			Shader* shader = &m_Shaders[material->material.shaderID];
			glUseProgram(shader->program);
			CheckGLErrorMessages();

			for (size_t i = 0; i < batchedRenderObjects.size(); ++i)
			{
				RenderObject* renderObject = batchedRenderObjects[i];

				glBindVertexArray(renderObject->VAO);
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				CheckGLErrorMessages();

				glCullFace(renderObject->cullFace);

				UpdatePerObjectUniforms(renderObject->renderID, gameContext);

				std::vector<int> texures;

				texures.push_back(material->material.useAlbedoSampler ? material->albedoSamplerID : -1);
				texures.push_back(material->material.useMetallicSampler ? material->metallicSamplerID : -1);
				texures.push_back(material->material.useRoughnessSampler ? material->roughnessSamplerID : -1);
				texures.push_back(material->material.useAOSampler ? material->aoSamplerID : -1);
				texures.push_back(material->material.useDiffuseSampler ? material->diffuseSamplerID : -1);
				texures.push_back(material->material.useNormalSampler ? material->normalSamplerID : -1);
				texures.push_back(material->material.useSpecularSampler ? material->specularSamplerID : -1);
				//texures.push_back(material->material.useEquirectangularSampler ? material->hdrTextureID : -1);

				glm::uint location = 0;
				for (glm::uint j = 0; j < texures.size(); ++j)
				{
					if (texures[j] != -1)
					{
						GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)location);
						glActiveTexture(activeTexture);
						glBindTexture(GL_TEXTURE_2D, (GLuint)texures[j]);
						++location;
					}
					CheckGLErrorMessages();
				}

				if (material->material.useCubemapSampler)
				{
					glBindTexture(GL_TEXTURE_CUBE_MAP, material->cubemapSamplerID);
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

		bool GLRenderer::GetShader(const std::string& shaderName, ShaderID& shaderID)
		{
			// TODO: Store shaders using sorted data structure?
			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				if (m_Shaders[i].name.compare(shaderName) == 0)
				{
					shaderID = i;
					return true;
				}
			}

			return false;
		}

		void GLRenderer::Draw(const GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);
			CheckGLErrorMessages();

			// Sort render objects into deferred + forward buckets
			std::vector<std::vector<RenderObject*>> deferredRenderObjectBatches;
			std::vector<std::vector<RenderObject*>> forwardRenderObjectBatches;
			for (size_t i = 0; i < m_Materials.size() - 1; ++i) // Last material is deferred combine, it's treated specially
			{
				UpdateMaterialUniforms(gameContext, i);

				if (m_Shaders[m_Materials[i].material.shaderID].deferred)
				{
					deferredRenderObjectBatches.push_back({});
					for (size_t j = 0; j < m_RenderObjects.size(); ++j)
					{
						RenderObject* renderObject = GetRenderObject(j);
						if (renderObject && renderObject->materialID == i)
						{
							deferredRenderObjectBatches[deferredRenderObjectBatches.size() - 1].push_back(renderObject);
						}
					}
				}
				else
				{
					forwardRenderObjectBatches.push_back({});
					for (size_t j = 0; j < m_RenderObjects.size(); ++j)
					{
						RenderObject* renderObject = GetRenderObject(j);
						if (renderObject && renderObject->materialID == i)
						{
							forwardRenderObjectBatches[forwardRenderObjectBatches.size() - 1].push_back(renderObject);
						}
					}
				}
			}

			// Geometry pass - Render scene's geometry into gbuffer render targets
			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);
			CheckGLErrorMessages();

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			CheckGLErrorMessages();

			for (size_t i = 0; i < deferredRenderObjectBatches.size(); ++i)
			{
				if (!deferredRenderObjectBatches[i].empty())
				{
					DrawRenderObjectBatch(deferredRenderObjectBatches[i], gameContext);
				}
			}

			glUseProgram(0);
			glBindVertexArray(0);
			CheckGLErrorMessages();

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

			// Copy depth from gbuffer to default render target
			glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferHandle);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			glBlitFramebuffer(0, 0, frameBufferSize.x, frameBufferSize.y, 0, 0, frameBufferSize.x, frameBufferSize.y, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			// Don't write gbuffer quad into depth buffer
			glDepthMask(GL_FALSE);

			// Lighting pass - Calculate lighting based on the gbuffer's contents
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			RenderObject* gBufferQuad = GetRenderObject(m_GBufferQuadRenderID);


			// TODO: Draw offscreen quad once for each deferred material type (store deferred matID in shaders, remove gBufferQuad->materialID) 


			glUseProgram(m_Shaders[m_Materials[gBufferQuad->materialID].material.shaderID].program);

			glBindVertexArray(gBufferQuad->VAO);
			glBindBuffer(GL_ARRAY_BUFFER, gBufferQuad->VBO);
			CheckGLErrorMessages();
			UpdatePerObjectUniforms(gBufferQuad->renderID, gameContext);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_gBuffer_PositionHandle);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, m_gBuffer_NormalHandle);
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, m_gBuffer_DiffuseSpecularHandle);

			glCullFace(gBufferQuad->cullFace);
			glDrawArrays(GL_TRIANGLES, 0, (GLsizei)gBufferQuad->vertexBufferData->VertexCount);
			CheckGLErrorMessages();

			glDepthMask(GL_TRUE);

			// Forward pass - draw all objects which don't use deferred shading
			for (size_t i = 0; i < forwardRenderObjectBatches.size(); ++i)
			{
				if (!forwardRenderObjectBatches[i].empty())
				{
					DrawRenderObjectBatch(forwardRenderObjectBatches[i], gameContext);
				}
			}

			// Draw UI
			ImDrawData* drawData = ImGui::GetDrawData();

			// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
			ImGuiIO& io = ImGui::GetIO();
			int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
			int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
			if (fb_width == 0 || fb_height == 0)
				return;
			drawData->ScaleClipRects(io.DisplayFramebufferScale);

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
			glUseProgram(m_ImGuiShaderHandle);
			//glUniform1i(m_ImGuiAttribLocationTex, 0);
			glUniformMatrix4fv(m_ImGuiAttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);
			glBindVertexArray(m_ImGuiVaoHandle);

			for (int n = 0; n < drawData->CmdListsCount; ++n)
			{
				const ImDrawList* cmd_list = drawData->CmdLists[n];
				const ImDrawIdx* idx_buffer_offset = 0;

				glBindBuffer(GL_ARRAY_BUFFER, m_ImGuiVboHandle);
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


			glfwSwapBuffers(((GLWindowWrapper*)gameContext.window)->GetWindow());
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
			const size_t shaderCount = m_Shaders.size();
			for (size_t i = 0; i < shaderCount; ++i)
			{
				glDeleteProgram(m_Shaders[i].program);
				CheckGLErrorMessages();
			}
			m_Shaders.clear();
		}

		void GLRenderer::LoadShaders()
		{
			m_Shaders = {
				{ "deferred_simple", RESOURCE_LOCATION + "shaders/GLSL/deferred_simple.vert", RESOURCE_LOCATION + "shaders/GLSL/deferred_simple.frag" },
				{ "color", RESOURCE_LOCATION + "shaders/GLSL/color.vert", RESOURCE_LOCATION + "shaders/GLSL/color.frag" },
				{ "imgui", RESOURCE_LOCATION + "shaders/GLSL/imgui.vert", RESOURCE_LOCATION + "shaders/GLSL/imgui.frag" },

				{ "pbr", RESOURCE_LOCATION + "shaders/GLSL/pbr.vert", RESOURCE_LOCATION + "shaders/GLSL/pbr.frag" },
				{ "skybox", RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/skybox.frag" },
				{ "equirectangular_to_cube", RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/equirectangular_to_cube.frag" },
				{ "background", RESOURCE_LOCATION + "shaders/GLSL/background.vert", RESOURCE_LOCATION + "shaders/GLSL/background.frag" },

				// NOTE: Deferred shader must be kept last
				{ "deferred_combine", RESOURCE_LOCATION + "shaders/GLSL/deferred_combine.vert", RESOURCE_LOCATION + "shaders/GLSL/deferred_combine.frag" },
			};

			ShaderID shaderID = 0;

			// Deferred Simple
			m_Shaders[shaderID].deferred = true;
			m_Shaders[shaderID].constantBufferUniforms = {};

			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("model", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("modelInvTranspose", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("viewProjection", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("useDiffuseSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("useNormalSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("useSpecularSampler", true);
			++shaderID;

			// Color
			m_Shaders[shaderID].deferred = false;
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("viewProjection", true);

			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("model", true);
			++shaderID;

			// ImGui
			m_Shaders[shaderID].deferred = false;
			m_Shaders[shaderID].constantBufferUniforms = {};

			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("model", true);
			++shaderID;

			// PBR
			m_Shaders[shaderID].deferred = false;
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("viewProjection", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("pointLights", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("camPos", true);

			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("model", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("modelInvTranspose", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("useAlbedoSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("constAlbedo", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("useMetallicSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("constMetallic", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("useRoughnessSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("constRoughness", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("useAOSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("constAO", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("useNormalSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("albedoSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("metallicSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("roughnessSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("aoSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("normalSampler", true);
			++shaderID;

			// Skybox
			m_Shaders[shaderID].deferred = false;
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("view", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("projection", true);

			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("model", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("useCubemapSampler", true);
			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("cubemapSampler", true);
			++shaderID;

			// Equirectangular to Cube
			m_Shaders[shaderID].deferred = false;
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("view", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("projection", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("equirectangularSampler", true);

			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("model", true);
			++shaderID;

			// Background
			m_Shaders[shaderID].deferred = false;
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("view", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("projection", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("cubemapSampler", true);

			m_Shaders[shaderID].dynamicBufferUniforms.AddUniform("model", true);
			++shaderID;

			// Deferred combine (sample gbuffer)
			m_Shaders[shaderID].deferred = false; // Sounds strange but this isn't deferred
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("positionFrameBufferSampler", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("normalFrameBufferSampler", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("diffuseSpecularFrameBufferSampler", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("pointLights", true);
			m_Shaders[shaderID].constantBufferUniforms.AddUniform("dirLight", true);

			m_Shaders[shaderID].dynamicBufferUniforms = {};
			++shaderID;

			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				m_Shaders[i].program = glCreateProgram();
				CheckGLErrorMessages();

				if (!LoadGLShaders(m_Shaders[i].program, m_Shaders[i]))
				{
					Logger::LogError("Couldn't load shaders " + m_Shaders[i].vertexShaderFilePath + " and " + m_Shaders[i].fragmentShaderFilePath + "!");
				}

				LinkProgram(m_Shaders[i].program);
			}

			m_ImGuiShaderHandle = m_Shaders[2].program;

			CheckGLErrorMessages();
		}

		void GLRenderer::UpdateMaterialUniforms(const GameContext& gameContext, MaterialID materialID)
		{
			GLMaterial* material = &m_Materials[materialID];
			Shader* shader = &m_Shaders[material->material.shaderID];
			
			glUseProgram(shader->program);

			glm::mat4 proj = gameContext.camera->GetProjection();
			glm::mat4 view = gameContext.camera->GetView();
			glm::mat4 viewInv = glm::inverse(view);
			glm::mat4 viewProj = proj * view;
			glm::vec4 camPos = glm::vec4(gameContext.camera->GetPosition(), 0.0f);


			if (shader->constantBufferUniforms.HasUniform("view"))
			{
				glUniformMatrix4fv(material->uniformIDs.view, 1, false, &view[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->constantBufferUniforms.HasUniform("viewInv"))
			{
				glUniformMatrix4fv(material->uniformIDs.viewInv, 1, false, &viewInv[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->constantBufferUniforms.HasUniform("projection"))
			{
				glUniformMatrix4fv(material->uniformIDs.projection, 1, false, &proj[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->constantBufferUniforms.HasUniform("viewProjection"))
			{
				glUniformMatrix4fv(material->uniformIDs.viewProjection, 1, false, &viewProj[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->constantBufferUniforms.HasUniform("camPos"))
			{
				glUniform4f(material->uniformIDs.camPos,
					camPos.x,
					camPos.y,
					camPos.z,
					camPos.w);
				CheckGLErrorMessages();
			}

			if (shader->constantBufferUniforms.HasUniform("dirLight"))
			{
				if (m_DirectionalLight.enabled)
				{
					SetUInt(material->material.shaderID, "dirLight.enabled", 1);
					CheckGLErrorMessages();
					SetVec4f(material->material.shaderID, "dirLight.direction", m_DirectionalLight.direction);
					CheckGLErrorMessages();
					SetVec4f(material->material.shaderID, "dirLight.color", m_DirectionalLight.color);
					CheckGLErrorMessages();
				}
				else
				{
					SetUInt(material->material.shaderID, "dirLight.enabled", 0);
					CheckGLErrorMessages();
				}
			}

			if (shader->constantBufferUniforms.HasUniform("pointLights"))
			{
				for (size_t i = 0; i < m_PointLights.size(); ++i)
				{
					const std::string numberStr = std::to_string(i);

					if (m_PointLights[i].enabled)
					{
						SetUInt(material->material.shaderID, "pointLights[" + numberStr + "].enabled", 1);
						CheckGLErrorMessages();

						SetVec4f(material->material.shaderID, "pointLights[" + numberStr + "].position", m_PointLights[i].position);
						CheckGLErrorMessages();

						SetVec4f(material->material.shaderID, "pointLights[" + numberStr + "].color", m_PointLights[i].color);
						CheckGLErrorMessages();
					}
					else
					{
						SetUInt(material->material.shaderID, "pointLights[" + numberStr + "].enabled", 0);
						CheckGLErrorMessages();
					}
				}
			}
		}


		void GLRenderer::UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext)
		{
			RenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			GLMaterial* material = &m_Materials[renderObject->materialID];
			Shader* shader = &m_Shaders[material->material.shaderID];

			glm::mat4 model = renderObject->model;
			glm::mat4 modelInv = glm::inverse(renderObject->model);
			glm::mat4 proj = gameContext.camera->GetProjection();
			glm::mat4 view = gameContext.camera->GetView();
			glm::mat4 MVP = proj * view * model;

			if (shader->dynamicBufferUniforms.HasUniform("model"))
			{
				glUniformMatrix4fv(material->uniformIDs.model, 1, false, &model[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("modelInvTranspose"))
			{
				// OpenGL will transpose for us if we set the third param to true
				glUniformMatrix4fv(material->uniformIDs.modelInvTranspose, 1, true, &modelInv[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("useDiffuseSampler"))
			{
				glUniform1i(material->uniformIDs.useDiffuseTexture, material->material.useDiffuseSampler);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("material.useNormalSampler"))
			{
				glUniform1i(material->uniformIDs.useNormalTexture, material->material.useNormalSampler);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("useSpecularSampler"))
			{
				glUniform1i(material->uniformIDs.useSpecularTexture, material->material.useSpecularSampler);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("useCubemapSampler"))
			{
				glUniform1i(material->uniformIDs.useCubemapTexture, material->material.useCubemapSampler);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("useAlbedoSampler"))
			{
				glUniform1ui(material->uniformIDs.useAlbedoSampler, material->material.useAlbedoSampler);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("constAlbedo"))
			{
				glUniform4f(material->uniformIDs.constAlbedo, material->material.constAlbedo.x, material->material.constAlbedo.y, material->material.constAlbedo.z, 0);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("useMetallicSampler"))
			{
				glUniform1ui(material->uniformIDs.useMetallicSampler, material->material.useMetallicSampler);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("constMetallic"))
			{
				glUniform1f(material->uniformIDs.constMetallic, material->material.constMetallic);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("useRoughnessSampler"))
			{
				glUniform1ui(material->uniformIDs.useRoughnessSampler, material->material.useRoughnessSampler);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("constRoughness"))
			{
				glUniform1f(material->uniformIDs.constRoughness, material->material.constRoughness);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("useAOSampler"))
			{
				glUniform1ui(material->uniformIDs.useAOSampler, material->material.useAOSampler);
				CheckGLErrorMessages();
			}

			if (shader->dynamicBufferUniforms.HasUniform("constAO"))
			{
				glUniform1f(material->uniformIDs.constAO, material->material.constAO);
				CheckGLErrorMessages();
			}
		}

		void GLRenderer::OnWindowSize(int width, int height)
		{
			glViewport(0, 0, width, height);
			CheckGLErrorMessages();

			const glm::vec2i newFrameBufferSize(width, height);

			// TODO: Store formats so they aren't duplicated here
			ResizeFrameBufferTexture(m_gBuffer_PositionHandle, 0, GL_RGB16F, GL_RGB, newFrameBufferSize);
			ResizeFrameBufferTexture(m_gBuffer_NormalHandle, 1, GL_RGB16F, GL_RGB, newFrameBufferSize);
			ResizeFrameBufferTexture(m_gBuffer_DiffuseSpecularHandle, 2, GL_RGBA, GL_RGBA, newFrameBufferSize);

			ResizeRenderBuffer(m_gBufferDepthHandle, newFrameBufferSize);
		}

		void GLRenderer::SetVSyncEnabled(bool enableVSync)
		{
			m_VSyncEnabled = enableVSync;
			glfwSwapInterval(enableVSync ? 1 : 0);
			CheckGLErrorMessages();
		}

		// TODO: Remove function
		void GLRenderer::UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model)
		{
			UNREFERENCED_PARAMETER(gameContext);

			RenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			renderObject->model = model;
		}

		void GLRenderer::SetFloat(ShaderID shaderID, const std::string& valName, float val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName.c_str());
			if (location == -1) Logger::LogWarning("Float " + valName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform1f(location, val);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetUInt(ShaderID shaderID, const std::string& valName, glm::uint val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName.c_str());
			if (location == -1) Logger::LogWarning("Unsigned int " + valName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform1ui(location, val);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec2f(ShaderID shaderID, const std::string& vecName, const glm::vec2& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1) Logger::LogWarning("Vec2f " + vecName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform2f(location, vec[0], vec[1]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec3f(ShaderID shaderID, const std::string& vecName, const glm::vec3& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1) Logger::LogWarning("Vec3f " + vecName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform3f(location, vec[0], vec[1], vec[2]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec4f(ShaderID shaderID, const std::string& vecName, const glm::vec4& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1) Logger::LogWarning("Vec4f " + vecName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform4f(location, vec[0], vec[1], vec[2], vec[3]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetMat4f(ShaderID shaderID, const std::string& matName, const glm::mat4& mat)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, matName.c_str());
			if (location == -1) Logger::LogWarning("Mat4f " + matName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniformMatrix4fv(location, 1, false, &mat[0][0]);
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

			GLMaterial* material = &m_Materials[renderObject->materialID];
			glm::uint program = m_Shaders[material->material.shaderID].program;

			glUseProgram(program);

			glBindVertexArray(renderObject->VAO);
			CheckGLErrorMessages();

			GLint location = glGetAttribLocation(program, variableName.c_str());
			if (location == -1)
			{
				//Logger::LogWarning("Invalid shader variable name: " + variableName);
				glBindVertexArray(0);
				return;
			}
			glEnableVertexAttribArray((GLuint)location);

			GLenum glRenderType = TypeToGLType(renderType);
			glVertexAttribPointer((GLuint)location, size, glRenderType, (GLboolean)normalized, stride, pointer);
			CheckGLErrorMessages();

			glBindVertexArray(0);

			glUseProgram(0);
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
			glGenTextures(1, &m_ImGuiFontTexture); CheckGLErrorMessages();
			glBindTexture(GL_TEXTURE_2D, m_ImGuiFontTexture); CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

			// Store our identifier
			io.Fonts->TexID = (void *)(intptr_t)m_ImGuiFontTexture;

			// Restore state
			glBindTexture(GL_TEXTURE_2D, last_texture); CheckGLErrorMessages();

			return true;
		}

		void GLRenderer::ImGui_Init(const GameContext& gameContext)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.RenderDrawListsFn = NULL;

			io.SetClipboardTextFn = SetClipboardText;
			io.GetClipboardTextFn = GetClipboardText;
			io.ClipboardUserData = gameContext.window;

			glm::vec2i windowSize = gameContext.window->GetSize();
			glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			io.DisplaySize = ImVec2((float)windowSize.x, (float)windowSize.y);
			io.DisplayFramebufferScale = ImVec2(
				windowSize.x > 0 ? ((float)frameBufferSize.x / windowSize.x) : 0,
				windowSize.y > 0 ? ((float)frameBufferSize.y / windowSize.y) : 0);

			io.DeltaTime = gameContext.deltaTime;

			ImGui_CreateDeviceObjects();

			// TODO:
			//io.ClipboardUserData = g_Window;
#ifdef _WIN32
		//io.ImeWindowHandle = glfwGetWin32Window(g_Window);
#endif
		}

		void GLRenderer::ImGui_NewFrame(const GameContext& gameContext)
		{
			ImGui::NewFrame();

			ImGuiIO& io = ImGui::GetIO();

			glm::vec2i windowSize = gameContext.window->GetSize();
			glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			io.DisplaySize = ImVec2((float)windowSize.x, (float)windowSize.y);
			io.DisplayFramebufferScale = ImVec2(
				windowSize.x > 0 ? ((float)frameBufferSize.x / windowSize.x) : 0,
				windowSize.y > 0 ? ((float)frameBufferSize.y / windowSize.y) : 0);

			io.DeltaTime = gameContext.deltaTime;
		}

		void GLRenderer::ImGui_Render()
		{
			ImGui::Render();
		}

		void GLRenderer::ImGui_ReleaseRenderObjects()
		{
			ImGui_InvalidateDeviceObjects();
		}

		bool GLRenderer::ImGui_CreateDeviceObjects()
		{
			// Backup GL state
			GLint last_texture, last_array_buffer, last_vertex_array;
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
			glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
			glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);

			//m_ImGuiShaderHandle = glCreateProgram(); CheckGLErrorMessages();
			//m_ImGuiVertHandle = glCreateShader(GL_VERTEX_SHADER); CheckGLErrorMessages();
			//m_ImGuiFragHandle = glCreateShader(GL_FRAGMENT_SHADER); CheckGLErrorMessages();
			//glShaderSource(m_ImGuiVertHandle, 1, &vertex_shader, 0); CheckGLErrorMessages();
			//glShaderSource(m_ImGuiFragHandle, 1, &fragment_shader, 0); CheckGLErrorMessages();
			//glCompileShader(m_ImGuiVertHandle); CheckGLErrorMessages();
			//glCompileShader(m_ImGuiFragHandle); CheckGLErrorMessages();
			//glAttachShader(m_ImGuiShaderHandle, m_ImGuiVertHandle); CheckGLErrorMessages();
			//glAttachShader(m_ImGuiShaderHandle, m_ImGuiFragHandle); CheckGLErrorMessages();
			//glLinkProgram(m_ImGuiShaderHandle); CheckGLErrorMessages();

			glUseProgram(m_ImGuiShaderHandle);

			m_ImGuiAttribLocationTex = glGetUniformLocation(m_ImGuiShaderHandle, "in_Texture"); CheckGLErrorMessages();
			if (m_ImGuiAttribLocationTex == -1) Logger::LogWarning("in_Texture not found in ImGui shader!");

			m_ImGuiAttribLocationProjMtx = glGetUniformLocation(m_ImGuiShaderHandle, "in_ProjMatrix"); CheckGLErrorMessages();
			if (m_ImGuiAttribLocationProjMtx == -1) Logger::LogWarning("in_ProjMatrix not found in ImGui shader!");

			m_ImGuiAttribLocationPosition = glGetAttribLocation(m_ImGuiShaderHandle, "in_Position2D"); CheckGLErrorMessages();
			if (m_ImGuiAttribLocationPosition == -1) Logger::LogWarning("in_Position2D not found in ImGui shader!");

			m_ImGuiAttribLocationUV = glGetAttribLocation(m_ImGuiShaderHandle, "in_TexCoord"); CheckGLErrorMessages();
			if (m_ImGuiAttribLocationUV == -1) Logger::LogWarning("in_TexCoord not found in ImGui shader!");

			m_ImGuiAttribLocationColor = glGetAttribLocation(m_ImGuiShaderHandle, "in_Color_32"); CheckGLErrorMessages();
			if (m_ImGuiAttribLocationColor == -1) Logger::LogWarning("in_Color_32 not found in ImGui shader!");


			glGenBuffers(1, &m_ImGuiVboHandle);
			glGenBuffers(1, &g_ElementsHandle);

			glGenVertexArrays(1, &m_ImGuiVaoHandle); CheckGLErrorMessages();
			glBindVertexArray(m_ImGuiVaoHandle); CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, m_ImGuiVboHandle); CheckGLErrorMessages();
			glEnableVertexAttribArray((GLuint)m_ImGuiAttribLocationPosition); CheckGLErrorMessages();
			glEnableVertexAttribArray((GLuint)m_ImGuiAttribLocationUV); CheckGLErrorMessages();
			glEnableVertexAttribArray((GLuint)m_ImGuiAttribLocationColor); CheckGLErrorMessages();

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
			glVertexAttribPointer(m_ImGuiAttribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, pos));
			glVertexAttribPointer(m_ImGuiAttribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, uv));
			glVertexAttribPointer(m_ImGuiAttribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, col));
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
			if (m_ImGuiVaoHandle) glDeleteVertexArrays(1, &m_ImGuiVaoHandle);
			if (m_ImGuiVboHandle) glDeleteBuffers(1, &m_ImGuiVboHandle);
			if (g_ElementsHandle) glDeleteBuffers(1, &g_ElementsHandle);
			m_ImGuiVaoHandle = m_ImGuiVboHandle = g_ElementsHandle = 0;

			if (m_ImGuiShaderHandle) glDeleteProgram(m_ImGuiShaderHandle);
			m_ImGuiShaderHandle = 0;

			if (m_ImGuiFontTexture)
			{
				glDeleteTextures(1, &m_ImGuiFontTexture);
				ImGui::GetIO().Fonts->TexID = 0;
				m_ImGuiFontTexture = 0;
			}
		}

		RenderObject* GLRenderer::GetRenderObject(RenderID renderID)
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


		void SetClipboardText(void* userData, const char* text)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			glfwWindow->SetClipboardText(text);
		}

		const char* GetClipboardText(void* userData)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			return glfwWindow->GetClipboardText();
		}

	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL
