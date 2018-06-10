#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLRenderer.hpp"

#include <array>
#include <algorithm>
#include <string>
#include <utility>
#include <functional>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "ImGui/imgui_impl_glfw_gl3.h"

#include <BulletDynamics\Dynamics\btDiscreteDynamicsWorld.h>

#include <freetype/ftbitmap.h>

#include "Cameras/CameraManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/GL/GLHelpers.hpp"
#include "Graphics/GL/GLPhysicsDebugDraw.hpp"
#include "Logger.hpp"
#include "Window/Window.hpp"
#include "Window/GLFWWindowWrapper.hpp"
#include "VertexAttribute.hpp"
#include "GameContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/GameObject.hpp"
#include "Helpers.hpp"
#include "Physics/PhysicsWorld.hpp"

namespace flex
{
	namespace gl
	{
		GLRenderer::GLRenderer(GameContext& gameContext)
		{
			gameContext.renderer = this;

			CheckGLErrorMessages();
		}

		GLRenderer::~GLRenderer()
		{
			
		}

		void GLRenderer::Initialize(const GameContext& gameContext)
		{
			CheckGLErrorMessages();

			m_BRDFTextureSize = { 512, 512 };
			m_BRDFTextureHandle = {};
			m_BRDFTextureHandle.internalFormat = GL_RG16F;
			m_BRDFTextureHandle.format = GL_RG;
			m_BRDFTextureHandle.type = GL_FLOAT;

			m_OffscreenTexture0Handle = {};
			m_OffscreenTexture0Handle.internalFormat = GL_RGBA16F;
			m_OffscreenTexture0Handle.format = GL_RGBA;
			m_OffscreenTexture0Handle.type = GL_FLOAT;

			m_OffscreenTexture1Handle = {};
			m_OffscreenTexture1Handle.internalFormat = GL_RGBA16F;
			m_OffscreenTexture1Handle.format = GL_RGBA;
			m_OffscreenTexture1Handle.type = GL_FLOAT;

			m_LoadingTextureHandle = {};
			m_LoadingTextureHandle.internalFormat = GL_RGBA;
			m_LoadingTextureHandle.format = GL_RGBA;
			m_LoadingTextureHandle.type = GL_FLOAT;


			m_gBuffer_PositionMetallicHandle = {};
			m_gBuffer_PositionMetallicHandle.internalFormat = GL_RGBA16F;
			m_gBuffer_PositionMetallicHandle.format = GL_RGBA;
			m_gBuffer_PositionMetallicHandle.type = GL_FLOAT;

			m_gBuffer_NormalRoughnessHandle = {};
			m_gBuffer_NormalRoughnessHandle.internalFormat = GL_RGBA16F;
			m_gBuffer_NormalRoughnessHandle.format = GL_RGBA;
			m_gBuffer_NormalRoughnessHandle.type = GL_FLOAT;

			m_gBuffer_DiffuseAOHandle = {};
			m_gBuffer_DiffuseAOHandle.internalFormat = GL_RGBA;
			m_gBuffer_DiffuseAOHandle.format = GL_RGBA;
			m_gBuffer_DiffuseAOHandle.type = GL_FLOAT;


			CheckGLErrorMessages();

			LoadShaders();

			CheckGLErrorMessages();

			glEnable(GL_DEPTH_TEST);
			//glDepthFunc(GL_LEQUAL);
			CheckGLErrorMessages();

			glFrontFace(GL_CCW);
			CheckGLErrorMessages();

			// Prevent seams from appearing on lower mip map levels of cubemaps
			glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);


			// Capture framebuffer (TODO: Merge with offscreen frame buffer?)
			{
				glGenFramebuffers(1, &m_CaptureFBO);
				glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
				CheckGLErrorMessages();

				glGenRenderbuffers(1, &m_CaptureRBO);
				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512); // TODO: Remove 512
				CheckGLErrorMessages();
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_CaptureRBO);
				CheckGLErrorMessages();

				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				{
					Logger::LogError("Capture frame buffer is incomplete!");
				}

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glBindRenderbuffer(GL_RENDERBUFFER, 0);
				CheckGLErrorMessages();
			}

			// Offscreen framebuffers
			{
				glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
				CreateOffscreenFrameBuffer(&m_Offscreen0FBO, &m_Offscreen0RBO, frameBufferSize, m_OffscreenTexture0Handle);
				CreateOffscreenFrameBuffer(&m_Offscreen1FBO, &m_Offscreen1RBO, frameBufferSize, m_OffscreenTexture1Handle);
			}

			const real captureProjectionNearPlane = gameContext.cameraManager->CurrentCamera()->GetZNear();
			const real captureProjectionFarPlane = gameContext.cameraManager->CurrentCamera()->GetZFar();
			m_CaptureProjection = glm::perspective(glm::radians(90.0f), 1.0f, captureProjectionNearPlane, captureProjectionFarPlane);
			m_CaptureViews =
			{
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
			};

			GenerateGLTexture(m_LoadingTextureHandle.id, RESOURCE_LOCATION + "textures/loading_1.png", false, false);
			GenerateGLTexture(m_WorkTextureHandle.id, RESOURCE_LOCATION + "textures/work_d.jpg", false, false);

			// Sprite quad
			MaterialCreateInfo spriteMatCreateInfo = {};
			spriteMatCreateInfo.name = "Sprite material";
			spriteMatCreateInfo.shaderName = "sprite";
			spriteMatCreateInfo.engineMaterial = true;
			m_SpriteMatID = InitializeMaterial(gameContext, &spriteMatCreateInfo);


			MaterialCreateInfo fontMatCreateInfo = {};
			fontMatCreateInfo.name = "Font material";
			fontMatCreateInfo.shaderName = "font";
			fontMatCreateInfo.engineMaterial = true;
			m_FontMatID = InitializeMaterial(gameContext, &fontMatCreateInfo);


			MaterialCreateInfo postProcessMatCreateInfo = {};
			postProcessMatCreateInfo.name = "Post process material";
			postProcessMatCreateInfo.shaderName = "post_process";
			postProcessMatCreateInfo.engineMaterial = true;
			m_PostProcessMatID = InitializeMaterial(gameContext, &postProcessMatCreateInfo);
			

			MaterialCreateInfo postFXAAMatCreateInfo = {};
			postFXAAMatCreateInfo.name = "FXAA";
			postFXAAMatCreateInfo.shaderName = "post_fxaa";
			postFXAAMatCreateInfo.engineMaterial = true;
			m_PostFXAAMatID = InitializeMaterial(gameContext, &postFXAAMatCreateInfo);
			

			// Sprite quad
			{
				VertexBufferData::CreateInfo spriteQuadVertexBufferDataCreateInfo = {};
				spriteQuadVertexBufferDataCreateInfo.positions_2D = {
					glm::vec2(-1.0f,  -1.0f),
					glm::vec2(-1.0f, 1.0f),
					glm::vec2(1.0f,  -1.0f),

					glm::vec2(1.0f,  -1.0f),
					glm::vec2(-1.0f, 1.0f),
					glm::vec2(1.0f, 1.0f),
				};

				spriteQuadVertexBufferDataCreateInfo.texCoords_UV = {
					glm::vec2(0.0f, 0.0f),
					glm::vec2(0.0f, 1.0f),
					glm::vec2(1.0f, 0.0f),

					glm::vec2(1.0f, 0.0f),
					glm::vec2(0.0f, 1.0f),
					glm::vec2(1.0f, 1.0f),
				};

				spriteQuadVertexBufferDataCreateInfo.colors_R32G32B32A32 = {
					glm::vec4(1.0f),
					glm::vec4(1.0f),
					glm::vec4(1.0f),

					glm::vec4(1.0f),
					glm::vec4(1.0f),
					glm::vec4(1.0f),
				};

				spriteQuadVertexBufferDataCreateInfo.attributes =
					(u32)VertexAttribute::POSITION_2D |
					(u32)VertexAttribute::UV |
					(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

				m_SpriteQuadVertexBufferData = {};
				m_SpriteQuadVertexBufferData.Initialize(&spriteQuadVertexBufferDataCreateInfo);


				GameObject* spriteQuadGameObject = new GameObject("Sprite Quad", GameObjectType::NONE);
				m_PersistentObjects.push_back(spriteQuadGameObject);
				spriteQuadGameObject->SetVisible(false);

				RenderObjectCreateInfo spriteQuadCreateInfo = {};
				spriteQuadCreateInfo.vertexBufferData = &m_SpriteQuadVertexBufferData;
				spriteQuadCreateInfo.materialID = m_SpriteMatID;
				spriteQuadCreateInfo.depthWriteEnable = false;
				spriteQuadCreateInfo.gameObject = spriteQuadGameObject;
				spriteQuadCreateInfo.enableCulling = false;
				spriteQuadCreateInfo.visibleInSceneExplorer = false;
				spriteQuadCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
				spriteQuadCreateInfo.depthWriteEnable = false;
				m_SpriteQuadRenderID = InitializeRenderObject(gameContext, &spriteQuadCreateInfo);

				m_SpriteQuadVertexBufferData.DescribeShaderVariables(this, m_SpriteQuadRenderID);
			}

			// Draw loading text
			{
				glm::vec3 pos(0.0f);
				glm::quat rot = glm::quat();
				glm::vec3 scale(1.0f, -1.0f, 1.0f);
				glm::vec4 color(1.0f);
				DrawSpriteQuad(gameContext, m_LoadingTextureHandle.id, 0, 0, m_SpriteMatID,
							   pos, rot, scale, AnchorPoint::WHOLE, color);
				SwapBuffers(gameContext);
			}

			if (m_BRDFTextureHandle.id == 0)
			{
				Logger::LogInfo("Generating BRDF LUT");
				GenerateGLTexture_Empty(m_BRDFTextureHandle.id, m_BRDFTextureSize, false,
										m_BRDFTextureHandle.internalFormat, m_BRDFTextureHandle.format, m_BRDFTextureHandle.type);
				GenerateBRDFLUT(gameContext, m_BRDFTextureHandle.id, m_BRDFTextureSize);
			}

			ImGui::CreateContext();
			CheckGLErrorMessages();


			// G-buffer objects
			glGenFramebuffers(1, &m_gBufferHandle);
			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

			GenerateFrameBufferTexture(&m_gBuffer_PositionMetallicHandle.id,
									   0,
									   m_gBuffer_PositionMetallicHandle.internalFormat,
									   m_gBuffer_PositionMetallicHandle.format,
									   m_gBuffer_PositionMetallicHandle.type,
									   frameBufferSize);

			GenerateFrameBufferTexture(&m_gBuffer_NormalRoughnessHandle.id,
									   1,
									   m_gBuffer_NormalRoughnessHandle.internalFormat,
									   m_gBuffer_NormalRoughnessHandle.format,
									   m_gBuffer_NormalRoughnessHandle.type,
									   frameBufferSize);

			GenerateFrameBufferTexture(&m_gBuffer_DiffuseAOHandle.id,
									   2,
									   m_gBuffer_DiffuseAOHandle.internalFormat,
									   m_gBuffer_DiffuseAOHandle.format,
									   m_gBuffer_DiffuseAOHandle.type,
									   frameBufferSize);

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

			if (m_BRDFTextureHandle.id == 0)
			{
				GenerateGLTexture_Empty(m_BRDFTextureHandle.id, m_BRDFTextureSize, false,
										m_BRDFTextureHandle.internalFormat, m_BRDFTextureHandle.format, m_BRDFTextureHandle.type);
				GenerateBRDFLUT(gameContext, m_BRDFTextureHandle.id, m_BRDFTextureSize);
			}

			CheckGLErrorMessages();
		}

		void GLRenderer::PostInitialize(const GameContext& gameContext)
		{
			GenerateGBuffer(gameContext);

			//std::string fontFilePath = RESOURCE_LOCATION + "fonts/SourceSansVariable-Roman.ttf";
			//std::string fontFilePath = RESOURCE_LOCATION + "fonts/bahnschrift.ttf";

			std::string ubuntuFilePath = RESOURCE_LOCATION + "fonts/UbuntuCondensed-Regular.ttf";
			LoadFont(gameContext, &m_FntUbuntuCondensed, ubuntuFilePath, 16);

			std::string sourceCodeProFilePath = RESOURCE_LOCATION + "fonts/SourceCodePro-regular.ttf";
			LoadFont(gameContext, &m_FntSourceCodePro, sourceCodeProFilePath, 10);


			GLFWWindowWrapper* castedWindow = dynamic_cast<GLFWWindowWrapper*>(gameContext.window);
			if (castedWindow == nullptr)
			{
				Logger::LogError("GLRenderer::PostInitialize expects gameContext.window to be of type GLFWWindowWrapper!");
				return;
			}

			ImGui_ImplGlfwGL3_Init(castedWindow->GetWindow());
			CheckGLErrorMessages();

			m_PhysicsDebugDrawer = new GLPhysicsDebugDraw(gameContext);
			m_PhysicsDebugDrawer->Initialize();
		}

		void GLRenderer::Destroy()
		{
			CheckGLErrorMessages();

			glDeleteVertexArrays(1, &m_TextQuadVAO);
			glDeleteBuffers(1, &m_TextQuadVBO);

			for (BitmapFont* font : m_Fonts)
			{
				SafeDelete(font);
			}
			m_Fonts.clear();

			ImGui_ImplGlfwGL3_Shutdown();
			ImGui::DestroyContext();

			CheckGLErrorMessages();

			if (m_1x1_NDC_QuadVertexBufferData.pDataStart)
			{
				m_1x1_NDC_QuadVertexBufferData.Destroy();
			}

			if (m_1x1_NDC_Quad)
			{
				DestroyRenderObject(m_1x1_NDC_Quad->renderID);
				m_1x1_NDC_Quad = nullptr;
			}

			for (GameObject* obj : m_PersistentObjects)
			{
				if (obj->GetRenderID() != InvalidRenderID)
				{
					DestroyRenderObject(obj->GetRenderID());
				}
				SafeDelete(obj);
			}
			m_PersistentObjects.clear();

			DestroyRenderObject(m_SpriteQuadRenderID);
			
			DestroyRenderObject(m_GBufferQuadRenderID);

			u32 activeRenderObjectCount = GetActiveRenderObjectCount();
			if (activeRenderObjectCount > 0)
			{
				Logger::LogError("=====================================================");
				Logger::LogError(std::to_string(activeRenderObjectCount) + " render objects were not destroyed before GL render:");

				for (size_t i = 0; i < m_RenderObjects.size(); ++i)
				{
					if (m_RenderObjects[i])
					{
						Logger::LogError("render object with material name: " + m_RenderObjects[i]->materialName);
						//Logger::LogError(m_RenderObjects[i]->gameObject->GetName());
						DestroyRenderObject(m_RenderObjects[i]->renderID);
					}
				}
				Logger::LogError("=====================================================");
			}
			m_RenderObjects.clear();
			CheckGLErrorMessages();

			m_SkyBoxMesh = nullptr;

			if (m_PhysicsDebugDrawer)
			{
				m_PhysicsDebugDrawer->Destroy();
				SafeDelete(m_PhysicsDebugDrawer);
			}

			m_gBufferQuadVertexBufferData.Destroy();
			m_SpriteQuadVertexBufferData.Destroy();

			// TODO: Is this wanted here?
			glfwTerminate();
		}

		MaterialID GLRenderer::InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo)
		{
			CheckGLErrorMessages();

			MaterialID matID = GetNextAvailableMaterialID();
			m_Materials.insert(std::pair<MaterialID, GLMaterial>(matID, {}));
			GLMaterial& mat = m_Materials.at(matID);
			mat.material = {};
			mat.material.name = createInfo->name;

			if (mat.material.name.empty())
			{
				Logger::LogWarning("Material doesn't have a name!");
			}

			if (!GetShaderID(createInfo->shaderName, mat.material.shaderID))
			{
				if (createInfo->shaderName.empty())
				{
					Logger::LogError("MaterialCreateInfo::shaderName must be filled in!");
				}
				else
				{
					Logger::LogError("Material's shader name couldn't be found! shader name: " + createInfo->shaderName);
				}
			}
			
			GLShader& shader = m_Shaders[mat.material.shaderID];

			glUseProgram(shader.program);
			CheckGLErrorMessages();

			// TODO: Is this really needed? (do things dynamically instead?)
			UniformInfo uniformInfo[] = {
				{ "model", 							&mat.uniformIDs.model },
				{ "modelInvTranspose", 				&mat.uniformIDs.modelInvTranspose },
				{ "modelViewProjection",			&mat.uniformIDs.modelViewProjection },
				{ "colorMultiplier", 				&mat.uniformIDs.colorMultiplier },
				{ "view", 							&mat.uniformIDs.view },
				{ "viewInv", 						&mat.uniformIDs.viewInv },
				{ "viewProjection", 				&mat.uniformIDs.viewProjection },
				{ "projection", 					&mat.uniformIDs.projection },
				{ "camPos", 						&mat.uniformIDs.camPos },
				{ "enableDiffuseSampler", 			&mat.uniformIDs.enableDiffuseTexture },
				{ "enableNormalSampler", 			&mat.uniformIDs.enableNormalTexture },
				{ "enableCubemapSampler", 			&mat.uniformIDs.enableCubemapTexture },
				{ "enableAlbedoSampler", 			&mat.uniformIDs.enableAlbedoSampler },
				{ "constAlbedo", 					&mat.uniformIDs.constAlbedo },
				{ "enableMetallicSampler", 			&mat.uniformIDs.enableMetallicSampler },
				{ "constMetallic", 					&mat.uniformIDs.constMetallic },
				{ "enableRoughnessSampler", 		&mat.uniformIDs.enableRoughnessSampler },
				{ "constRoughness", 				&mat.uniformIDs.constRoughness },
				{ "enableAOSampler",				&mat.uniformIDs.enableAOSampler },
				{ "constAO",						&mat.uniformIDs.constAO },
				{ "hdrEquirectangularSampler",		&mat.uniformIDs.hdrEquirectangularSampler },
				{ "enableIrradianceSampler",		&mat.uniformIDs.enableIrradianceSampler },
				{ "transformMat",					&mat.uniformIDs.transformMat },
				{ "texSize",					&mat.uniformIDs.texSize },
			};

			const u32 uniformCount = sizeof(uniformInfo) / sizeof(uniformInfo[0]);

			for (size_t i = 0; i < uniformCount; ++i)
			{
				if (shader.shader.dynamicBufferUniforms.HasUniform(uniformInfo[i].name) ||
					shader.shader.constantBufferUniforms.HasUniform(uniformInfo[i].name))
				{
					*uniformInfo[i].id = glGetUniformLocation(shader.program, uniformInfo[i].name);
					if (*uniformInfo[i].id == -1)
					{
						Logger::LogWarning(std::string(uniformInfo[i].name) + " was not found for material " + createInfo->name + " (shader " + createInfo->shaderName + ")");
					}
				}
			}

			CheckGLErrorMessages();

			mat.material.diffuseTexturePath = createInfo->diffuseTexturePath;
			mat.material.generateDiffuseSampler = createInfo->generateDiffuseSampler;
			mat.material.enableDiffuseSampler = createInfo->enableDiffuseSampler;

			mat.material.normalTexturePath = createInfo->normalTexturePath;
			mat.material.generateNormalSampler = createInfo->generateNormalSampler;
			mat.material.enableNormalSampler = createInfo->enableNormalSampler;

			mat.material.frameBuffers = createInfo->frameBuffers;

			mat.material.enableCubemapTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;

			mat.material.enableCubemapSampler = createInfo->enableCubemapSampler;
			mat.material.generateCubemapSampler = createInfo->generateCubemapSampler || createInfo->generateHDRCubemapSampler;
			mat.material.cubemapSamplerSize = createInfo->generatedCubemapSize;
			mat.material.cubeMapFilePaths = createInfo->cubeMapFilePaths;

			assert(mat.material.cubemapSamplerSize.x <= MAX_TEXTURE_DIM);
			assert(mat.material.cubemapSamplerSize.y <= MAX_TEXTURE_DIM);

			mat.material.constAlbedo = glm::vec4(createInfo->constAlbedo, 0);
			mat.material.generateAlbedoSampler = createInfo->generateAlbedoSampler;
			mat.material.albedoTexturePath = createInfo->albedoTexturePath;
			mat.material.enableAlbedoSampler = createInfo->enableAlbedoSampler;

			mat.material.constMetallic = createInfo->constMetallic;
			mat.material.generateMetallicSampler = createInfo->generateMetallicSampler;
			mat.material.metallicTexturePath = createInfo->metallicTexturePath;
			mat.material.enableMetallicSampler = createInfo->enableMetallicSampler;

			mat.material.constRoughness = createInfo->constRoughness;
			mat.material.generateRoughnessSampler = createInfo->generateRoughnessSampler;
			mat.material.roughnessTexturePath = createInfo->roughnessTexturePath;
			mat.material.enableRoughnessSampler = createInfo->enableRoughnessSampler;

			mat.material.constAO = createInfo->constAO;
			mat.material.generateAOSampler = createInfo->generateAOSampler;
			mat.material.aoTexturePath = createInfo->aoTexturePath;
			mat.material.enableAOSampler = createInfo->enableAOSampler;

			mat.material.enableHDREquirectangularSampler = createInfo->enableHDREquirectangularSampler;
			mat.material.generateHDREquirectangularSampler = createInfo->generateHDREquirectangularSampler;
			mat.material.hdrEquirectangularTexturePath = createInfo->hdrEquirectangularTexturePath;

			mat.material.generateHDRCubemapSampler = createInfo->generateHDRCubemapSampler;

			mat.material.enableIrradianceSampler = createInfo->enableIrradianceSampler;
			mat.material.generateIrradianceSampler = createInfo->generateIrradianceSampler;
			mat.material.irradianceSamplerSize = createInfo->generatedIrradianceCubemapSize;







			// TODO: FIXME!!!
			if (m_SkyBoxMesh &&
				m_Shaders[mat.material.shaderID].shader.needPrefilteredMap)
			{
				MaterialID skyboxMaterialID = m_SkyBoxMesh->GetMeshComponent()->GetMaterialID();

				mat.irradianceSamplerID = m_Materials[skyboxMaterialID].irradianceSamplerID;
				mat.prefilteredMapSamplerID = m_Materials[skyboxMaterialID].prefilteredMapSamplerID;
			}





			mat.material.environmentMapPath = createInfo->environmentMapPath;

			mat.material.generateReflectionProbeMaps = createInfo->generateReflectionProbeMaps;

			mat.material.colorMultiplier = createInfo->colorMultiplier;

			mat.material.engineMaterial = createInfo->engineMaterial;

			if (shader.shader.needIrradianceSampler)
			{
				if (mat.irradianceSamplerID == InvalidID)
				{
					if (m_Materials.find(createInfo->irradianceSamplerMatID) == m_Materials.end())
					{
						//Logger::LogError("material being initialized in GLRenderer::InitializeMaterial attempting to use invalid irradianceSamplerMatID: " + std::to_string(createInfo->irradianceSamplerMatID));
						mat.irradianceSamplerID = InvalidID;
					}
					else
					{
						mat.irradianceSamplerID = m_Materials[createInfo->irradianceSamplerMatID].irradianceSamplerID;
					}
				}
			}
			if (shader.shader.needBRDFLUT)
			{
				if (m_BRDFTextureHandle.id == 0)
				{
					GenerateGLTexture_Empty(m_BRDFTextureHandle.id, m_BRDFTextureSize, false, m_BRDFTextureHandle.internalFormat, m_BRDFTextureHandle.format, m_BRDFTextureHandle.type);
					GenerateBRDFLUT(gameContext, m_BRDFTextureHandle.id, m_BRDFTextureSize);
				}
				mat.brdfLUTSamplerID = m_BRDFTextureHandle.id;
			}
			if (shader.shader.needPrefilteredMap)
			{
				if (mat.prefilteredMapSamplerID == InvalidID)
				{
					if (m_Materials.find(createInfo->prefilterMapSamplerMatID) == m_Materials.end())
					{
						//Logger::LogError("material being initialized in GLRenderer::InitializeMaterial attempting to use invalid prefilterMapSamplerMatID: " + std::to_string(createInfo->prefilterMapSamplerMatID));
						mat.prefilteredMapSamplerID = InvalidID;
					}
					else
					{
						mat.prefilteredMapSamplerID = m_Materials[createInfo->prefilterMapSamplerMatID].prefilteredMapSamplerID;
					}
				}
			}

			mat.material.enablePrefilteredMap = createInfo->enablePrefilteredMap;
			mat.material.generatePrefilteredMap = createInfo->generatePrefilteredMap;
			mat.material.prefilteredMapSize = createInfo->generatedPrefilteredCubemapSize;

			mat.material.enableBRDFLUT = createInfo->enableBRDFLUT;

			struct SamplerCreateInfo
			{
				bool needed;
				bool create;
				u32* id;
				std::string filepath;
				std::string textureName;
				bool flipVertically;
				std::function<bool(u32&, const std::string&, bool, bool)> createFunction;
			};

			// Samplers that need to be loaded from file
			SamplerCreateInfo samplerCreateInfos[] =
			{
				{ shader.shader.needAlbedoSampler, mat.material.generateAlbedoSampler, &mat.albedoSamplerID, 
				createInfo->albedoTexturePath, "albedoSampler", false, GenerateGLTexture },
				{ shader.shader.needMetallicSampler, mat.material.generateMetallicSampler, &mat.metallicSamplerID, 
				createInfo->metallicTexturePath, "metallicSampler", false,GenerateGLTexture },
				{ shader.shader.needRoughnessSampler, mat.material.generateRoughnessSampler, &mat.roughnessSamplerID, 
				createInfo->roughnessTexturePath, "roughnessSampler" ,false, GenerateGLTexture },
				{ shader.shader.needAOSampler, mat.material.generateAOSampler, &mat.aoSamplerID, 
				createInfo->aoTexturePath, "aoSampler", false,GenerateGLTexture },
				{ shader.shader.needDiffuseSampler, mat.material.generateDiffuseSampler, &mat.diffuseSamplerID, 
				createInfo->diffuseTexturePath, "diffuseSampler", false,GenerateGLTexture },
				{ shader.shader.needNormalSampler, mat.material.generateNormalSampler, &mat.normalSamplerID, 
				createInfo->normalTexturePath, "normalSampler",false, GenerateGLTexture },
				{ shader.shader.needHDREquirectangularSampler, mat.material.generateHDREquirectangularSampler, &mat.hdrTextureID, 
				createInfo->hdrEquirectangularTexturePath, "hdrEquirectangularSampler", true, GenerateHDRGLTexture },
			};

			i32 binding = 0;
			for (SamplerCreateInfo& samplerCreateInfo : samplerCreateInfos)
			{
				if (samplerCreateInfo.needed)
				{
					if (samplerCreateInfo.create)
					{
						if (samplerCreateInfo.filepath.empty())
						{
							Logger::LogError("Empty file path specified in SamplerCreateInfo for texture " + samplerCreateInfo.textureName + " in material " + mat.material.name);
						}
						else
						{
							if (!GetLoadedTexture(samplerCreateInfo.filepath, *samplerCreateInfo.id))
							{
								// Texture hasn't been loaded yet, load it now
								samplerCreateInfo.createFunction(*samplerCreateInfo.id, samplerCreateInfo.filepath, samplerCreateInfo.flipVertically, false);
								m_LoadedTextures.insert({ samplerCreateInfo.filepath, *samplerCreateInfo.id });
							}

							i32 uniformLocation = glGetUniformLocation(shader.program, samplerCreateInfo.textureName.c_str());
							CheckGLErrorMessages();
							if (uniformLocation == -1)
							{
								Logger::LogWarning(samplerCreateInfo.textureName + " was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
							}
							else
							{
								glUniform1i(uniformLocation, binding);
								CheckGLErrorMessages();
							}
						}
					}
					// Always increment the binding, even when not binding anything
					++binding;
				}
			}

			for (auto& frameBufferPair : createInfo->frameBuffers)
			{
				const char* frameBufferName = frameBufferPair.first.c_str();
				i32 positionLocation = glGetUniformLocation(shader.program, frameBufferName);
				CheckGLErrorMessages();
				if (positionLocation == -1)
				{
					Logger::LogWarning(frameBufferPair.first + " was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
				}
				else
				{
					glUniform1i(positionLocation, binding);
					CheckGLErrorMessages();
				}
				++binding;
			}


			if (createInfo->generateCubemapSampler)
			{
				GLCubemapCreateInfo cubemapCreateInfo = {};
				cubemapCreateInfo.program = shader.program;
				cubemapCreateInfo.textureID = &mat.cubemapSamplerID;
				cubemapCreateInfo.HDR = false;
				cubemapCreateInfo.generateMipmaps = false;
				cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				cubemapCreateInfo.filePaths = mat.material.cubeMapFilePaths;

				if (createInfo->cubeMapFilePaths[0].empty())
				{
					cubemapCreateInfo.textureSize = createInfo->generatedCubemapSize;
					GenerateGLCubemap(cubemapCreateInfo);
				}
				else
				{
					// Load from file
					GenerateGLCubemap(cubemapCreateInfo);

					i32 uniformLocation = glGetUniformLocation(shader.program, "cubemapSampler");
					CheckGLErrorMessages();
					if (uniformLocation == -1)
					{
						Logger::LogWarning("cubemapSampler was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
					}
					else
					{
						glUniform1i(uniformLocation, binding);
					}
					CheckGLErrorMessages();
					++binding;
				}
			}

			if (mat.material.generateReflectionProbeMaps)
			{
				mat.cubemapSamplerGBuffersIDs = {
					{ 0, "positionMetallicFrameBufferSampler", GL_RGBA16F, GL_RGBA },
					{ 0, "normalRoughnessFrameBufferSampler", GL_RGBA16F, GL_RGBA },
					{ 0, "albedoAOFrameBufferSampler", GL_RGBA, GL_RGBA },
				};

				GLCubemapCreateInfo cubemapCreateInfo = {};
				cubemapCreateInfo.program = shader.program;
				cubemapCreateInfo.textureID = &mat.cubemapSamplerID;
				cubemapCreateInfo.textureGBufferIDs = &mat.cubemapSamplerGBuffersIDs;
				cubemapCreateInfo.depthTextureID = &mat.cubemapDepthSamplerID;
				cubemapCreateInfo.HDR = true;
				cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				cubemapCreateInfo.generateMipmaps = false;
				cubemapCreateInfo.textureSize = createInfo->generatedCubemapSize;
				cubemapCreateInfo.generateDepthBuffers = createInfo->generateCubemapDepthBuffers;
			
				GenerateGLCubemap(cubemapCreateInfo);
			}
			else if (createInfo->generateHDRCubemapSampler)
			{
				GLCubemapCreateInfo cubemapCreateInfo = {};
				cubemapCreateInfo.program = shader.program;
				cubemapCreateInfo.textureID = &mat.cubemapSamplerID;
				cubemapCreateInfo.textureGBufferIDs = &mat.cubemapSamplerGBuffersIDs;
				cubemapCreateInfo.depthTextureID = &mat.cubemapDepthSamplerID;
				cubemapCreateInfo.HDR = true;
				cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				cubemapCreateInfo.generateMipmaps = false;
				cubemapCreateInfo.textureSize = createInfo->generatedCubemapSize;
				cubemapCreateInfo.generateDepthBuffers = createInfo->generateCubemapDepthBuffers;

				GenerateGLCubemap(cubemapCreateInfo);
			}

			if (shader.shader.needCubemapSampler)
			{
				// TODO: Save location for binding later?
				i32 uniformLocation = glGetUniformLocation(shader.program, "cubemapSampler");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("cubemapSampler was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (shader.shader.needBRDFLUT)
			{
				i32 uniformLocation = glGetUniformLocation(shader.program, "brdfLUT");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("brdfLUT was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (mat.material.generateIrradianceSampler)
			{
				GLCubemapCreateInfo cubemapCreateInfo = {};
				cubemapCreateInfo.program = shader.program;
				cubemapCreateInfo.textureID = &mat.irradianceSamplerID;
				cubemapCreateInfo.HDR = true;
				cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				cubemapCreateInfo.generateMipmaps = false;
				cubemapCreateInfo.textureSize = createInfo->generatedIrradianceCubemapSize;

				GenerateGLCubemap(cubemapCreateInfo);
			}

			if (shader.shader.needIrradianceSampler)
			{
				i32 uniformLocation = glGetUniformLocation(shader.program, "irradianceSampler");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("irradianceSampler was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (mat.material.generatePrefilteredMap)
			{
				GLCubemapCreateInfo cubemapCreateInfo = {};
				cubemapCreateInfo.program = shader.program;
				cubemapCreateInfo.textureID = &mat.prefilteredMapSamplerID;
				cubemapCreateInfo.HDR = true;
				cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				cubemapCreateInfo.generateMipmaps = true;
				cubemapCreateInfo.textureSize = createInfo->generatedPrefilteredCubemapSize;

				GenerateGLCubemap(cubemapCreateInfo);
			}

			if (shader.shader.needPrefilteredMap)
			{
				i32 uniformLocation = glGetUniformLocation(shader.program, "prefilterMap");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("prefilterMap was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			glUseProgram(0);

			return matID;
		}

		u32 GLRenderer::InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			const RenderID renderID = GetNextAvailableRenderID();

			assert(createInfo->materialID != InvalidMaterialID);

			GLRenderObject* renderObject = new GLRenderObject();
			renderObject->renderID = renderID;
			InsertNewRenderObject(renderObject);

			renderObject->materialID = createInfo->materialID;
			renderObject->vertexBufferData = createInfo->vertexBufferData;
			renderObject->indices = createInfo->indices;
			renderObject->gameObject = createInfo->gameObject;
			renderObject->cullFace = CullFaceToGLCullFace(createInfo->cullFace);
			renderObject->enableCulling = createInfo->enableCulling ? GL_TRUE : GL_FALSE;
			renderObject->depthTestReadFunc = DepthTestFuncToGlenum(createInfo->depthTestReadFunc);
			renderObject->depthWriteEnable = BoolToGLBoolean(createInfo->depthWriteEnable);
			renderObject->editorObject = createInfo->editorObject;

			if (renderObject->materialID == InvalidMaterialID)
			{
				Logger::LogError("Render object's materialID has not been set in its createInfo!");

				// TODO: Use INVALID material here (Bright pink)
				// Hopefully the first material works out okay! Should be better than crashing
				renderObject->materialID = 0;

				if (!m_Materials.empty())
				{
					renderObject->materialName = m_Materials[renderObject->materialID].material.name;
				}
			}
			else
			{
				renderObject->materialName = m_Materials[renderObject->materialID].material.name;

				if (renderObject->materialName.empty())
				{
					Logger::LogWarning("Render object created with empty material name!");
				}
			}

			if (renderObject->gameObject->GetName().empty())
			{
				Logger::LogWarning("Render object created with empty name!");
			}

			if (m_Materials.empty())
			{
				Logger::LogError("Render object is being created before any materials have been created!");
			}

			if (m_Materials.find(renderObject->materialID) == m_Materials.end())
			{
				Logger::LogError("Uninitialized material with MaterialID " + std::to_string(renderObject->materialID));
				return renderID;
			}

			GLMaterial& material = m_Materials[renderObject->materialID];
			GLShader& shader = m_Shaders[material.material.shaderID];

			glUseProgram(shader.program);
			CheckGLErrorMessages();

			if (createInfo->vertexBufferData)
			{
				glGenVertexArrays(1, &renderObject->VAO);
				glBindVertexArray(renderObject->VAO);
				CheckGLErrorMessages();

				glGenBuffers(1, &renderObject->VBO);
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)createInfo->vertexBufferData->BufferSize, createInfo->vertexBufferData->pDataStart, GL_STATIC_DRAW);
				CheckGLErrorMessages();
			}

			if (createInfo->indices != nullptr)
			{
				renderObject->indexed = true;

				glGenBuffers(1, &renderObject->IBO);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderObject->IBO);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(createInfo->indices[0]) * createInfo->indices->size()), createInfo->indices->data(), GL_STATIC_DRAW);
			}

			glBindVertexArray(0);
			glUseProgram(0);

			return renderID;
		}

		void GLRenderer::PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			GLMaterial& material = m_Materials[renderObject->materialID];

			if (material.material.generateReflectionProbeMaps)
			{
				Logger::LogInfo("Capturing reflection probe");
				CaptureSceneToCubemap(gameContext, renderID);
				GenerateIrradianceSamplerFromCubemap(gameContext, renderObject->materialID);
				GeneratePrefilteredMapFromCubemap(gameContext, renderObject->materialID);
				Logger::LogInfo("Done");

				// Capture again to use just generated irradiance + prefilter sampler (TODO: Remove soon)
				Logger::LogInfo("Capturing reflection probe");
				CaptureSceneToCubemap(gameContext, renderID);
				GenerateIrradianceSamplerFromCubemap(gameContext, renderObject->materialID);
				GeneratePrefilteredMapFromCubemap(gameContext, renderObject->materialID);
				Logger::LogInfo("Done");

				// Display captured cubemap as skybox
				//m_Materials[m_RenderObjects[cubemapID]->materialID].cubemapSamplerID =
				//	m_Materials[m_RenderObjects[renderID]->materialID].cubemapSamplerID;
			}
			else if (material.material.generateIrradianceSampler)
			{
				GenerateCubemapFromHDREquirectangular(gameContext, renderObject->materialID, material.material.environmentMapPath);
				GenerateIrradianceSamplerFromCubemap(gameContext, renderObject->materialID);
				GeneratePrefilteredMapFromCubemap(gameContext, renderObject->materialID);
			}
		}

		void GLRenderer::ClearRenderObjects()
		{
			for (GLRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject)
				{
					DestroyRenderObject(renderObject->renderID, renderObject);
				}
			}
			m_RenderObjects.clear();
		}

		void GLRenderer::ClearMaterials()
		{
			auto iter = m_Materials.begin();
			while (iter != m_Materials.end())
			{
				if (!iter->second.material.engineMaterial)
				{
					iter = m_Materials.erase(iter);
				}
				else
				{
					++iter;
				}
			}
		}

		void GLRenderer::GenerateCubemapFromHDREquirectangular(const GameContext& gameContext, MaterialID cubemapMaterialID, const std::string& environmentMapPath)
		{
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);

			MaterialID equirectangularToCubeMatID = InvalidMaterialID;
			if (!GetMaterialID("Equirectangular to Cube", equirectangularToCubeMatID))
			{
				MaterialCreateInfo equirectangularToCubeMatCreateInfo = {};
				equirectangularToCubeMatCreateInfo.name = "Equirectangular to Cube";
				equirectangularToCubeMatCreateInfo.shaderName = "equirectangular_to_cube";
				equirectangularToCubeMatCreateInfo.enableHDREquirectangularSampler = true;
				equirectangularToCubeMatCreateInfo.generateHDREquirectangularSampler = true;
				// TODO: Make cyclable at runtime
				equirectangularToCubeMatCreateInfo.hdrEquirectangularTexturePath = environmentMapPath;
				equirectangularToCubeMatID = InitializeMaterial(gameContext, &equirectangularToCubeMatCreateInfo);
			}

			GLMaterial& equirectangularToCubemapMaterial = m_Materials[equirectangularToCubeMatID];
			GLShader& equirectangularToCubemapShader = m_Shaders[equirectangularToCubemapMaterial.material.shaderID];

			// TODO: Handle no skybox being set gracefully
			GLRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetRenderID());
			GLMaterial& skyboxGLMaterial = m_Materials[skyboxRenderObject->materialID];

			glUseProgram(equirectangularToCubemapShader.program);
			CheckGLErrorMessages();

			// TODO: Store what location this texture is at (might not be 0)
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_Materials[equirectangularToCubeMatID].hdrTextureID);
			CheckGLErrorMessages();

			// Update object's uniforms under this shader's program
			glUniformMatrix4fv(equirectangularToCubemapMaterial.uniformIDs.model, 1, false, 
				&m_SkyBoxMesh->GetTransform()->GetWorldTransform()[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(equirectangularToCubemapMaterial.uniformIDs.projection, 1, false, 
				&m_CaptureProjection[0][0]);
			CheckGLErrorMessages();

			glm::vec2 cubemapSize = skyboxGLMaterial.material.cubemapSamplerSize;

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			CheckGLErrorMessages();
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize.x, cubemapSize.y);
			CheckGLErrorMessages();

			glViewport(0, 0, cubemapSize.x, cubemapSize.y);
			CheckGLErrorMessages();

			glBindVertexArray(skyboxRenderObject->VAO);
			CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, skyboxRenderObject->VBO);
			CheckGLErrorMessages();

			glCullFace(skyboxRenderObject->cullFace);
			CheckGLErrorMessages();

			if (skyboxRenderObject->enableCulling)
			{
				glEnable(GL_CULL_FACE);
			}
			else
			{
				glDisable(GL_CULL_FACE);
			}

			glDepthFunc(skyboxRenderObject->depthTestReadFunc);
			CheckGLErrorMessages();

			for (u32 i = 0; i < 6; ++i)
			{
				glUniformMatrix4fv(equirectangularToCubemapMaterial.uniformIDs.view, 1, false, 
					&m_CaptureViews[i][0][0]);
				CheckGLErrorMessages();

				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[cubemapMaterialID].cubemapSamplerID, 0);
				CheckGLErrorMessages();

				glDepthMask(GL_TRUE);

				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				CheckGLErrorMessages();

				glDepthMask(skyboxRenderObject->depthWriteEnable);
				CheckGLErrorMessages();

				glDrawArrays(skyboxRenderObject->topology, 0, 
					(GLsizei)skyboxRenderObject->vertexBufferData->VertexCount);
				CheckGLErrorMessages();
			}

			// Generate mip maps for generated cubemap
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[cubemapMaterialID].cubemapSamplerID);
			glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

			glUseProgram(0);
			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
		}

		void GLRenderer::GeneratePrefilteredMapFromCubemap(const GameContext& gameContext, MaterialID cubemapMaterialID)
		{
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);

			MaterialID prefilterMatID = InvalidMaterialID;
			if (!GetMaterialID("Prefilter", prefilterMatID))
			{
				MaterialCreateInfo prefilterMaterialCreateInfo = {};
				prefilterMaterialCreateInfo.name = "Prefilter";
				prefilterMaterialCreateInfo.shaderName = "prefilter";
				prefilterMatID = InitializeMaterial(gameContext, &prefilterMaterialCreateInfo);
			}

			GLMaterial& prefilterMat = m_Materials[prefilterMatID];
			GLShader& prefilterShader = m_Shaders[prefilterMat.material.shaderID];

			GLRenderObject* skybox = GetRenderObject(m_SkyBoxMesh->GetRenderID());

			glUseProgram(prefilterShader.program);
			CheckGLErrorMessages();

			glUniformMatrix4fv(prefilterMat.uniformIDs.model, 1, false, 
				&m_SkyBoxMesh->GetTransform()->GetWorldTransform()[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(prefilterMat.uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
			CheckGLErrorMessages();

			glActiveTexture(GL_TEXTURE0); // TODO: Remove constant
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[cubemapMaterialID].cubemapSamplerID);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();

			glBindVertexArray(skybox->VAO);
			CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, skybox->VBO);
			CheckGLErrorMessages();

			if (skybox->enableCulling)
			{
				glEnable(GL_CULL_FACE);
			}
			else
			{
				glDisable(GL_CULL_FACE);
			}

			glCullFace(skybox->cullFace);
			CheckGLErrorMessages();

			glDepthFunc(skybox->depthTestReadFunc);
			CheckGLErrorMessages();

			glDepthMask(skybox->depthWriteEnable);
			CheckGLErrorMessages();

			u32 maxMipLevels = 5;
			for (u32 mip = 0; mip < maxMipLevels; ++mip)
			{
				u32 mipWidth = (u32)(m_Materials[cubemapMaterialID].material.prefilteredMapSize.x * pow(0.5f, mip));
				u32 mipHeight = (u32)(m_Materials[cubemapMaterialID].material.prefilteredMapSize.y * pow(0.5f, mip));
				assert(mipWidth <= Renderer::MAX_TEXTURE_DIM);
				assert(mipHeight <= Renderer::MAX_TEXTURE_DIM);

				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
				CheckGLErrorMessages();

				glViewport(0, 0, mipWidth, mipHeight);
				CheckGLErrorMessages();

				real roughness = (real)mip / (real(maxMipLevels - 1));
				i32 roughnessUniformLocation = glGetUniformLocation(prefilterShader.program, "roughness");
				glUniform1f(roughnessUniformLocation, roughness);
				CheckGLErrorMessages();
				for (u32 i = 0; i < 6; ++i)
				{
					glUniformMatrix4fv(prefilterMat.uniformIDs.view, 1, false, &m_CaptureViews[i][0][0]);
					CheckGLErrorMessages();

					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[cubemapMaterialID].prefilteredMapSamplerID, mip);
					CheckGLErrorMessages();
					
					glDrawArrays(skybox->topology, 0, (GLsizei)skybox->vertexBufferData->VertexCount);
					CheckGLErrorMessages();
				}
			}

			// TODO: Make this a togglable bool param for the shader (or roughness param)
			// Visualize prefiltered map as skybox:
			//m_Materials[renderObject->materialID].cubemapSamplerID = m_Materials[renderObject->materialID].prefilteredMapSamplerID;

			glUseProgram(0);
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
		}

		void GLRenderer::GenerateBRDFLUT(const GameContext& gameContext, u32 brdfLUTTextureID, glm::vec2 BRDFLUTSize)
		{
			if (m_1x1_NDC_Quad)
			{
				// Don't re-create material or object
				return;
			}

			GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);

			MaterialCreateInfo brdfMaterialCreateInfo = {};
			brdfMaterialCreateInfo.name = "BRDF";
			brdfMaterialCreateInfo.shaderName = "brdf";
			brdfMaterialCreateInfo.engineMaterial = true;
			MaterialID brdfMatID = InitializeMaterial(gameContext, &brdfMaterialCreateInfo);

			if (m_1x1_NDC_Quad == nullptr)
			{
				VertexBufferData::CreateInfo quadVertexBufferDataCreateInfo = {};
				quadVertexBufferDataCreateInfo.positions_3D = {
					{ -1.0f,  1.0f, 0.0f },
					{ -1.0f, -1.0f, 0.0f },
					{ 1.0f,  1.0f, 0.0f },
					{ 1.0f, -1.0f, 0.0f },
				};
				quadVertexBufferDataCreateInfo.texCoords_UV = {
					{ 0.0f, 1.0f },
					{ 0.0f, 0.0f },
					{ 1.0f, 1.0f },
					{ 1.0f, 0.0f },
				};
				quadVertexBufferDataCreateInfo.attributes = 
					(u32)VertexAttribute::POSITION | 
					(u32)VertexAttribute::UV;

				m_1x1_NDC_QuadVertexBufferData = {};
				m_1x1_NDC_QuadVertexBufferData.Initialize(&quadVertexBufferDataCreateInfo);

				m_1x1_NDC_QuadTransform = Transform::Identity();


				GameObject* oneByOneQuadGameObject = new GameObject("1x1 Quad", GameObjectType::NONE);
				m_PersistentObjects.push_back(oneByOneQuadGameObject);
				// Don't render this normally, we'll draw it manually
				oneByOneQuadGameObject->SetVisible(false);

				RenderObjectCreateInfo quadCreateInfo = {};
				quadCreateInfo.materialID = brdfMatID;
				quadCreateInfo.vertexBufferData = &m_1x1_NDC_QuadVertexBufferData;
				quadCreateInfo.gameObject = oneByOneQuadGameObject;
				quadCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
				quadCreateInfo.depthWriteEnable = false;
				quadCreateInfo.visibleInSceneExplorer = false;

				RenderID quadRenderID = InitializeRenderObject(gameContext, &quadCreateInfo);
				m_1x1_NDC_Quad = GetRenderObject(quadRenderID);

				if (!m_1x1_NDC_Quad)
				{
					Logger::LogError("Failed to create 1x1 NDC quad!");
				}
				else
				{
					SetTopologyMode(quadRenderID, TopologyMode::TRIANGLE_STRIP);
					m_1x1_NDC_QuadVertexBufferData.DescribeShaderVariables(gameContext.renderer, quadRenderID);
				}
			}

			glUseProgram(m_Shaders[m_Materials[brdfMatID].material.shaderID].program);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			CheckGLErrorMessages();

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTextureID, 0);
			CheckGLErrorMessages();

			glBindVertexArray(m_1x1_NDC_Quad->VAO);
			CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, m_1x1_NDC_Quad->VBO);
			CheckGLErrorMessages();

			glViewport(0, 0, BRDFLUTSize.x, BRDFLUTSize.y);
			CheckGLErrorMessages();

			if (m_1x1_NDC_Quad->enableCulling)
			{
				glEnable(GL_CULL_FACE);
			}
			else
			{
				glDisable(GL_CULL_FACE);
			}

			glCullFace(m_1x1_NDC_Quad->cullFace);
			CheckGLErrorMessages();

			glDepthFunc(m_1x1_NDC_Quad->depthTestReadFunc);
			CheckGLErrorMessages();

			glDepthMask(m_1x1_NDC_Quad->depthWriteEnable);
			CheckGLErrorMessages();

			// Render quad
			glDrawArrays(m_1x1_NDC_Quad->topology, 0, (GLsizei)m_1x1_NDC_Quad->vertexBufferData->VertexCount);
			CheckGLErrorMessages();
			
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glUseProgram(last_program);
			glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
		}

		void GLRenderer::GenerateIrradianceSamplerFromCubemap(const GameContext& gameContext, MaterialID cubemapMaterialID)
		{
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);

			MaterialID irrandianceMatID = InvalidMaterialID;
			if (!GetMaterialID("Irradiance", irrandianceMatID))
			{
				MaterialCreateInfo irrandianceMatCreateInfo = {};
				irrandianceMatCreateInfo.name = "Irradiance";
				irrandianceMatCreateInfo.shaderName = "irradiance";
				irrandianceMatCreateInfo.enableCubemapSampler = true;
				irrandianceMatCreateInfo.engineMaterial = true;
				irrandianceMatID = InitializeMaterial(gameContext, &irrandianceMatCreateInfo);
			}

			GLMaterial& irradianceMat = m_Materials[irrandianceMatID];
			GLShader& shader = m_Shaders[irradianceMat.material.shaderID];

			GLRenderObject* skybox = GetRenderObject(m_SkyBoxMesh->GetRenderID());

			glUseProgram(shader.program);
			CheckGLErrorMessages();

			glUniformMatrix4fv(irradianceMat.uniformIDs.model, 1, false, 
				&m_SkyBoxMesh->GetTransform()->GetWorldTransform()[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(irradianceMat.uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
			CheckGLErrorMessages();

			glActiveTexture(GL_TEXTURE0); // TODO: Remove constant
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[cubemapMaterialID].cubemapSamplerID);
			CheckGLErrorMessages();

			glm::vec2 cubemapSize = m_Materials[cubemapMaterialID].material.irradianceSamplerSize;

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			CheckGLErrorMessages();
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize.x, cubemapSize.y);
			CheckGLErrorMessages();

			glViewport(0, 0, cubemapSize.x, cubemapSize.y);
			CheckGLErrorMessages();

			if (skybox->enableCulling)
			{
				glEnable(GL_CULL_FACE);
			}
			else
			{
				glDisable(GL_CULL_FACE);
			}

			glCullFace(skybox->cullFace);
			CheckGLErrorMessages();

			glDepthFunc(skybox->depthTestReadFunc);
			CheckGLErrorMessages();

			glDepthMask(skybox->depthWriteEnable);
			CheckGLErrorMessages();

			for (u32 i = 0; i < 6; ++i)
			{
				glBindVertexArray(skybox->VAO);
				CheckGLErrorMessages();
				glBindBuffer(GL_ARRAY_BUFFER, skybox->VBO);
				CheckGLErrorMessages();

				glUniformMatrix4fv(irradianceMat.uniformIDs.view, 1, false, &m_CaptureViews[i][0][0]);
				CheckGLErrorMessages();

				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[cubemapMaterialID].irradianceSamplerID, 0);
				CheckGLErrorMessages();

				// Should be drawing cube here, not object (relfection probe's sphere is being drawn
				glDrawArrays(skybox->topology, 0, (GLsizei)skybox->vertexBufferData->VertexCount);
				CheckGLErrorMessages();
			}

			glUseProgram(0);
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
		}

		void GLRenderer::CaptureSceneToCubemap(const GameContext& gameContext, RenderID cubemapRenderID)
		{
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);

			BatchRenderObjects(gameContext);

			DrawCallInfo drawCallInfo = {};
			drawCallInfo.renderToCubemap = true;
			drawCallInfo.cubemapObjectRenderID = cubemapRenderID;

			// Clear cubemap faces
			GLRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
			GLMaterial* cubemapMaterial = &m_Materials[cubemapRenderObject->materialID];

			glm::vec2 cubemapSize = cubemapMaterial->material.cubemapSamplerSize;

			// Must be enabled to clear depth buffer
			glDepthMask(GL_TRUE);
			CheckGLErrorMessages();
			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			CheckGLErrorMessages();
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize.x, cubemapSize.y);
			CheckGLErrorMessages();

			glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);
			CheckGLErrorMessages();

			for (size_t face = 0; face < 6; ++face)
			{
				// Clear all gbuffers
				if (!cubemapMaterial->cubemapSamplerGBuffersIDs.empty())
				{
					for (size_t i = 0; i < cubemapMaterial->cubemapSamplerGBuffersIDs.size(); ++i)
					{
						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, 
							GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerGBuffersIDs[i].id, 0);
						CheckGLErrorMessages();

						glDepthMask(GL_TRUE);

						glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
						CheckGLErrorMessages();
					}
				}

				// Clear base cubemap framebuffer + depth buffer
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerID, 0);
				CheckGLErrorMessages();
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapDepthSamplerID, 0);
				CheckGLErrorMessages();

				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				CheckGLErrorMessages();
			}

			drawCallInfo.deferred = true;
			DrawDeferredObjects(gameContext, drawCallInfo);
			drawCallInfo.deferred = false;
			DrawGBufferQuad(gameContext, drawCallInfo);
			DrawForwardObjects(gameContext, drawCallInfo);
			
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SwapBuffers(const GameContext& gameContext)
		{
			glfwSwapBuffers(static_cast<GLWindowWrapper*>(gameContext.window)->GetWindow());
		}

		bool GLRenderer::GetShaderID(const std::string& shaderName, ShaderID& shaderID)
		{
			// TODO: Store shaders using sorted data structure?
			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				if (m_Shaders[i].shader.name.compare(shaderName) == 0)
				{
					shaderID = i;
					return true;
				}
			}

			return false;
		}

		bool GLRenderer::GetMaterialID(const std::string& materialName, MaterialID& materialID)
		{
			// TODO: Store shaders using sorted data structure?
			for (auto iter = m_Materials.begin(); iter != m_Materials.end(); ++iter)
			{
				if (iter->second.material.name.compare(materialName) == 0)
				{
					materialID = iter->first;
					return true;
				}
			}

			return false;
		}

		void GLRenderer::SetTopologyMode(RenderID renderID, TopologyMode topology)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject)
			{
				Logger::LogError("Invalid renderID passed to SetTopologyMode: " + std::to_string(renderID));
				return;
			}

			GLenum glMode = TopologyModeToGLMode(topology);

			if (glMode == GL_INVALID_ENUM)
			{
				Logger::LogError("Unhandled TopologyMode passed to GLRenderer::SetTopologyMode: " + 
					std::to_string((i32)topology));
				renderObject->topology = GL_TRIANGLES;
			}
			else
			{
				renderObject->topology = glMode;
			}
		}

		void GLRenderer::SetClearColor(real r, real g, real b)
		{
			m_ClearColor = { r, g, b };
			glClearColor(r, g, b, 1.0f);
			CheckGLErrorMessages();
		}

		void GLRenderer::GenerateFrameBufferTexture(u32* handle, i32 index, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size)
		{
			glGenTextures(1, handle);
			glBindTexture(GL_TEXTURE_2D, *handle);
			CheckGLErrorMessages();
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.x, size.y, 0, format, type, NULL);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			CheckGLErrorMessages();
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, *handle, 0);
			CheckGLErrorMessages();
			glBindTexture(GL_TEXTURE_2D, 0);
			CheckGLErrorMessages();
		}

		void GLRenderer::ResizeFrameBufferTexture(u32 handle, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size)
		{
			glBindTexture(GL_TEXTURE_2D, handle);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.x, size.y, 0, format, type, NULL);
			CheckGLErrorMessages();
		}

		void GLRenderer::ResizeRenderBuffer(u32 handle, const glm::vec2i& size)
		{
			glBindRenderbuffer(GL_RENDERBUFFER, handle);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, size.x, size.y);
		}

		void GLRenderer::Update(const GameContext& gameContext)
		{
			m_PhysicsDebugDrawer->UpdateDebugMode();

			// TODO: Move keybind catches out to FlexEngine or Renderer base Update and
			// call renderer-specific code
			if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_U))
			{
				for (auto iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
				{
					GLRenderObject* renderObject = *iter;
					if (renderObject && m_Materials[renderObject->materialID].material.generateReflectionProbeMaps)
					{
						Logger::LogInfo("Capturing reflection probe");
						CaptureSceneToCubemap(gameContext, renderObject->renderID);
						GenerateIrradianceSamplerFromCubemap(gameContext, renderObject->materialID);
						GeneratePrefilteredMapFromCubemap(gameContext, renderObject->materialID);
						Logger::LogInfo("Done");
					}
				}
			}
		}

		void GLRenderer::Draw(const GameContext& gameContext)
		{
			CheckGLErrorMessages();

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			CheckGLErrorMessages();

			DrawCallInfo drawCallInfo = {};

			// TODO: Don't sort render objects frame! Only when things are added/removed
			BatchRenderObjects(gameContext);

			// World-space objects
			DrawDeferredObjects(gameContext, drawCallInfo);
			DrawGBufferQuad(gameContext, drawCallInfo);
			DrawForwardObjects(gameContext, drawCallInfo);
			DrawWorldSpaceSprites(gameContext);
			DrawOffscreenTexture(gameContext);

			if (!m_PhysicsDebuggingSettings.DisableAll)
			{
				PhysicsDebugRender(gameContext);
			}

			DrawEditorObjects(gameContext, drawCallInfo);

			// Screen-space objects
			std::string fxaaEnabledStr = std::string("FXAA: ") + (m_bEnableFXAA ? "1" : "0");
			SetFont(m_FntUbuntuCondensed);
			DrawString(fxaaEnabledStr, glm::vec4(1.0f), glm::vec2(300.0f, 300.0f));

			UpdateTextBuffer();
			DrawText(gameContext);

			DrawScreenSpaceSprites(gameContext);

			ImGuiRender();

			SwapBuffers(gameContext);
		}

		void GLRenderer::BatchRenderObjects(const GameContext& gameContext)
		{
			/*
			TODO: Don't create two nested vectors every call, just sort things by deferred/forward, then by material ID
			

			Eg. deferred | matID
			yes		 0
			yes		 2
			no		 1
			no		 3
			no		 5
			*/
			m_DeferredRenderObjectBatches.clear();
			m_ForwardRenderObjectBatches.clear();
			m_EditorRenderObjectBatch.clear();
			
			// Sort render objects i32o deferred + forward buckets
			for (auto iter = m_Materials.begin(); iter != m_Materials.end(); ++iter)
			{
				MaterialID matID = iter->first;
				ShaderID shaderID = iter->second.material.shaderID;
				if (shaderID == InvalidShaderID)
				{
					Logger::LogWarning("GLRenderer::BatchRenderObjects > Material has invalid shaderID: " + iter->second.material.name);
					continue;
				}
				GLShader* shader = &m_Shaders[shaderID];

				UpdateMaterialUniforms(gameContext, matID);

				if (shader->shader.deferred)
				{
					m_DeferredRenderObjectBatches.push_back({});
					for (size_t j = 0; j < m_RenderObjects.size(); ++j)
					{
						GLRenderObject* renderObject = GetRenderObject(j);
						if (renderObject &&
							renderObject->gameObject->IsVisible() &&
							renderObject->materialID == matID &&
							!renderObject->editorObject)
						{
							m_DeferredRenderObjectBatches.back().push_back(renderObject);
						}
					}
				}
				else
				{
					m_ForwardRenderObjectBatches.push_back({});
					for (size_t j = 0; j < m_RenderObjects.size(); ++j)
					{
						GLRenderObject* renderObject = GetRenderObject(j);
						if (renderObject &&
							renderObject->gameObject->IsVisible() &&
							renderObject->materialID == matID &&
							!renderObject->editorObject)
						{
							m_ForwardRenderObjectBatches.back().push_back(renderObject);
						}
					}
				}
			}

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				GLRenderObject* renderObject = GetRenderObject(i);
				if (renderObject &&
					renderObject->gameObject->IsVisible() &&
					renderObject->editorObject)
				{
					m_EditorRenderObjectBatch.push_back(renderObject);
				}
			}
		}

		void GLRenderer::DrawDeferredObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo)
		{
			if (drawCallInfo.renderToCubemap)
			{
				// TODO: Bind depth buffer to cubemap's depth buffer (needs to generated?)

				GLRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
				GLMaterial* cubemapMaterial = &m_Materials[cubemapRenderObject->materialID];

				glm::vec2 cubemapSize = cubemapMaterial->material.cubemapSamplerSize;

				glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
				CheckGLErrorMessages();
				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				CheckGLErrorMessages();
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize.x, cubemapSize.y);
				CheckGLErrorMessages();

				glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);
				CheckGLErrorMessages();
			}
			else
			{
				glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);
				CheckGLErrorMessages();
				glBindRenderbuffer(GL_RENDERBUFFER, m_gBufferDepthHandle);
				CheckGLErrorMessages();
			}

			{
				// TODO: Make more dynamic (based on framebuffer count)
				constexpr i32 numBuffers = 3;
				u32 attachments[numBuffers] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
				glDrawBuffers(numBuffers, attachments);
				CheckGLErrorMessages();
			}

			glDepthMask(GL_TRUE);

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			CheckGLErrorMessages();

			for (size_t i = 0; i < m_DeferredRenderObjectBatches.size(); ++i)
			{
				if (!m_DeferredRenderObjectBatches[i].empty())
				{
					DrawRenderObjectBatch(gameContext, m_DeferredRenderObjectBatches[i], drawCallInfo);
				}
			}

			glUseProgram(0);
			glBindVertexArray(0);
			CheckGLErrorMessages();

			{
				constexpr i32 numBuffers = 1;
				u32 attachments[numBuffers] = { GL_COLOR_ATTACHMENT0 };
				glDrawBuffers(numBuffers, attachments);
				CheckGLErrorMessages();
			}

			// Copy depth from gbuffer to default render target
			if (drawCallInfo.renderToCubemap)
			{
				// No blit is needed, right? We already drew to the cubemap depth?
			}
			else
			{
				const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

				glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferHandle);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_Offscreen0RBO);
				glBlitFramebuffer(0, 0, frameBufferSize.x, frameBufferSize.y, 0, 0, frameBufferSize.x, 
					frameBufferSize.y, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
				CheckGLErrorMessages();
			}
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		}

		void GLRenderer::DrawGBufferQuad(const GameContext& gameContext, const DrawCallInfo& drawCallInfo)
		{
			if (!m_gBufferQuadVertexBufferData.pDataStart)
			{
				// Generate GBuffer if not already generated
				GenerateGBuffer(gameContext);
			}

			if (drawCallInfo.renderToCubemap)
			{
				GLRenderObject* skybox = GetRenderObject(m_SkyBoxMesh->GetRenderID());

				GLRenderObject* cubemapObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
				GLMaterial* cubemapMaterial = &m_Materials[cubemapObject->materialID];
				GLShader* cubemapShader = &m_Shaders[cubemapMaterial->material.shaderID];

				CheckGLErrorMessages();
				glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
				CheckGLErrorMessages();
				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				CheckGLErrorMessages();
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
					cubemapMaterial->material.cubemapSamplerSize.x, cubemapMaterial->material.cubemapSamplerSize.y);
				CheckGLErrorMessages();

				glUseProgram(cubemapShader->program);

				glBindVertexArray(skybox->VAO);
				CheckGLErrorMessages();
				glBindBuffer(GL_ARRAY_BUFFER, skybox->VBO);
				CheckGLErrorMessages();

				UpdateMaterialUniforms(gameContext, cubemapObject->materialID);
				UpdatePerObjectUniforms(cubemapObject->materialID, 
					skybox->gameObject->GetTransform()->GetWorldTransform(), gameContext);

				u32 bindingOffset = BindDeferredFrameBufferTextures(cubemapMaterial);
				BindTextures(&cubemapShader->shader, cubemapMaterial, bindingOffset);

				if (skybox->enableCulling)
				{
					glEnable(GL_CULL_FACE);
				}
				else
				{
					glDisable(GL_CULL_FACE);
				}

				glCullFace(skybox->cullFace);
				CheckGLErrorMessages();

				glDepthFunc(GL_ALWAYS);
				CheckGLErrorMessages();

				glDepthMask(GL_FALSE);
				CheckGLErrorMessages();
				
				glUniformMatrix4fv(cubemapMaterial->uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
				CheckGLErrorMessages();

				glBindRenderbuffer(GL_RENDERBUFFER, 0);

				for (i32 face = 0; face < 6; ++face)
				{
					glUniformMatrix4fv(cubemapMaterial->uniformIDs.view, 1, false, &m_CaptureViews[face][0][0]);
					CheckGLErrorMessages();

					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerID, 0);
					CheckGLErrorMessages();

					// Draw cube (TODO: Use "cube" object to be less confusing)
					glDrawArrays(skybox->topology, 0, (GLsizei)skybox->vertexBufferData->VertexCount);
					CheckGLErrorMessages();
				}

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				CheckGLErrorMessages();
			}
			else
			{
				glBindFramebuffer(GL_FRAMEBUFFER, m_Offscreen0FBO);
				CheckGLErrorMessages();
				glBindRenderbuffer(GL_RENDERBUFFER, m_Offscreen0RBO);
				CheckGLErrorMessages();

				glDepthMask(GL_TRUE);

				glClear(GL_COLOR_BUFFER_BIT); // Don't clear depth - we just copied it over from the GBuffer
				CheckGLErrorMessages();

				GLRenderObject* gBufferQuad = GetRenderObject(m_GBufferQuadRenderID);

				GLMaterial* material = &m_Materials[gBufferQuad->materialID];
				GLShader* glShader = &m_Shaders[material->material.shaderID];
				Shader* shader = &glShader->shader;
				glUseProgram(glShader->program);

				glBindVertexArray(gBufferQuad->VAO);
				glBindBuffer(GL_ARRAY_BUFFER, gBufferQuad->VBO);
				CheckGLErrorMessages();

				UpdateMaterialUniforms(gameContext, gBufferQuad->materialID);
				UpdatePerObjectUniforms(gBufferQuad->renderID, gameContext);

				u32 bindingOffset = BindFrameBufferTextures(material);
				BindTextures(shader, material, bindingOffset);

				if (gBufferQuad->enableCulling)
				{
					glEnable(GL_CULL_FACE);
				}
				else
				{
					glDisable(GL_CULL_FACE);
				}

				glCullFace(gBufferQuad->cullFace);
				CheckGLErrorMessages();

				glDepthFunc(gBufferQuad->depthTestReadFunc);
				CheckGLErrorMessages();

				glDepthMask(gBufferQuad->depthWriteEnable);
				CheckGLErrorMessages();

				glDrawArrays(gBufferQuad->topology, 0, (GLsizei)gBufferQuad->vertexBufferData->VertexCount);
				CheckGLErrorMessages();
			}
		}

		void GLRenderer::DrawForwardObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo)
		{
			for (size_t i = 0; i < m_ForwardRenderObjectBatches.size(); ++i)
			{
				if (!m_ForwardRenderObjectBatches[i].empty())
				{
					DrawRenderObjectBatch(gameContext, m_ForwardRenderObjectBatches[i], drawCallInfo);
				}
			}
		}

		void GLRenderer::DrawEditorObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo)
		{
			DrawRenderObjectBatch(gameContext, m_EditorRenderObjectBatch, drawCallInfo);
		}

		void GLRenderer::DrawOffscreenTexture(const GameContext& gameContext)
		{
			i32 FBO = m_Offscreen1FBO;
			i32 RBO = m_Offscreen1RBO;

			if (!m_bEnableFXAA)
			{
				FBO = 0;
				RBO = 0;
			}

			glm::vec3 pos(0.0f);
			glm::quat rot = glm::quat();
			glm::vec3 scale(1.0f, 1.0f, 1.0f);
			glm::vec4 color(1.0f);
			DrawSpriteQuad(gameContext, m_OffscreenTexture0Handle.id, FBO, RBO,
						   m_PostProcessMatID, pos, rot, scale, AnchorPoint::WHOLE, color, false);

			if (m_bEnableFXAA)
			{
				FBO = 0;
				RBO = 0;

				scale = glm::vec3(1.0f, 1.0f, 1.0f);
				DrawSpriteQuad(gameContext, m_OffscreenTexture1Handle.id, FBO, RBO,
							   m_PostFXAAMatID, pos, rot, scale, AnchorPoint::WHOLE, color, false);
			}

			{
				const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

				glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Offscreen0RBO);
				CheckGLErrorMessages();
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
				CheckGLErrorMessages();
				glBlitFramebuffer(0, 0, frameBufferSize.x, frameBufferSize.y,
								  0, 0, frameBufferSize.x, frameBufferSize.y,
								  GL_DEPTH_BUFFER_BIT, GL_NEAREST);
				CheckGLErrorMessages();
			}

		}

		void GLRenderer::DrawScreenSpaceSprites(const GameContext& gameContext)
		{
			glm::vec3 pos(0.0f);
			glm::quat rot = glm::quat(glm::vec3(.0f, 0.0f, sin(gameContext.elapsedTime * 0.2f)));
			glm::quat rot2 = glm::quat(glm::vec3(.0f, 0.0f, sin(-gameContext.elapsedTime * 0.2f)));
			glm::vec3 scale(100.0f);
			glm::vec4 color(1.0f);
			DrawSpriteQuad(gameContext, m_WorkTextureHandle.id, 0, 0, m_SpriteMatID,
						   pos, rot, scale, AnchorPoint::BOTTOM_RIGHT, color);
			//DrawSpriteQuad(gameContext, m_WorkTextureHandle.id, m_SpriteMatID,
			//			   pos, rot2, scale, AnchorPoint::BOTTOM_RIGHT, color);
			//DrawSpriteQuad(gameContext, m_WorkTextureHandle.id, m_SpriteMatID,
			//			   pos, rot, scale, AnchorPoint::BOTTOM_LEFT, color);
			//DrawSpriteQuad(gameContext, m_WorkTextureHandle.id, m_SpriteMatID,
			//			   pos, rot2, scale, AnchorPoint::LEFT, color);
			//DrawSpriteQuad(gameContext, m_WorkTextureHandle.id, m_SpriteMatID,
			//			   pos, rot, scale, AnchorPoint::TOP_LEFT, color);
			//DrawSpriteQuad(gameContext, m_WorkTextureHandle.id, m_SpriteMatID,
			//			   pos, rot2, scale, AnchorPoint::TOP, color);
			//DrawSpriteQuad(gameContext, m_WorkTextureHandle.id, m_SpriteMatID,
			//			   pos, rot, scale, AnchorPoint::TOP_RIGHT, color);
			//DrawSpriteQuad(gameContext, m_WorkTextureHandle.id, m_SpriteMatID,
			//			   pos, rot2, scale, AnchorPoint::RIGHT, color);
			//DrawSpriteQuad(gameContext, m_WorkTextureHandle.id, m_SpriteMatID,
			//			   pos, rot, glm::vec3(200.0f), AnchorPoint::CENTER, glm::vec4(1.0f, 0.0f, 1.0f, 0.5f));
		}

		void GLRenderer::DrawWorldSpaceSprites(const GameContext& gameContext)
		{
			//glm::vec3 pos(0.0f);
			//glm::quat rot = glm::quat(glm::vec3(.0f, 0.0f, sin(gameContext.elapsedTime * 0.2f)));
			//glm::quat rot2 = glm::quat(glm::vec3(.0f, 0.0f, sin(-gameContext.elapsedTime * 0.2f)));
			//glm::vec3 scale(100.0f);
			//glm::vec4 color(1.0f);
			//DrawSpriteQuad(gameContext, m_WorkTextureHandle.id, 0, 0, m_SpriteMatID,
			//			   pos, rot, scale, AnchorPoint::BOTTOM_RIGHT, color);
		}

		void GLRenderer::DrawSpriteQuad(const GameContext& gameContext, 
										u32 inputTextureHandle,
										u32 FBO, 
										u32 RBO,
										MaterialID materialID,
										const glm::vec3& posOff, 
										const glm::quat& rotationOff, 
										const glm::vec3& scaleOff,
										AnchorPoint anchor,
										const glm::vec4& color,
										bool writeDepth)
		{
			GLRenderObject* spriteRenderObject = GetRenderObject(m_SpriteQuadRenderID);
			if (!spriteRenderObject)
			{
				return;
			}

			spriteRenderObject->materialID = materialID;

			GLMaterial& spriteMaterial = m_Materials[spriteRenderObject->materialID];
			GLShader& spriteShader = m_Shaders[spriteMaterial.material.shaderID];

			glUseProgram(spriteShader.program);
			CheckGLErrorMessages();

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

			if (spriteShader.shader.dynamicBufferUniforms.HasUniform("transformMat"))
			{
				glm::vec3 scale(1.0f);
				
				if (anchor != AnchorPoint::WHOLE)
				{
					scale = glm::vec3(1.0f / frameBufferSize.x, 1.0f / frameBufferSize.y, 1.0f);
				}
				////scale *= glm::min(frameBufferSize.x * 2.0f, frameBufferSize.y * 2.0f);
				//scale *= 400.0f;
				//const real minScale = 0.2f;
				//const real maxScale = 0.5f;
				//scale *= glm::vec3((sin(gameContext.elapsedTime * 0.4f) * 0.5f + 0.5f) * (maxScale - minScale) + minScale);
				scale *= scaleOff;

				glm::quat rotation = rotationOff;

				glm::vec3 translation(0.0f);
				switch (anchor)
				{
				case AnchorPoint::CENTER:
					// Already centered (zero)
					break;
				case AnchorPoint::TOP_LEFT:
					translation = glm::vec3(-1.0f + (scale.x), (1.0f - scale.y), 0.0f);
					break;
				case AnchorPoint::TOP:
					translation = glm::vec3(0.0f, (1.0f - scale.y), 0.0f);
					break;
				case AnchorPoint::TOP_RIGHT:
					translation = glm::vec3(1.0f - (scale.x), (1.0f - scale.y), 0.0f);
					break;
				case AnchorPoint::RIGHT:
					translation = glm::vec3(1.0f - (scale.x), 0.0f, 0.0f);
					break;
				case AnchorPoint::BOTTOM_RIGHT:
					translation = glm::vec3(1.0f - (scale.x), (-1.0f + scale.y), 0.0f);
					break;
				case AnchorPoint::BOTTOM:
					translation = glm::vec3(0.0f, (-1.0f + scale.y), 0.0f);
					break;
				case AnchorPoint::BOTTOM_LEFT:
					translation = glm::vec3(-1.0f + (scale.x), (-1.0f + scale.y), 0.0f);
					break;
				case AnchorPoint::LEFT:
					translation = glm::vec3(-1.0f + (scale.x), 0.0f, 0.0f);
					break;
				case AnchorPoint::WHOLE:
					// Already centered (zero)
					break;
				default:
					break;
				}

				translation += posOff;

				glm::mat4 matScale = glm::scale(glm::mat4(1.0f), scale);
				glm::mat4 matRot = glm::mat4(rotation);
				glm::mat4 matTrans = glm::translate(glm::mat4(1.0f), translation);
				// What about SRT? :o
				glm::mat4 transformMat = matTrans * matScale * matRot;
				
				glUniformMatrix4fv(spriteMaterial.uniformIDs.transformMat, 1, true, &transformMat[0][0]);
				CheckGLErrorMessages();
			}

			if (spriteShader.shader.dynamicBufferUniforms.HasUniform("colorMultiplier"))
			{
				glUniform4fv(spriteMaterial.uniformIDs.colorMultiplier, 1, &color.r);
				CheckGLErrorMessages();
			}

			glViewport(0, 0, (GLsizei)frameBufferSize.x, (GLsizei)frameBufferSize.y);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, FBO);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, RBO);
			CheckGLErrorMessages();

			glBindVertexArray(spriteRenderObject->VAO);
			CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, spriteRenderObject->VBO);
			CheckGLErrorMessages();

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, inputTextureHandle);
			CheckGLErrorMessages();

			// TODO: Use member
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			CheckGLErrorMessages();

			// TODO: Is this necessary to clear the depth?
			if (writeDepth)
			{
				glDepthMask(GL_TRUE);
			}
			else
			{
				glDepthMask(GL_FALSE);
			}

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			if (spriteRenderObject->enableCulling)
			{
				glEnable(GL_CULL_FACE);
			}
			else
			{
				glDisable(GL_CULL_FACE);
			}

			glCullFace(spriteRenderObject->cullFace);
			CheckGLErrorMessages();

			glDepthFunc(spriteRenderObject->depthTestReadFunc);
			CheckGLErrorMessages();

			glDepthMask(spriteRenderObject->depthWriteEnable);
			CheckGLErrorMessages();

			glDrawArrays(spriteRenderObject->topology, 0, (GLsizei)spriteRenderObject->vertexBufferData->VertexCount);
			CheckGLErrorMessages();
		}

		void GLRenderer::DrawText(const GameContext& gameContext)
		{
			GLMaterial& fontMaterial = m_Materials[m_FontMatID];
			GLShader& fontShader = m_Shaders[fontMaterial.material.shaderID];

			glUseProgram(fontShader.program);
			CheckGLErrorMessages();

			//if (fontShader.shader.dynamicBufferUniforms.HasUniform("soften"))
			//{
			//	real soften = ((sin(gameContext.elapsedTime) * 0.5f + 0.5f) * 0.25f);
			//	glUniform1f(glGetUniformLocation(fontShader.program, "soften"), soften);
			//	CheckGLErrorMessages();
			//}
			
			//if (fontShader.shader.dynamicBufferUniforms.HasUniform("colorMultiplier"))
			//{
			//	glm::vec4 color(1.0f);
			//	glUniform4fv(fontMaterial.uniformIDs.colorMultiplier, 1, &color.r);
			//	CheckGLErrorMessages();
			//}

			glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

			glViewport(0, 0, (GLsizei)(frameBufferSize.x), (GLsizei)(frameBufferSize.y));
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			CheckGLErrorMessages();

			glBindVertexArray(m_TextQuadVAO);
			CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, m_TextQuadVBO);
			CheckGLErrorMessages();

			for (BitmapFont* font : m_Fonts)
			{
				if (font->m_BufferSize > 0)
				{
					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, font->GetTexture()->GetHandle());
					CheckGLErrorMessages();

					if (fontShader.shader.dynamicBufferUniforms.HasUniform("transformMat"))
					{
						// TODO: Find out how font sizes actually work
						real scale = ((real)font->GetFontSize()) / 12.0f;
						glm::mat4 transformMat = glm::scale(glm::mat4(1.0f), glm::vec3(
							(1.0f / frameBufferSize.x) * scale,
							-(1.0f / frameBufferSize.y) * scale,
							1.0f));
						glUniformMatrix4fv(fontMaterial.uniformIDs.transformMat, 1, true, &transformMat[0][0]);
						CheckGLErrorMessages();
					}

					if (fontShader.shader.dynamicBufferUniforms.HasUniform("texSize"))
					{
						glm::vec2 texSize = (glm::vec2)font->GetTexture()->GetResolution();
						glUniform2fv(fontMaterial.uniformIDs.texSize, 1, &texSize.r);
						CheckGLErrorMessages();
					}

					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					CheckGLErrorMessages();
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					CheckGLErrorMessages();

					glDisable(GL_CULL_FACE);
					CheckGLErrorMessages();

					glDepthFunc(GL_ALWAYS);
					CheckGLErrorMessages();

					glDepthMask(GL_FALSE);
					CheckGLErrorMessages();

					glEnable(GL_BLEND);
					CheckGLErrorMessages();
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					CheckGLErrorMessages();

					glDrawArrays(GL_POINTS, font->m_BufferStart, font->m_BufferSize);
					CheckGLErrorMessages();
				}
			}

			glBindVertexArray(0);
			glUseProgram(0);
		}

		bool GLRenderer::LoadFont(const GameContext& gameContext, BitmapFont** font, const std::string& filePath, i32 size)
		{
			// TODO: Determine via monitor struct
			glm::vec2i monitorDPI(300, 300);

			FT_Error error;
			error = FT_Init_FreeType(&ft);
			if (error != FT_Err_Ok)
			{
				Logger::LogError("Could not init FreeType");
				return false;
			}

			std::vector<char> fileMemory;
			ReadFile(filePath, fileMemory, true);

			FT_Face face;
			error = FT_New_Memory_Face(ft, (FT_Byte*)fileMemory.data(), (FT_Long)fileMemory.size(), 0, &face);
			if (error == FT_Err_Unknown_File_Format)
			{
				Logger::LogError("Unhandled font file format: " + filePath);
				return false;
			}
			else if (error != FT_Err_Ok ||
					 !face)
			{
				Logger::LogError("Failed to create new font face: " + filePath);
				return false;
			}

			error = FT_Set_Char_Size(face,
									 0, size * 64,
									 monitorDPI.x, monitorDPI.y);

			//FT_Set_Pixel_Sizes(face, 0, fontPixelSize);

			std::string fileName = filePath;
			StripLeadingDirectories(fileName);
			Logger::LogInfo("Loaded font " + fileName);

			std::string fontName = std::string(face->family_name) + " - " + face->style_name;
			*font = new BitmapFont(size, fontName, face->num_glyphs);
			BitmapFont* newFont = *font; // Shortcut

			m_Fonts.push_back(newFont);

			// font->m_UseKerning = FT_HAS_KERNING(face) != 0;

			// Atlas helper variables
			glm::vec2i startPos[4] = { { 0.0f, 0.0f },{ 0.0f, 0.0f },{ 0.0f, 0.0f },{ 0.0f, 0.0f } };
			glm::vec2i maxPos[4] = { { 0.0f, 0.0f },{ 0.0f, 0.0f },{ 0.0f, 0.0f },{ 0.0f, 0.0f } };
			bool bHorizontal = false; // Direction this pass expands the map in (internal moves are !bHorizontal)
			u32 posCount = 1; // Internal move count in this pass
			u32 curPos = 0;   // Internal move count
			u32 channel = 0;  // Current channel writing to

			u32 padding = 1;
			u32 spread = 5;
			u32 totPadding = padding + spread;

			u32 sampleDensity = 32;

			std::map<i32, FontMetric*> characters;
			for (i32 c = 0; c < BitmapFont::CHAR_COUNT - 1; ++c)
			{
				FontMetric* metric = newFont->GetMetric((wchar_t)c);
				if (!metric)
				{
					continue;
				}

				metric->character = (wchar_t)c;

				u32 glyphIndex = FT_Get_Char_Index(face, c);
				// TODO: Is this correct?
				if (glyphIndex == 0)
				{
					continue;
				}

				if (newFont->GetUseKerning() && glyphIndex)
				{
					for (i32 previous = 0; previous < BitmapFont::CHAR_COUNT - 1; ++previous)
					{
						FT_Vector delta;

						u32 prevIdx = FT_Get_Char_Index(face, previous);
						FT_Get_Kerning(face, prevIdx, glyphIndex, FT_KERNING_DEFAULT, &delta);

						if (delta.x || delta.y)
						{
							metric->kerning[static_cast<char>(previous)] =
								glm::vec2((real)delta.x / 64.0f, (real)delta.y / 64.0f);
						}
					}
				}

				if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER))
				{
					Logger::LogError("Failed to load glyph with index " + std::to_string(glyphIndex));
					continue;
				}


				u32 width = face->glyph->bitmap.width + totPadding * 2;
				u32 height = face->glyph->bitmap.rows + totPadding * 2;


				metric->width = (u32)width;
				metric->height = (u32)height;
				metric->offsetX = (i16)face->glyph->bitmap_left + (i16)totPadding;
				metric->offsetY = -(i16)(face->glyph->bitmap_top + (i16)totPadding);
				metric->advanceX = (real)face->glyph->advance.x / 64.0f;

				// Generate atlas coordinates
				//metric->page = 0;
				metric->channel = (u8)channel;
				metric->texCoord = startPos[channel];
				if (bHorizontal)
				{
					maxPos[channel].y = std::max(maxPos[channel].y, startPos[channel].y + (i32)height);
					startPos[channel].y += height;
					maxPos[channel].x = std::max(maxPos[channel].x, startPos[channel].x + (i32)width);
				}
				else
				{
					maxPos[channel].x = std::max(maxPos[channel].x, startPos[channel].x + (i32)width);
					startPos[channel].x += width;
					maxPos[channel].y = std::max(maxPos[channel].y, startPos[channel].y + (i32)height);
				}
				channel++;
				if (channel == 4)
				{
					channel = 0;
					curPos++;
					if (curPos == posCount)
					{
						curPos = 0;
						bHorizontal = !bHorizontal;
						if (bHorizontal)
						{
							for (u8 cha = 0; cha < 4; ++cha)
							{
								startPos[cha] = glm::vec2i(maxPos[cha].x, 0);
							}
						}
						else
						{
							for (u8 cha = 0; cha < 4; ++cha)
							{
								startPos[cha] = glm::vec2i(0, maxPos[cha].y);
							}
							posCount++;
						}
					}
				}

				metric->bIsValid = true;

				characters[c] = metric;
			}

			glm::vec2i textureSize(
				std::max(std::max(maxPos[0].x, maxPos[1].x), std::max(maxPos[2].x, maxPos[3].x)),
				std::max(std::max(maxPos[0].y, maxPos[1].y), std::max(maxPos[2].y, maxPos[3].y)));
			newFont->SetTextureSize(textureSize);

			//Setup rendering
			TextureParameters params(false);
			params.wrapS = GL_CLAMP_TO_EDGE;
			params.wrapT = GL_CLAMP_TO_EDGE;

			GLTexture* fontTex = newFont->SetTexture(new GLTexture(textureSize.x, textureSize.y, GL_RGBA16F, GL_RGBA, GL_FLOAT));
			fontTex->Build();
			fontTex->SetParameters(params);

			GLuint captureFBO;
			GLuint captureRBO;

			glGenFramebuffers(1, &captureFBO);
			CheckGLErrorMessages();
			glGenRenderbuffers(1, &captureRBO);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
			CheckGLErrorMessages();

			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, textureSize.x, textureSize.y);
			CheckGLErrorMessages();
			// TODO: Don't use depth buffer
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fontTex->GetHandle(), 0);
			CheckGLErrorMessages();

			glViewport(0, 0, textureSize.x, textureSize.y);
			CheckGLErrorMessages();
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			CheckGLErrorMessages();

			ShaderID computeSDFShaderID;
			GetShaderID("compute_sdf", computeSDFShaderID);
			GLShader& computeSDFShader = m_Shaders[computeSDFShaderID];

			glUseProgram(computeSDFShader.program);

			glUniform1i(glGetUniformLocation(computeSDFShader.program, "highResTex"), 0);
			auto texChannel = glGetUniformLocation(computeSDFShader.program, "texChannel");
			auto charResolution = glGetUniformLocation(computeSDFShader.program, "charResolution");
			glUniform1f(glGetUniformLocation(computeSDFShader.program, "spread"), (real)spread);
			glUniform1f(glGetUniformLocation(computeSDFShader.program, "sampleDensity"), (real)sampleDensity);

			glEnable(GL_BLEND);
			glBlendEquation(GL_FUNC_ADD);
			glBlendFunc(GL_ONE, GL_ONE);
			CheckGLErrorMessages();

			GLRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);

			//Render to Glyphs atlas
			FT_Set_Pixel_Sizes(face, 0, size * sampleDensity);

			for (auto& character : characters)
			{
				auto metric = character.second;

				u32 glyphIndex = FT_Get_Char_Index(face, metric->character);
				if (glyphIndex == 0)
				{
					continue;
				}

				if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER))
				{
					Logger::LogError("Failed to load glyph with index " + std::to_string(glyphIndex));
					continue;
				}

				if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP)
				{
					if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
					{
						Logger::LogError("Failed to render glyph with index " + std::to_string(glyphIndex));
						continue;
					}
				}

				if (face->glyph->bitmap.width == 0 ||
					face->glyph->bitmap.rows == 0)
				{
					continue;
				}

				FT_Bitmap alignedBitmap{};
				if (FT_Bitmap_Convert(ft, &face->glyph->bitmap, &alignedBitmap, 4))
				{
					Logger::LogError("Couldn't align free type bitmap size");
					continue;
				}

				u32 width = alignedBitmap.width;
				u32 height = alignedBitmap.rows;

				if (width == 0 ||
					height == 0)
				{
					continue;
				}

				GLuint texHandle;
				glGenTextures(1, &texHandle);
				CheckGLErrorMessages();

				glActiveTexture(GL_TEXTURE0);
				CheckGLErrorMessages();

				glBindTexture(GL_TEXTURE_2D, texHandle);
				CheckGLErrorMessages();
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, alignedBitmap.buffer);
				CheckGLErrorMessages();

				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
				CheckGLErrorMessages();
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
				CheckGLErrorMessages();
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
				CheckGLErrorMessages();
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				CheckGLErrorMessages();
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				CheckGLErrorMessages();
				glm::vec4 borderColor(0.0f);
				glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &borderColor.r);
				CheckGLErrorMessages();

				if (metric->width > 0 && metric->height > 0)
				{
					glm::vec2i res = glm::vec2i(metric->width - padding * 2, metric->height - padding * 2);
					glm::vec2i viewportTL = glm::vec2i(metric->texCoord) + glm::vec2i(padding);

					glViewport(viewportTL.x, viewportTL.y, res.x, res.y);
					CheckGLErrorMessages();
					glActiveTexture(GL_TEXTURE0);
					CheckGLErrorMessages();
					glBindTexture(GL_TEXTURE_2D, texHandle);
					CheckGLErrorMessages();

					glUniform1i(texChannel, metric->channel);
					glUniform2f(charResolution, (real)res.x, (real)res.y);
					CheckGLErrorMessages();
					glActiveTexture(GL_TEXTURE0);
					glBindVertexArray(gBufferRenderObject->VAO);
					CheckGLErrorMessages();
					glBindBuffer(GL_ARRAY_BUFFER, gBufferRenderObject->VBO);
					CheckGLErrorMessages();
					glDrawArrays(gBufferRenderObject->topology, 0,
						(GLsizei)gBufferRenderObject->vertexBufferData->VertexCount);
					CheckGLErrorMessages();
					glBindVertexArray(0);
					CheckGLErrorMessages();
				}

				glDeleteTextures(1, &texHandle);

				metric->texCoord = metric->texCoord / glm::vec2((real)textureSize.x, (real)textureSize.y);

				FT_Bitmap_Done(ft, &alignedBitmap);
			}


			// Cleanup
			glDisable(GL_BLEND);

			FT_Done_Face(face);
			FT_Done_FreeType(ft);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			glViewport(0, 0,
					   gameContext.window->GetFrameBufferSize().x,
					   gameContext.window->GetFrameBufferSize().y);

			glDeleteRenderbuffers(1, &captureRBO);
			glDeleteFramebuffers(1, &captureFBO);

			CheckGLErrorMessages();



			// Initialize font shader things
			{
				GLMaterial& mat = m_Materials[m_FontMatID];
				GLShader& shader = m_Shaders[mat.material.shaderID];
				glUseProgram(shader.program);
				//m_uTransform = glGetUniformLocation(m_pTextShader->GetProgram(), "transform");
				//m_uTexSize = glGetUniformLocation(m_pTextShader->GetProgram(), "texSize");

				//u32 texLoc = glGetUniformLocation(shader.program, "in_Texture");
				//glUniform1i(texLoc, 0);

				glGenVertexArrays(1, &m_TextQuadVAO);
				glGenBuffers(1, &m_TextQuadVBO);
				CheckGLErrorMessages();


				glBindVertexArray(m_TextQuadVAO);
				glBindBuffer(GL_ARRAY_BUFFER, m_TextQuadVBO);
				CheckGLErrorMessages();

				//set data and attributes
				// TODO: ?
				i32 bufferSize = 50;
				glBufferData(GL_ARRAY_BUFFER, bufferSize, NULL, GL_DYNAMIC_DRAW);
				CheckGLErrorMessages();

				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);
				glEnableVertexAttribArray(3);
				glEnableVertexAttribArray(4);
				CheckGLErrorMessages();

				glVertexAttribPointer(0, (GLint)2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(TextVertex), (GLvoid*)offsetof(TextVertex, pos));
				glVertexAttribPointer(1, (GLint)2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(TextVertex), (GLvoid*)offsetof(TextVertex, uv));
				glVertexAttribPointer(2, (GLint)4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(TextVertex), (GLvoid*)offsetof(TextVertex, color));
				glVertexAttribPointer(3, (GLint)4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(TextVertex), (GLvoid*)offsetof(TextVertex, RGCharSize));
				glVertexAttribIPointer(4, (GLint)1, GL_INT, (GLsizei)sizeof(TextVertex), (GLvoid*)offsetof(TextVertex, channel));
				CheckGLErrorMessages();

				glBindBuffer(GL_ARRAY_BUFFER, 0);
				glBindVertexArray(0);
			}

			Logger::LogInfo("Rendered font atlas for " + fileName);

			return true;
		}

		void GLRenderer::SetFont(BitmapFont* font)
		{
			m_CurrentFont = font;
		}

		void GLRenderer::DrawString(const std::string& str, const glm::vec4& color, const glm::vec2& pos)
		{
			assert(m_CurrentFont != nullptr);

			//real scale = ((real)size) / m_CurrentFont->GetFontSize();
			m_CurrentFont->m_TextCache.push_back(TextCache(str, pos, color, 1.0f));
		}

		void GLRenderer::UpdateTextBuffer()
		{
			std::vector<TextVertex> textVertices;
			for (auto font : m_Fonts)
			{
				font->m_BufferStart = (i32)(textVertices.size());
				font->m_BufferSize = 0;

				for (i32 i = 0; i < font->m_TextCache.size(); ++i)
				{
					TextCache& currentCache = font->m_TextCache[i];
					std::string currentStr = currentCache.str;

					// TODO: Use kerning values
					//i32 fontSize = font->GetFontSize();
					real totalAdvanceX = 0;

					for (i32 j = 0; j < currentStr.length(); ++j)
					{
						char c = currentStr[j];

						if (BitmapFont::IsCharValid(c))
						{
							FontMetric* metric = font->GetMetric(c);
							if (metric->bIsValid)
							{
								if (c == ' ')
								{
									totalAdvanceX += metric->advanceX;
									continue;
								}

								glm::vec2 pos(currentCache.pos.x + (totalAdvanceX + metric->offsetX),
											  currentCache.pos.y + (metric->offsetY));

								glm::vec4 extraVec4(metric->width, metric->height, 0, 0);

								i32 texChannel = (i32)metric->channel;

								TextVertex vert{};
								vert.pos = pos;
								vert.uv = metric->texCoord;
								vert.color = currentCache.color;
								vert.RGCharSize = extraVec4;
								vert.channel = texChannel;

								textVertices.push_back(vert);

								totalAdvanceX += metric->advanceX;
							}
							else
							{
								Logger::LogWarning("Attempted to draw char with invalid metric: " + std::to_string(c) + " in font " + font->m_Name);
							}
						}
						else
						{
							Logger::LogWarning("Attempted to draw invalid char: " + std::to_string(c) + " in font " + font->m_Name);
						}
					}
				}

				font->m_BufferSize = (i32)(textVertices.size()) - font->m_BufferStart;
				font->m_TextCache.clear();
			}


			u32 bufferByteCount = (u32)(textVertices.size() * sizeof(TextVertex));

			glBindVertexArray(m_TextQuadVAO);
			glBindBuffer(GL_ARRAY_BUFFER, m_TextQuadVBO);
			CheckGLErrorMessages();
			glBufferData(GL_ARRAY_BUFFER, bufferByteCount, textVertices.data(), GL_DYNAMIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindVertexArray(0);
			CheckGLErrorMessages();
		}

		void GLRenderer::DrawRenderObjectBatch(const GameContext& gameContext, const std::vector<GLRenderObject*>& batchedRenderObjects, const DrawCallInfo& drawCallInfo)
		{
			assert(!batchedRenderObjects.empty());

			GLMaterial* material = &m_Materials[batchedRenderObjects[0]->materialID];
			GLShader* glShader = &m_Shaders[material->material.shaderID];
			Shader* shader = &glShader->shader;
			glUseProgram(glShader->program);
			CheckGLErrorMessages();

			for (size_t i = 0; i < batchedRenderObjects.size(); ++i)
			{
				GLRenderObject* renderObject = batchedRenderObjects[i];
				if (!renderObject->gameObject->IsVisible())
				{
					continue;
				}

				if (!renderObject->vertexBufferData)
				{
					Logger::LogError("Attempted to draw object which contains no vertex buffer data: " + renderObject->gameObject->GetName());
					return;
				}

				glBindVertexArray(renderObject->VAO);
				CheckGLErrorMessages();
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				CheckGLErrorMessages();

				if (renderObject->enableCulling)
				{
					glEnable(GL_CULL_FACE);

					glCullFace(renderObject->cullFace);
					CheckGLErrorMessages();
				}
				else
				{
					glDisable(GL_CULL_FACE);
				}

				glDepthFunc(renderObject->depthTestReadFunc);
				CheckGLErrorMessages();

				glDepthMask(renderObject->depthWriteEnable);
				CheckGLErrorMessages();

				UpdatePerObjectUniforms(renderObject->renderID, gameContext);

				BindTextures(shader, material);

				// TODO: OPTIMIZATION: Create DrawRenderObjectBatchToCubemap rather than check bool
				if (drawCallInfo.renderToCubemap && renderObject->gameObject->IsStatic())
				{
					GLRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
					GLMaterial* cubemapMaterial = &m_Materials[cubemapRenderObject->materialID];

					glm::vec2 cubemapSize = cubemapMaterial->material.cubemapSamplerSize;
					
					glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
					CheckGLErrorMessages();
					glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
					CheckGLErrorMessages();
					glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize.x, cubemapSize.y);
					CheckGLErrorMessages();
					
					glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);
					CheckGLErrorMessages();

					if (material->uniformIDs.projection == 0)
					{
						Logger::LogWarning("Attempted to draw object to cubemap but "
										   "uniformIDs.projection is not set! " + renderObject->gameObject->GetName());
						continue;
					}

					// Use capture projection matrix
					glUniformMatrix4fv(material->uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
					CheckGLErrorMessages();
					
					// TODO: Test if this is actually correct
					glm::vec3 cubemapTranslation = -cubemapRenderObject->gameObject->GetTransform()->GetWorldlPosition();
					for (size_t face = 0; face < 6; ++face)
					{
						glm::mat4 view = glm::translate(m_CaptureViews[face], cubemapTranslation);

						// This doesn't work because it flips the winding order of things (I think), maybe just account for that?
						// Flip vertically to match cubemap, cubemap shouldn't even be captured here eventually?
						//glm::mat4 view = glm::translate(glm::scale(m_CaptureViews[face], glm::vec3(1.0f, -1.0f, 1.0f)), cubemapTranslation);
						
						glUniformMatrix4fv(material->uniformIDs.view, 1, false, &view[0][0]);
						CheckGLErrorMessages();

						if (drawCallInfo.deferred)
						{
							constexpr i32 numBuffers = 3;
							u32 attachments[numBuffers] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
							glDrawBuffers(numBuffers, attachments);

							for (size_t j = 0; j < cubemapMaterial->cubemapSamplerGBuffersIDs.size(); ++j)
							{
								glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + j, 
									GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerGBuffersIDs[j].id, 0);
								CheckGLErrorMessages();
							}
						}
						else
						{
							glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
								GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerID, 0);
							CheckGLErrorMessages();
						}

						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 
							GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapDepthSamplerID, 0);
						CheckGLErrorMessages();

						// TODO: Move to translucent pass?
						if (shader->translucent)
						{
							glEnable(GL_BLEND);
							glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						}
						else
						{
							glDisable(GL_BLEND);
						}

						if (renderObject->indexed)
						{
							glDrawElements(renderObject->topology, (GLsizei)renderObject->indices->size(), 
								GL_UNSIGNED_INT, (void*)renderObject->indices->data());
							CheckGLErrorMessages();
						}
						else
						{
							glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
							CheckGLErrorMessages();
						}
					}
				}
				else
				{
					// renderToCubemap is false, just render normally

					// TODO: Move to translucent pass?
					if (shader->translucent)
					{
						glEnable(GL_BLEND);
						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					}
					else
					{
						glDisable(GL_BLEND);
					}

					if (renderObject->indexed)
					{
						glDrawElements(renderObject->topology, (GLsizei)renderObject->indices->size(),
							GL_UNSIGNED_INT, (void*)renderObject->indices->data());
						CheckGLErrorMessages();
					}
					else
					{
						glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
						CheckGLErrorMessages();
					}
				}

			}
		}

		u32 GLRenderer::BindTextures(Shader* shader, GLMaterial* glMaterial, u32 startingBinding)
		{
			Material* material = &glMaterial->material;

			struct Tex
			{
				bool needed;
				bool enabled;
				u32 textureID;
				GLenum target;
			};

			std::vector<Tex> textures;
			textures.push_back({ shader->needAlbedoSampler, material->enableAlbedoSampler, glMaterial->albedoSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needMetallicSampler, material->enableMetallicSampler, glMaterial->metallicSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needRoughnessSampler, material->enableRoughnessSampler, glMaterial->roughnessSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needAOSampler, material->enableAOSampler, glMaterial->aoSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needDiffuseSampler, material->enableDiffuseSampler, glMaterial->diffuseSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needNormalSampler, material->enableNormalSampler, glMaterial->normalSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needBRDFLUT, material->enableBRDFLUT, glMaterial->brdfLUTSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needIrradianceSampler, material->enableIrradianceSampler, glMaterial->irradianceSamplerID, GL_TEXTURE_CUBE_MAP });
			textures.push_back({ shader->needPrefilteredMap, material->enablePrefilteredMap, glMaterial->prefilteredMapSamplerID, GL_TEXTURE_CUBE_MAP });
			textures.push_back({ shader->needCubemapSampler, material->enableCubemapSampler, glMaterial->cubemapSamplerID, GL_TEXTURE_CUBE_MAP });

			u32 binding = startingBinding;
			for (Tex& tex : textures)
			{
				if (tex.needed)
				{
					if (tex.enabled)
					{

						GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)binding);
						glActiveTexture(activeTexture);
						glBindTexture(tex.target, (GLuint)tex.textureID);
						CheckGLErrorMessages();
					}
					++binding;
				}
			}

			return binding;
		}

		u32 GLRenderer::BindFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding)
		{
			Material* material = &glMaterial->material;

			struct Tex
			{
				bool needed;
				bool enabled;
				u32 textureID;
			};

			if (material->frameBuffers.empty())
			{
				Logger::LogWarning("Attempted to bind frame buffers on material that doesn't contain any framebuffers!");
				return startingBinding;
			}

			u32 binding = startingBinding;
			for (auto& frameBuffer : material->frameBuffers)
			{
				GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)binding);
				glActiveTexture(activeTexture);
				glBindTexture(GL_TEXTURE_2D, *((GLuint*)frameBuffer.second));
				CheckGLErrorMessages();
				++binding;
			}

			return binding;
		}

		u32 GLRenderer::BindDeferredFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding)
		{
			struct Tex
			{
				bool needed;
				bool enabled;
				u32 textureID;
			};

			if (glMaterial->cubemapSamplerGBuffersIDs.empty())
			{
				Logger::LogWarning("Attempted to bind gbuffer samplers on material doesn't contain any gbuffer samplers!");
				return startingBinding;
			}

			u32 binding = startingBinding;
			for (auto& cubemapGBuffer : glMaterial->cubemapSamplerGBuffersIDs)
			{
				GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)binding);
				glActiveTexture(activeTexture);
				glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapGBuffer.id);
				CheckGLErrorMessages();
				++binding;
			}

			return binding;
		}

		void GLRenderer::CreateOffscreenFrameBuffer(u32* FBO, u32* RBO, const glm::vec2i& size, FrameBufferHandle& handle)
		{
			glGenFramebuffers(1, FBO);
			glBindFramebuffer(GL_FRAMEBUFFER, *FBO);
			CheckGLErrorMessages();

			glGenRenderbuffers(1, RBO);
			glBindRenderbuffer(GL_RENDERBUFFER, *RBO);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size.x, size.y);
			CheckGLErrorMessages();
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *RBO);
			CheckGLErrorMessages();

			GenerateFrameBufferTexture(&handle.id,
									   0,
									   handle.internalFormat,
									   handle.format,
									   handle.type,
									   size);

			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				Logger::LogError("Offscreen frame buffer is incomplete!");
			}

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			CheckGLErrorMessages();
		}

		bool GLRenderer::GetLoadedTexture(const std::string& filePath, u32& handle)
		{
			auto location = m_LoadedTextures.find(filePath);
			if (location == m_LoadedTextures.end())
			{
				return false;
			}
			else
			{
				handle = location->second;
				return true;
			}
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
				{ "deferred_simple", RESOURCE_LOCATION + "shaders/deferred_simple.vert", RESOURCE_LOCATION + "shaders/deferred_simple.frag" },
				{ "deferred_combine", RESOURCE_LOCATION + "shaders/deferred_combine.vert", RESOURCE_LOCATION + "shaders/deferred_combine.frag" },
				{ "deferred_combine_cubemap", RESOURCE_LOCATION + "shaders/deferred_combine_cubemap.vert", RESOURCE_LOCATION + "shaders/deferred_combine_cubemap.frag" },
				{ "color", RESOURCE_LOCATION + "shaders/color.vert", RESOURCE_LOCATION + "shaders/color.frag" },
				{ "pbr", RESOURCE_LOCATION + "shaders/pbr.vert", RESOURCE_LOCATION + "shaders/pbr.frag" },
				{ "skybox", RESOURCE_LOCATION + "shaders/skybox.vert", RESOURCE_LOCATION + "shaders/skybox.frag" },
				{ "equirectangular_to_cube", RESOURCE_LOCATION + "shaders/skybox.vert", RESOURCE_LOCATION + "shaders/equirectangular_to_cube.frag" },
				{ "irradiance", RESOURCE_LOCATION + "shaders/skybox.vert", RESOURCE_LOCATION + "shaders/irradiance.frag" },
				{ "prefilter", RESOURCE_LOCATION + "shaders/skybox.vert", RESOURCE_LOCATION + "shaders/prefilter.frag" },
				{ "brdf", RESOURCE_LOCATION + "shaders/brdf.vert", RESOURCE_LOCATION + "shaders/brdf.frag" },
				{ "background", RESOURCE_LOCATION + "shaders/background.vert", RESOURCE_LOCATION + "shaders/background.frag" },
				{ "sprite", RESOURCE_LOCATION + "shaders/sprite.vert", RESOURCE_LOCATION + "shaders/sprite.frag" },
				{ "post_process", RESOURCE_LOCATION + "shaders/post_process.vert", RESOURCE_LOCATION + "shaders/post_process.frag" },
				{ "post_fxaa", RESOURCE_LOCATION + "shaders/post_fxaa.vert", RESOURCE_LOCATION + "shaders/post_fxaa.frag" },
				{ "compute_sdf", RESOURCE_LOCATION + "shaders/ComputeSDF.vert", RESOURCE_LOCATION + "shaders/ComputeSDF.frag" },
				{ "font", RESOURCE_LOCATION + "shaders/font.vert", RESOURCE_LOCATION + "shaders/font.frag",  RESOURCE_LOCATION + "shaders/font.geom" },
			};

			ShaderID shaderID = 0;

			// TOOD: Determine this info automatically when parsing shader code

			// Deferred Simple
			m_Shaders[shaderID].shader.deferred = true;
			m_Shaders[shaderID].shader.needDiffuseSampler = true;
			m_Shaders[shaderID].shader.needNormalSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("modelInvTranspose");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableDiffuseSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableNormalSampler");
			++shaderID;

			// Deferred combine
			m_Shaders[shaderID].shader.deferred = false; // Sounds strange but this isn't deferred
			// m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.depthWriteEnable = false; // Disable depth writing
			m_Shaders[shaderID].shader.needBRDFLUT = true;
			m_Shaders[shaderID].shader.needIrradianceSampler = true;
			m_Shaders[shaderID].shader.needPrefilteredMap = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("camPos");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("pointLights");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("dirLight");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("irradianceSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("prefilterMap");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("brdfLUT");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("positionMetallicFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("normalRoughnessFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("albedoAOFrameBufferSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableIrradianceSampler");
			++shaderID;

			// Deferred combine cubemap
			m_Shaders[shaderID].shader.deferred = false; // Sounds strange but this isn't deferred
			// m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.depthWriteEnable = false; // Disable depth writing
			m_Shaders[shaderID].shader.needBRDFLUT = true;
			m_Shaders[shaderID].shader.needIrradianceSampler = true;
			m_Shaders[shaderID].shader.needPrefilteredMap = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("camPos");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("pointLights");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("dirLight");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("irradianceSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("prefilterMap");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("brdfLUT");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("positionMetallicFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("normalRoughnessFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("albedoAOFrameBufferSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableIrradianceSampler");
			++shaderID;

			// Color
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.translucent = true;
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("colorMultiplier");
			++shaderID;

			// PBR
			m_Shaders[shaderID].shader.deferred = true;
			m_Shaders[shaderID].shader.needAlbedoSampler = true;
			m_Shaders[shaderID].shader.needMetallicSampler = true;
			m_Shaders[shaderID].shader.needRoughnessSampler = true;
			m_Shaders[shaderID].shader.needAOSampler = true;
			m_Shaders[shaderID].shader.needNormalSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constAlbedo");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableAlbedoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("albedoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constMetallic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableMetallicSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("metallicSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constRoughness");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableRoughnessSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("roughnessSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableAOSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constAO");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("aoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableNormalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("normalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Skybox
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableCubemapSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("cubemapSampler");
			++shaderID;

			// Equirectangular to Cube
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needHDREquirectangularSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("hdrEquirectangularSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Irradiance
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Prefilter
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// BRDF
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Background
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Sprite
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("transformMat");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("colorMultiplier");
			++shaderID;

			// Post processing
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("transformMat");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("colorMultiplier");
			++shaderID;

			// Post FXAA (Fast approximate anti-aliasing)
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("lumaThresholdMin");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("lumaThresholdMax");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("mulReduce");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("minReduce");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("maxSpan");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("texelStep");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("bDEBUGShowEdges");

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Compute SDF
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			// TODO: Move some of these to constant buffer
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("charResolution");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("spread");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("highResTex");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("texChannel");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("sdfResolution");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("highRes");
			++shaderID;

			// Font
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("transformMat");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("texSize");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("threshold");
			//m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("outline");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("soften");
			++shaderID;

			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				m_Shaders[i].program = glCreateProgram();
				CheckGLErrorMessages();

				if (!LoadGLShaders(m_Shaders[i].program, m_Shaders[i]))
				{
					std::string fileNames = m_Shaders[i].shader.vertexShaderFilePath + " & " + m_Shaders[i].shader.fragmentShaderFilePath;
					if (!m_Shaders[i].shader.geometryShaderFilePath.empty())
					{
						fileNames += " & " + m_Shaders[i].shader.geometryShaderFilePath;
					}
					Logger::LogError("Couldn't load/compile shaders: " + fileNames);
				}

				LinkProgram(m_Shaders[i].program);
			}

			CheckGLErrorMessages();
		}

		void GLRenderer::UpdateMaterialUniforms(const GameContext& gameContext, MaterialID materialID)
		{
			GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);

			GLMaterial* material = &m_Materials[materialID];
			GLShader* shader = &m_Shaders[material->material.shaderID];
			
			glUseProgram(shader->program);

			glm::mat4 proj = gameContext.cameraManager->CurrentCamera()->GetProjection();
			glm::mat4 view = gameContext.cameraManager->CurrentCamera()->GetView();
			glm::mat4 viewInv = glm::inverse(view);
			glm::mat4 viewProj = proj * view;
			glm::vec4 camPos = glm::vec4(gameContext.cameraManager->CurrentCamera()->GetPosition(), 0.0f);


			if (shader->shader.constantBufferUniforms.HasUniform("view"))
			{
				glUniformMatrix4fv(material->uniformIDs.view, 1, false, &view[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("viewInv"))
			{
				glUniformMatrix4fv(material->uniformIDs.viewInv, 1, false, &viewInv[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("projection"))
			{
				glUniformMatrix4fv(material->uniformIDs.projection, 1, false, &proj[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("viewProjection"))
			{
				glUniformMatrix4fv(material->uniformIDs.viewProjection, 1, false, &viewProj[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("camPos"))
			{
				glUniform4f(material->uniformIDs.camPos,
					camPos.x,
					camPos.y,
					camPos.z,
					camPos.w);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("dirLight"))
			{
				if (m_DirectionalLight.enabled)
				{
					SetUInt(material->material.shaderID, "dirLight.enabled", 1);
					CheckGLErrorMessages();
					SetVec4f(material->material.shaderID, "dirLight.direction", m_DirectionalLight.direction);
					CheckGLErrorMessages();
					SetVec4f(material->material.shaderID, "dirLight.color", m_DirectionalLight.color * m_DirectionalLight.brightness);
					CheckGLErrorMessages();
				}
				else
				{
					SetUInt(material->material.shaderID, "dirLight.enabled", 0);
					CheckGLErrorMessages();
				}
			}

			if (shader->shader.constantBufferUniforms.HasUniform("pointLights"))
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

						SetVec4f(material->material.shaderID, "pointLights[" + numberStr + "].color", m_PointLights[i].color * m_PointLights[i].brightness);
						CheckGLErrorMessages();
					}
					else
					{
						SetUInt(material->material.shaderID, "pointLights[" + numberStr + "].enabled", 0);
						CheckGLErrorMessages();
					}
				}
			}

			if (shader->shader.constantBufferUniforms.HasUniform("texelStep"))
			{
				glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
				glm::vec2 texelStep(1.0f / frameBufferSize.x, 1.0f / frameBufferSize.y);
				SetVec2f(material->material.shaderID, "texelStep", texelStep);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("bDEBUGShowEdges"))
			{
				SetInt(material->material.shaderID, "bDEBUGShowEdges", m_bEnableFXAADEBUGShowEdges ? 1 : 0);
				CheckGLErrorMessages();
			}

			glUseProgram(last_program);
			CheckGLErrorMessages();
		}

		void GLRenderer::UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject)
			{
				Logger::LogError("Invalid renderID passed to UpdatePerObjectUniforms: " + std::to_string(renderID));
				return;
			}

			glm::mat4 model = renderObject->gameObject->GetTransform()->GetWorldTransform();
			UpdatePerObjectUniforms(renderObject->materialID, model, gameContext);
		}

		void GLRenderer::UpdatePerObjectUniforms(MaterialID materialID, const glm::mat4& model, const GameContext& gameContext)
		{
			// TODO: OPTIMIZATION: Investigate performance impact of caching each uniform and preventing updates to data that hasn't changed

			GLMaterial* material = &m_Materials[materialID];
			GLShader* shader = &m_Shaders[material->material.shaderID];

			glm::mat4 modelInv = glm::inverse(model);
			glm::mat4 proj = gameContext.cameraManager->CurrentCamera()->GetProjection();
			glm::mat4 view = gameContext.cameraManager->CurrentCamera()->GetView();
			glm::mat4 MVP = proj * view * model;
			glm::vec4 colorMultiplier = material->material.colorMultiplier;

			// TODO: Use set functions here (SetFloat, SetMatrix, ...)
			if (shader->shader.dynamicBufferUniforms.HasUniform("model"))
			{
				glUniformMatrix4fv(material->uniformIDs.model, 1, false, &model[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("modelInvTranspose"))
			{
				// OpenGL will transpose for us if we set the third param to true
				glUniformMatrix4fv(material->uniformIDs.modelInvTranspose, 1, true, &modelInv[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("colorMultiplier"))
			{
				// OpenGL will transpose for us if we set the third param to true
				glUniform4fv(material->uniformIDs.colorMultiplier, 1, &colorMultiplier[0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableDiffuseSampler"))
			{
				glUniform1i(material->uniformIDs.enableDiffuseTexture, material->material.enableDiffuseSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableNormalSampler"))
			{
				glUniform1i(material->uniformIDs.enableNormalTexture, material->material.enableNormalSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableCubemapSampler"))
			{
				glUniform1i(material->uniformIDs.enableCubemapTexture, material->material.enableCubemapSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableAlbedoSampler"))
			{
				glUniform1ui(material->uniformIDs.enableAlbedoSampler, material->material.enableAlbedoSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constAlbedo"))
			{
				glUniform4f(material->uniformIDs.constAlbedo, material->material.constAlbedo.x, material->material.constAlbedo.y, material->material.constAlbedo.z, 0);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableMetallicSampler"))
			{
				glUniform1ui(material->uniformIDs.enableMetallicSampler, material->material.enableMetallicSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constMetallic"))
			{
				glUniform1f(material->uniformIDs.constMetallic, material->material.constMetallic);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableRoughnessSampler"))
			{
				glUniform1ui(material->uniformIDs.enableRoughnessSampler, material->material.enableRoughnessSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constRoughness"))
			{
				glUniform1f(material->uniformIDs.constRoughness, material->material.constRoughness);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableAOSampler"))
			{
				glUniform1ui(material->uniformIDs.enableAOSampler, material->material.enableAOSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constAO"))
			{
				glUniform1f(material->uniformIDs.constAO, material->material.constAO);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableIrradianceSampler"))
			{
				glUniform1i(material->uniformIDs.enableIrradianceSampler, material->material.enableIrradianceSampler);
				CheckGLErrorMessages();
			}
		}

		void GLRenderer::OnWindowSizeChanged(i32 width, i32 height)
		{
			if (width == 0 || height == 0 || m_gBufferHandle == 0)
			{
				return;
			}

			glViewport(0, 0, width, height);
			CheckGLErrorMessages();

			const glm::vec2i newFrameBufferSize(width, height);

			glBindFramebuffer(GL_FRAMEBUFFER, m_Offscreen0FBO);
			CheckGLErrorMessages();
			ResizeFrameBufferTexture(m_OffscreenTexture0Handle.id,
									 m_OffscreenTexture0Handle.internalFormat,
									 m_OffscreenTexture0Handle.format,
									 m_OffscreenTexture0Handle.type,
									 newFrameBufferSize);

			ResizeRenderBuffer(m_Offscreen0RBO, newFrameBufferSize);


			glBindFramebuffer(GL_FRAMEBUFFER, m_Offscreen1FBO);
			CheckGLErrorMessages();
			ResizeFrameBufferTexture(m_OffscreenTexture1Handle.id,
									 m_OffscreenTexture1Handle.internalFormat,
									 m_OffscreenTexture1Handle.format,
									 m_OffscreenTexture1Handle.type,
									 newFrameBufferSize);

			ResizeRenderBuffer(m_Offscreen1RBO, newFrameBufferSize);


			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);
			CheckGLErrorMessages();
			ResizeFrameBufferTexture(m_gBuffer_PositionMetallicHandle.id,
				m_gBuffer_PositionMetallicHandle.internalFormat,
				m_gBuffer_PositionMetallicHandle.format,
				m_gBuffer_PositionMetallicHandle.type,
				newFrameBufferSize);

			ResizeFrameBufferTexture(m_gBuffer_NormalRoughnessHandle.id,
				m_gBuffer_NormalRoughnessHandle.internalFormat,
				m_gBuffer_NormalRoughnessHandle.format,
				m_gBuffer_NormalRoughnessHandle.type,
				newFrameBufferSize);

			ResizeFrameBufferTexture(m_gBuffer_DiffuseAOHandle.id,
				m_gBuffer_DiffuseAOHandle.internalFormat,
				m_gBuffer_DiffuseAOHandle.format,
				m_gBuffer_DiffuseAOHandle.type,
				newFrameBufferSize);

			ResizeRenderBuffer(m_gBufferDepthHandle, newFrameBufferSize);
		}

		bool GLRenderer::GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo)
		{
			outInfo = {};

			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject)
			{
				return false;
			}

			outInfo.materialID = renderObject->materialID;
			outInfo.vertexBufferData = renderObject->vertexBufferData;
			outInfo.indices = renderObject->indices;
			outInfo.gameObject = renderObject->gameObject;
			outInfo.visible = renderObject->gameObject->IsVisible();
			outInfo.visibleInSceneExplorer = renderObject->gameObject->IsVisibleInSceneExplorer();
			outInfo.cullFace = GLCullFaceToCullFace(renderObject->cullFace);
			outInfo.enableCulling = renderObject->enableCulling;
			outInfo.depthTestReadFunc = GlenumToDepthTestFunc(renderObject->depthTestReadFunc);
			outInfo.depthWriteEnable = renderObject->depthWriteEnable;

			return true;
		}

		void GLRenderer::SetVSyncEnabled(bool enableVSync)
		{
			m_VSyncEnabled = enableVSync;
			glfwSwapInterval(enableVSync ? 1 : 0);
			CheckGLErrorMessages();
		}

		bool GLRenderer::GetVSyncEnabled()
		{
			return m_VSyncEnabled;
		}

		void GLRenderer::SetFloat(ShaderID shaderID, const std::string& valName, real val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName.c_str());
			if (location == -1)
			{
				Logger::LogWarning("Float " + valName + " couldn't be found!");
			}
			CheckGLErrorMessages();

			glUniform1f(location, val);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetInt(ShaderID shaderID, const std::string & valName, i32 val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName.c_str());
			if (location == -1)
			{
				Logger::LogWarning("i32 " + valName + " couldn't be found!");
			}
			CheckGLErrorMessages();

			glUniform1i(location, val);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetUInt(ShaderID shaderID, const std::string& valName, u32 val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName.c_str());
			if (location == -1)
			{
				Logger::LogWarning("u32 " + valName + " couldn't be found!");
			}
			CheckGLErrorMessages();

			glUniform1ui(location, val);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec2f(ShaderID shaderID, const std::string& vecName, const glm::vec2& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1)
			{
				Logger::LogWarning("Vec2f " + vecName + " couldn't be found!");
			}
			CheckGLErrorMessages();

			glUniform2f(location, vec[0], vec[1]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec3f(ShaderID shaderID, const std::string& vecName, const glm::vec3& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1)
			{
				Logger::LogWarning("Vec3f " + vecName + " couldn't be found!");
			}
			CheckGLErrorMessages();

			glUniform3f(location, vec[0], vec[1], vec[2]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec4f(ShaderID shaderID, const std::string& vecName, const glm::vec4& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1)
			{
				Logger::LogWarning("Vec4f " + vecName + " couldn't be found!");
			}
			CheckGLErrorMessages();

			glUniform4f(location, vec[0], vec[1], vec[2], vec[3]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetMat4f(ShaderID shaderID, const std::string& matName, const glm::mat4& mat)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, matName.c_str());
			if (location == -1)
			{
				Logger::LogWarning("Mat4f " + matName + " couldn't be found!");
			}
			CheckGLErrorMessages();

			glUniformMatrix4fv(location, 1, false, &mat[0][0]);
			CheckGLErrorMessages();
		}


		void GLRenderer::GenerateGBufferVertexBuffer()
		{
			if (m_gBufferQuadVertexBufferData.pDataStart == nullptr)
			{
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

				gBufferQuadVertexBufferDataCreateInfo.attributes = (u32)VertexAttribute::POSITION | (u32)VertexAttribute::UV;

				m_gBufferQuadVertexBufferData.Initialize(&gBufferQuadVertexBufferDataCreateInfo);
			}
		}

		void GLRenderer::GenerateGBuffer(const GameContext& gameContext)
		{
			if (!m_gBufferQuadVertexBufferData.pDataStart)
			{
				GenerateGBufferVertexBuffer();
			}

			// TODO: Allow user to not set this and have a backup plan (disable deferred rendering?)
			assert(m_ReflectionProbeMaterialID != InvalidMaterialID);

			MaterialCreateInfo gBufferMaterialCreateInfo = {};
			gBufferMaterialCreateInfo.name = "GBuffer material";
			gBufferMaterialCreateInfo.shaderName = "deferred_combine";
			gBufferMaterialCreateInfo.enableIrradianceSampler = true;
			gBufferMaterialCreateInfo.irradianceSamplerMatID = m_ReflectionProbeMaterialID;
			gBufferMaterialCreateInfo.enablePrefilteredMap = true;
			gBufferMaterialCreateInfo.prefilterMapSamplerMatID = m_ReflectionProbeMaterialID;
			gBufferMaterialCreateInfo.enableBRDFLUT = true;
			gBufferMaterialCreateInfo.engineMaterial = true;
			gBufferMaterialCreateInfo.frameBuffers = {
				{ "positionMetallicFrameBufferSampler",  &m_gBuffer_PositionMetallicHandle.id },
				{ "normalRoughnessFrameBufferSampler",  &m_gBuffer_NormalRoughnessHandle.id },
				{ "albedoAOFrameBufferSampler",  &m_gBuffer_DiffuseAOHandle.id },
			};

			MaterialID gBufferMatID = InitializeMaterial(gameContext, &gBufferMaterialCreateInfo);


			GameObject* gBufferQuadGameObject = new GameObject("GBuffer Quad", GameObjectType::NONE);
			m_PersistentObjects.push_back(gBufferQuadGameObject);
			// Don't render the g buffer normally, we'll handle it separately
			gBufferQuadGameObject->SetVisible(false);

			RenderObjectCreateInfo gBufferQuadCreateInfo = {};
			gBufferQuadCreateInfo.materialID = gBufferMatID;
			gBufferQuadCreateInfo.gameObject = gBufferQuadGameObject;
			gBufferQuadCreateInfo.vertexBufferData = &m_gBufferQuadVertexBufferData;
			gBufferQuadCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS; // Ignore previous depth values
			gBufferQuadCreateInfo.depthWriteEnable = false; // Don't write GBuffer quad to depth buffer
			gBufferQuadCreateInfo.visibleInSceneExplorer = false;

			m_GBufferQuadRenderID = InitializeRenderObject(gameContext, &gBufferQuadCreateInfo);

			m_gBufferQuadVertexBufferData.DescribeShaderVariables(this, m_GBufferQuadRenderID);

			GLRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);
		}

		u32 GLRenderer::GetRenderObjectCount() const
		{
			// TODO: Replace function with m_RenderObjects.size()? (only if no nullptr objects exist)
			u32 count = 0;

			for (auto renderObject : m_RenderObjects)
			{
				if (renderObject)
				{
					++count;
				}
			}

			return count;
		}

		u32 GLRenderer::GetRenderObjectCapacity() const
		{
			return m_RenderObjects.capacity();
		}

		u32 GLRenderer::GetActiveRenderObjectCount() const
		{
			u32 capacity = 0;
			for (auto renderObject : m_RenderObjects)
			{
				if (renderObject)
				{
					++capacity;
				}
			}
			return capacity;
		}

		void GLRenderer::DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size, DataType dataType, bool normalized, i32 stride, void* pointer)
		{
			GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);

			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject)
			{
				Logger::LogError("Invalid renderID passed to DescribeShaderVariable: " + std::to_string(renderID));
				return;
			}

			GLMaterial* material = &m_Materials[renderObject->materialID];
			u32 program = m_Shaders[material->material.shaderID].program;


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

			GLenum glRenderType = DataTypeToGLType(dataType);
			glVertexAttribPointer((GLuint)location, size, glRenderType, (GLboolean)normalized, stride, pointer);
			CheckGLErrorMessages();

			glBindVertexArray(0);

			glUseProgram(last_program);
		}

		void GLRenderer::SetSkyboxMesh(GameObject* skyboxMesh)
		{
			m_SkyBoxMesh = skyboxMesh;

			if (skyboxMesh == nullptr)
			{
				return;
			}

			MaterialID skyboxMaterialID = m_SkyBoxMesh->GetMeshComponent()->GetMaterialID();
			if (skyboxMaterialID == InvalidMaterialID)
			{
				Logger::LogError("Skybox doesn't have a valid material! Irradiance textures can't be generated");
				return;
			}

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				GLRenderObject* renderObject = GetRenderObject(i);
				if (renderObject)
				{
					if (m_Materials.find(renderObject->materialID) == m_Materials.end())
					{
						Logger::LogError("Render object contains invalid material ID: " + 
										 std::to_string(renderObject->materialID) + 
										 ", material name: " + renderObject->materialName);
					}
					else
					{
						GLMaterial& material = m_Materials[renderObject->materialID];
						GLShader& shader = m_Shaders[material.material.shaderID];
						if (shader.shader.needPrefilteredMap)
						{
							material.irradianceSamplerID = m_Materials[skyboxMaterialID].irradianceSamplerID;
							material.prefilteredMapSamplerID = m_Materials[skyboxMaterialID].prefilteredMapSamplerID;
						}
					}
				}
			}
		}

		GameObject* GLRenderer::GetSkyboxMesh()
		{
			return m_SkyBoxMesh;
		}

		void GLRenderer::SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject)
			{
				renderObject->materialID = materialID;
			}
			else
			{
				Logger::LogError("SetRenderObjectMaterialID couldn't find render object with ID " + std::to_string(renderID));
			}
		}

		Material& GLRenderer::GetMaterial(MaterialID matID)
		{
			return m_Materials[matID].material;
		}

		Shader& GLRenderer::GetShader(ShaderID shaderID)
		{
			return m_Shaders[shaderID].shader;
		}

		void GLRenderer::DestroyRenderObject(RenderID renderID)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			DestroyRenderObject(renderID, renderObject);
		}

		void GLRenderer::DestroyRenderObject(RenderID renderID, GLRenderObject* renderObject)
		{
			if (renderObject)
			{
				glDeleteBuffers(1, &renderObject->VBO);
				if (renderObject->indexed)
				{
					glDeleteBuffers(1, &renderObject->IBO);
				}

				renderObject->gameObject = nullptr;
				SafeDelete(renderObject);
			}

			m_RenderObjects[renderID] = nullptr;
		}

		void GLRenderer::NewFrame()
		{
			if (m_PhysicsDebugDrawer)
			{
				m_PhysicsDebugDrawer->ClearLines();
			}
			ImGui_ImplGlfwGL3_NewFrame();
		}

		btIDebugDraw* GLRenderer::GetDebugDrawer()
		{
			return m_PhysicsDebugDrawer;
		}

		void GLRenderer::ImGuiRender()
		{
			ImGui::Render();
			ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
		}

		void GLRenderer::PhysicsDebugRender(const GameContext& gameContext)
		{
			btDiscreteDynamicsWorld* physicsWorld = gameContext.sceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			physicsWorld->debugDrawWorld();
		}

		void GLRenderer::DrawImGuiItems(const GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);

			if (ImGui::CollapsingHeader("Scene info"))
			{
				//const u32 objectCount = GetRenderObjectCount();
				//const u32 objectCapacity = GetRenderObjectCapacity();
				//const std::string objectCountStr("Render object count/capacity: " + std::to_string//(objectCount) + "/" + std::to_string(objectCapacity));
				//ImGui::Text(objectCountStr.c_str());

				if (ImGui::TreeNode("Render Objects"))
				{
					std::vector<GameObject*>& rootObjects = gameContext.sceneManager->CurrentScene()->GetRootObjects();
					for (size_t i = 0; i < rootObjects.size(); ++i)
					{
						DrawImGuiForGameObjectAndChildren(rootObjects[i]);
					}

					ImGui::TreePop();
				}

				DrawImGuiLights();
			}
		}

		void GLRenderer::DrawImGuiForGameObjectAndChildren(GameObject* gameObject)
		{
			RenderID renderID = gameObject->GetRenderID();
			GLRenderObject* renderObject = nullptr;
			std::string objectName;
			if (renderID != InvalidRenderID)
			{
				renderObject = GetRenderObject(renderID);
				objectName = std::string(gameObject->GetName() + "##" + std::to_string(renderObject->renderID));

				if (!gameObject->IsVisibleInSceneExplorer())
				{
					return;
				}
			}
			else
			{
				// TODO: FIXME: This will fail if multiple objects share the same name
				// and have no valid RenderID. Add "##UID" to end of string to ensure uniqueness
				objectName = std::string(gameObject->GetName());
			}

			const std::string objectID("##" + objectName + "-visible");
			bool visible = gameObject->IsVisible();
			if (ImGui::Checkbox(objectID.c_str(), &visible))
			{
				gameObject->SetVisible(visible);
			}
			ImGui::SameLine();
			if (ImGui::TreeNode(objectName.c_str()))
			{
				if (renderObject)
				{
					const std::string renderIDStr = "renderID: " + std::to_string(renderObject->renderID);
					ImGui::TextUnformatted(renderIDStr.c_str());
				}

				DrawImGuiForRenderObjectCommon(gameObject);

				if (renderObject)
				{
					GLMaterial& material = m_Materials[renderObject->materialID];
					GLShader& shader = m_Shaders[material.material.shaderID];

					std::string matNameStr = "Material: " + material.material.name;
					std::string shaderNameStr = "Shader: " + shader.shader.name;
					ImGui::TextUnformatted(matNameStr.c_str());
					ImGui::TextUnformatted(shaderNameStr.c_str());

					if (material.uniformIDs.enableIrradianceSampler)
					{
						ImGui::Checkbox("Enable Irradiance Sampler", &material.material.enableIrradianceSampler);
					}
				}

				ImGui::Indent();
				const std::vector<GameObject*>& children = gameObject->GetChildren();
				for (GameObject* child : children)
				{
					DrawImGuiForGameObjectAndChildren(child);
				}
				ImGui::Unindent();

				ImGui::TreePop();
			}
		}

		void GLRenderer::UpdateRenderObjectVertexData(RenderID renderID)
		{
			UNREFERENCED_PARAMETER(renderID);
			// TODO: IMPLEMENT: UNIMPLEMENTED:
		}

		GLRenderObject* GLRenderer::GetRenderObject(RenderID renderID)
		{
#if _DEBUG
			if (renderID > m_RenderObjects.size() ||
				renderID == InvalidRenderID)
			{
				Logger::LogError("Invalid renderID passed to GetRenderObject: " + std::to_string(renderID));
				return nullptr;
			}
#endif

			return m_RenderObjects[renderID];
		}

		void GLRenderer::InsertNewRenderObject(GLRenderObject* renderObject)
		{
			if (renderObject->renderID < m_RenderObjects.size())
			{
				assert(m_RenderObjects[renderObject->renderID] == nullptr);
				m_RenderObjects[renderObject->renderID] = renderObject;
			}
			else
			{
				m_RenderObjects.resize(renderObject->renderID + 1);
				m_RenderObjects[renderObject->renderID] = renderObject;
			}
		}

		MaterialID GLRenderer::GetNextAvailableMaterialID()
		{
			// Return lowest available ID
			MaterialID result = 0;
			while (m_Materials.find(result) != m_Materials.end())
			{
				++result;
			}
			return result;
		}

		RenderID GLRenderer::GetNextAvailableRenderID() const
		{
			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				if (m_RenderObjects[i] == nullptr)
				{
					return i;
				}
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
