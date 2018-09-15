#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLRenderer.hpp"

#include <array>
#include <algorithm>
#include <string>
#include <utility>
#include <functional>

#pragma warning(push, 0)
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "imgui.h"
#include "ImGui/imgui_impl_glfw_gl3.h"
// TODO: Remove?
#include "imgui_internal.h"

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <freetype/ftbitmap.h>
#pragma warning(pop)

#include "Cameras/CameraManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/GL/GLHelpers.hpp"
#include "Graphics/GL/GLPhysicsDebugDraw.hpp"
#include "Helpers.hpp"
#include "JSONParser.hpp"
#include "JSONTypes.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Profiler.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Time.hpp"
#include "VertexAttribute.hpp"
#include "Window/GLFWWindowWrapper.hpp"
#include "Window/Monitor.hpp"
#include "Window/Window.hpp"

#pragma warning(push, 0)
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#pragma warning(pop)

namespace flex
{
	namespace gl
	{
		GLRenderer::GLRenderer()
		{
			m_DefaultSettingsFilePathAbs = RelativePathToAbsolute(RESOURCE_LOCATION + std::string("config/default-renderer-settings.ini"));
			m_SettingsFilePathAbs = RelativePathToAbsolute(RESOURCE_LOCATION + std::string("config/renderer-settings.ini"));

			g_Renderer = this;
		}

		GLRenderer::~GLRenderer()
		{
			
		}

		void GLRenderer::Initialize()
		{
			LoadSettingsFromDisk();

			m_OffscreenTexture0Handle = {};
			m_OffscreenTexture0Handle.internalFormat = GL_RGBA16F;
			m_OffscreenTexture0Handle.format = GL_RGBA;
			m_OffscreenTexture0Handle.type = GL_FLOAT;

			m_OffscreenTexture1Handle = {};
			m_OffscreenTexture1Handle.internalFormat = GL_RGBA16F;
			m_OffscreenTexture1Handle.format = GL_RGBA;
			m_OffscreenTexture1Handle.type = GL_FLOAT;


			m_gBuffer_PositionMetallicHandle = {};
			m_gBuffer_PositionMetallicHandle.internalFormat = GL_RGBA16F;
			m_gBuffer_PositionMetallicHandle.format = GL_RGBA;
			m_gBuffer_PositionMetallicHandle.type = GL_FLOAT;

			m_gBuffer_NormalRoughnessHandle = {};
			m_gBuffer_NormalRoughnessHandle.internalFormat = GL_RGBA16F;
			m_gBuffer_NormalRoughnessHandle.format = GL_RGBA;
			m_gBuffer_NormalRoughnessHandle.type = GL_FLOAT;

			m_gBuffer_DiffuseAOHandle = {};
			m_gBuffer_DiffuseAOHandle.internalFormat = GL_RGBA16F;
			m_gBuffer_DiffuseAOHandle.format = GL_RGBA;
			m_gBuffer_DiffuseAOHandle.type = GL_FLOAT;

			LoadShaders();

			glEnable(GL_DEPTH_TEST);
			//glDepthFunc(GL_LEQUAL);

			glFrontFace(GL_CCW);

			// Prevent seams from appearing on lower mip map levels of cubemaps
			glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

			// Capture framebuffer (TODO: Merge with offscreen frame buffer?)
			{
				glGenFramebuffers(1, &m_CaptureFBO);
				glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
				

				glGenRenderbuffers(1, &m_CaptureRBO);
				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				glRenderbufferStorage(GL_RENDERBUFFER, m_CaptureDepthInternalFormat, 512, 512); // TODO: Remove 512
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_CaptureRBO);

				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				{
					PrintError("Capture frame buffer is incomplete!\n");
				}
			}

			// Offscreen framebuffers
			{
				glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
				CreateOffscreenFrameBuffer(&m_Offscreen0FBO, &m_Offscreen0RBO, frameBufferSize, m_OffscreenTexture0Handle);
				CreateOffscreenFrameBuffer(&m_Offscreen1FBO, &m_Offscreen1RBO, frameBufferSize, m_OffscreenTexture1Handle);
			}

			const real captureProjectionNearPlane = g_CameraManager->CurrentCamera()->GetZNear();
			const real captureProjectionFarPlane = g_CameraManager->CurrentCamera()->GetZFar();
			m_CaptureProjection = glm::perspective(glm::radians(90.0f), 1.0f, captureProjectionNearPlane, captureProjectionFarPlane);
			m_CaptureViews =
			{
				glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
				glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
				glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAtRH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
			};

			m_AlphaBGTextureID = InitializeTexture(RESOURCE_LOCATION + "textures/alpha-bg.png", 3, false, false, false);
			m_LoadingTextureID = InitializeTexture(RESOURCE_LOCATION + "textures/loading_1.png", 3, false, false, false);
			m_WorkTextureID = InitializeTexture(RESOURCE_LOCATION + "textures/work_d.jpg", 3, false, true, false);
			m_PointLightIconID = InitializeTexture(RESOURCE_LOCATION + "textures/icons/point-light-icon-256.png", 4, false, true, false);
			m_DirectionalLightIconID = InitializeTexture(RESOURCE_LOCATION + "textures/icons/directional-light-icon-256.png", 4, false, true, false);

			MaterialCreateInfo spriteMatCreateInfo = {};
			spriteMatCreateInfo.name = "Sprite material";
			spriteMatCreateInfo.shaderName = "sprite";
			spriteMatCreateInfo.engineMaterial = true;
			m_SpriteMatID = InitializeMaterial(&spriteMatCreateInfo);

			MaterialCreateInfo fontMatCreateInfo = {};
			fontMatCreateInfo.name = "Font material";
			fontMatCreateInfo.shaderName = "font";
			fontMatCreateInfo.engineMaterial = true;
			m_FontMatID = InitializeMaterial(&fontMatCreateInfo);

			MaterialCreateInfo postProcessMatCreateInfo = {};
			postProcessMatCreateInfo.name = "Post process material";
			postProcessMatCreateInfo.shaderName = "post_process";
			postProcessMatCreateInfo.engineMaterial = true;
			m_PostProcessMatID = InitializeMaterial(&postProcessMatCreateInfo);

			MaterialCreateInfo postFXAAMatCreateInfo = {};
			postFXAAMatCreateInfo.name = "FXAA";
			postFXAAMatCreateInfo.shaderName = "post_fxaa";
			postFXAAMatCreateInfo.engineMaterial = true;
			m_PostFXAAMatID = InitializeMaterial(&postFXAAMatCreateInfo);
			

			// 2D Quad
			{
				VertexBufferData::CreateInfo quad2DVertexBufferDataCreateInfo = {};
				quad2DVertexBufferDataCreateInfo.positions_2D = {
					glm::vec2(-1.0f,  -1.0f),
					glm::vec2(-1.0f, 3.0f),
					glm::vec2(3.0f,  -1.0f)
				};

				quad2DVertexBufferDataCreateInfo.texCoords_UV = {
					glm::vec2(0.0f, 0.0f),
					glm::vec2(0.0f, 2.0f),
					glm::vec2(2.0f, 0.0f)
				};

				quad2DVertexBufferDataCreateInfo.attributes =
					(u32)VertexAttribute::POSITION_2D |
					(u32)VertexAttribute::UV;

				m_Quad2DVertexBufferData = {};
				m_Quad2DVertexBufferData.Initialize(&quad2DVertexBufferDataCreateInfo);


				GameObject* quad2DGameObject = new GameObject("Sprite Quad 2D", GameObjectType::NONE);
				m_PersistentObjects.push_back(quad2DGameObject);
				quad2DGameObject->SetVisible(false);

				RenderObjectCreateInfo quad2DCreateInfo = {};
				quad2DCreateInfo.vertexBufferData = &m_Quad2DVertexBufferData;
				quad2DCreateInfo.materialID = m_PostProcessMatID;
				quad2DCreateInfo.depthWriteEnable = false;
				quad2DCreateInfo.gameObject = quad2DGameObject;
				quad2DCreateInfo.enableCulling = false;
				quad2DCreateInfo.visibleInSceneExplorer = false;
				quad2DCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
				quad2DCreateInfo.depthWriteEnable = false;
				m_Quad2DRenderID = InitializeRenderObject(&quad2DCreateInfo);

				m_Quad2DVertexBufferData.DescribeShaderVariables(this, m_Quad2DRenderID);
			}

			// 3D Quad
			{
				VertexBufferData::CreateInfo quad3DVertexBufferDataCreateInfo = {};
				quad3DVertexBufferDataCreateInfo.positions_3D = {
					glm::vec3(-1.0f, -1.0f, 0.0f),
					glm::vec3(-1.0f, 1.0f, 0.0f),
					glm::vec3(1.0f, -1.0f, 0.0f),

					glm::vec3(1.0f, -1.0f, 0.0f),
					glm::vec3(-1.0f, 1.0f, 0.0f),
					glm::vec3(1.0f, 1.0f, 0.0f),
				};

				quad3DVertexBufferDataCreateInfo.texCoords_UV = {
					glm::vec2(0.0f, 0.0f),
					glm::vec2(0.0f, 1.0f),
					glm::vec2(1.0f, 0.0f),

					glm::vec2(1.0f, 0.0f),
					glm::vec2(0.0f, 1.0f),
					glm::vec2(1.0f, 1.0f),
				};

				quad3DVertexBufferDataCreateInfo.attributes =
					(u32)VertexAttribute::POSITION |
					(u32)VertexAttribute::UV;

				m_Quad3DVertexBufferData = {};
				m_Quad3DVertexBufferData.Initialize(&quad3DVertexBufferDataCreateInfo);


				GameObject* quad3DGameObject = new GameObject("Sprite Quad 3D", GameObjectType::NONE);
				m_PersistentObjects.push_back(quad3DGameObject);
				quad3DGameObject->SetVisible(false);

				RenderObjectCreateInfo quad3DCreateInfo = {};
				quad3DCreateInfo.vertexBufferData = &m_Quad3DVertexBufferData;
				quad3DCreateInfo.materialID = m_SpriteMatID;
				quad3DCreateInfo.depthWriteEnable = false;
				quad3DCreateInfo.gameObject = quad3DGameObject;
				quad3DCreateInfo.enableCulling = false;
				quad3DCreateInfo.visibleInSceneExplorer = false;
				quad3DCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
				quad3DCreateInfo.depthWriteEnable = false;
				quad3DCreateInfo.editorObject = true; // TODO: Create other quad which is identical but is not an editor object for gameplay objects?
				m_Quad3DRenderID = InitializeRenderObject(&quad3DCreateInfo);

				m_Quad3DVertexBufferData.DescribeShaderVariables(this, m_Quad3DRenderID);
			}

			DrawLoadingTextureQuad();
			SwapBuffers();

			MaterialCreateInfo selectedObjectMatCreateInfo = {};
			selectedObjectMatCreateInfo.name = "Selected Object";
			selectedObjectMatCreateInfo.shaderName = "color";
			selectedObjectMatCreateInfo.engineMaterial = true;
			selectedObjectMatCreateInfo.colorMultiplier = glm::vec4(1.0f);
			m_SelectedObjectMatID = InitializeMaterial(&selectedObjectMatCreateInfo);

			if (!m_BRDFTexture)
			{
				i32 brdfSize = 512;
				i32 internalFormat = GL_RG16F;
				GLenum format = GL_RG;
				GLenum type = GL_FLOAT;

				m_BRDFTexture = new GLTexture("BRDF",
											  brdfSize,
											  brdfSize,
											  internalFormat,
											  format,
											  type);
				if (m_BRDFTexture->GenerateEmpty())
				{
					m_LoadedTextures.push_back(m_BRDFTexture);
					GenerateBRDFLUT(m_BRDFTexture->handle, brdfSize);
				}
				else
				{
					PrintError("Failed to generate BRDF texture\n");
				}
			}

			ImGui::CreateContext();

			// G-buffer objects
			glGenFramebuffers(1, &m_gBufferHandle);
			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);

			const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();

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
			glRenderbufferStorage(GL_RENDERBUFFER, m_OffscreenDepthBufferInternalFormat, frameBufferSize.x, frameBufferSize.y);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_gBufferDepthHandle);

			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				PrintError("Framebuffer not complete!\n");
			}
		}

		void GLRenderer::PostInitialize()
		{
			GenerateGBuffer();

			std::string ubuntuFilePath = RESOURCE_LOCATION + "fonts/UbuntuCondensed-Regular.ttf";
			std::string ubuntuRenderedFilePath = RESOURCE_LOCATION + "fonts/UbuntuCondensed-Regular-32.png";
			PROFILE_BEGIN("load font UbuntuCondensed");
			LoadFont(&m_FntUbuntuCondensed, 32, ubuntuFilePath, ubuntuRenderedFilePath);
			PROFILE_END("load font UbuntuCondensed");
			Profiler::PrintBlockDuration("load font UbuntuCondensed");



			std::string sourceCodeProFilePath = RESOURCE_LOCATION + "fonts/SourceCodePro-regular.ttf";
			std::string sourceCodeProRenderedFilePath = RESOURCE_LOCATION + "fonts/SourceCodePro-regular-12.png";
			PROFILE_BEGIN("load font SourceCodePro");
			LoadFont(&m_FntSourceCodePro, 12, sourceCodeProFilePath, sourceCodeProRenderedFilePath);
			PROFILE_END("load font SourceCodePro");
			Profiler::PrintBlockDuration("load font SourceCodePro");


			GLFWWindowWrapper* castedWindow = dynamic_cast<GLFWWindowWrapper*>(g_Window);
			if (castedWindow == nullptr)
			{
				PrintError("GLRenderer::PostInitialize expects g_Window to be of type GLFWWindowWrapper!\n");
				return;
			}

			ImGui_ImplGlfwGL3_Init(castedWindow->GetWindow());

			m_PhysicsDebugDrawer = new GLPhysicsDebugDraw();
			m_PhysicsDebugDrawer->Initialize();

			Print("Renderer initialized!\n");
		}

		void GLRenderer::Destroy()
		{
			glDeleteVertexArrays(1, &m_TextQuadVAO);
			glDeleteBuffers(1, &m_TextQuadVBO);

			for (BitmapFont* font : m_Fonts)
			{
				SafeDelete(font);
			}
			m_Fonts.clear();

			for (GLTexture* texture : m_LoadedTextures)
			{
				if (texture)
				{
					texture->Destroy();
					SafeDelete(texture);
				}
			}
			m_LoadedTextures.clear();

			ImGui_ImplGlfwGL3_Shutdown();
			ImGui::DestroyContext();

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

			DestroyRenderObject(m_Quad3DRenderID);
			DestroyRenderObject(m_Quad2DRenderID);
			
			DestroyRenderObject(m_GBufferQuadRenderID);

			u32 activeRenderObjectCount = GetActiveRenderObjectCount();
			if (activeRenderObjectCount > 0)
			{
				PrintError("=====================================================\n");
				PrintError("%i render objects were not destroyed before GL render:\n", activeRenderObjectCount);

				for (GLRenderObject* renderObject : m_RenderObjects)
				{
					if (renderObject)
					{
						PrintError("render object with material name: %s\n", renderObject->materialName.c_str());
						DestroyRenderObject(renderObject->renderID);
					}
				}
				PrintError("=====================================================\n");
			}
			m_RenderObjects.clear();

			m_SkyBoxMesh = nullptr;

			if (m_PhysicsDebugDrawer)
			{
				m_PhysicsDebugDrawer->Destroy();
				SafeDelete(m_PhysicsDebugDrawer);
			}

			m_gBufferQuadVertexBufferData.Destroy();
			m_Quad2DVertexBufferData.Destroy();
			m_Quad3DVertexBufferData.Destroy();

			// TODO: Is this wanted here?
			glfwTerminate();
		}

		MaterialID GLRenderer::InitializeMaterial(const MaterialCreateInfo* createInfo)
		{
			MaterialID matID = GetNextAvailableMaterialID();
			m_Materials.insert(std::pair<MaterialID, GLMaterial>(matID, {}));
			GLMaterial& mat = m_Materials.at(matID);
			mat.material = {};
			mat.material.name = createInfo->name;

			if (mat.material.name.empty())
			{
				PrintWarn("Material doesn't have a name!\n");
			}

			if (!GetShaderID(createInfo->shaderName, mat.material.shaderID))
			{
				if (createInfo->shaderName.empty())
				{
					PrintError("MaterialCreateInfo::shaderName must be filled in!\n");
				}
				else
				{
					PrintError("Material's shader name couldn't be found! shader name: %s\n", createInfo->shaderName.c_str());
				}
			}
			
			GLShader& shader = m_Shaders[mat.material.shaderID];

			glUseProgram(shader.program);


			// TODO: Is this really needed? (do things dynamically instead?)
			UniformInfo uniformInfo[] = {
				{ "model", 							&mat.uniformIDs.model },
				{ "modelInvTranspose", 				&mat.uniformIDs.modelInvTranspose },
				{ "modelViewProjection",			&mat.uniformIDs.modelViewProjection },
				{ "colorMultiplier", 				&mat.uniformIDs.colorMultiplier },
				{ "contrastBrightnessSaturation", 	&mat.uniformIDs.contrastBrightnessSaturation },
				{ "exposure",						&mat.uniformIDs.exposure },
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
				{ "texSize",						&mat.uniformIDs.texSize },
			};

			for (const UniformInfo& uniform : uniformInfo)
			{
				if (shader.shader.dynamicBufferUniforms.HasUniform(uniform.name) ||
					shader.shader.constantBufferUniforms.HasUniform(uniform.name))
				{
					*uniform.id = glGetUniformLocation(shader.program, uniform.name);
					if (*uniform.id == -1)
					{
						PrintWarn("uniform %s was not found for material %s (shader: %s)\n",
								  uniform.name, createInfo->name.c_str(), createInfo->shaderName.c_str());
					}
				}
			}

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

			mat.material.environmentMapPath = createInfo->environmentMapPath;

			mat.material.generateReflectionProbeMaps = createInfo->generateReflectionProbeMaps;

			mat.material.colorMultiplier = createInfo->colorMultiplier;

			mat.material.engineMaterial = createInfo->engineMaterial;

			if (shader.shader.needIrradianceSampler)
			{
				if (createInfo->irradianceSamplerMatID == InvalidID)
				{
					mat.irradianceSamplerID = InvalidID;
				}
				else
				{
					mat.irradianceSamplerID = m_Materials[createInfo->irradianceSamplerMatID].irradianceSamplerID;
				}
			}

			if (shader.shader.needBRDFLUT)
			{
				if (!m_BRDFTexture)
				{
					Print("BRDF LUT has not been generated before material which uses it!\n");
				}
				mat.brdfLUTSamplerID = m_BRDFTexture->handle;
			}

			if (shader.shader.needPrefilteredMap)
			{
				if (createInfo->prefilterMapSamplerMatID == InvalidID)
				{
					mat.prefilteredMapSamplerID = InvalidID;
				}
				else
				{
					mat.prefilteredMapSamplerID = m_Materials[createInfo->prefilterMapSamplerMatID].prefilteredMapSamplerID;
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
				i32 channelCount;
				bool flipVertically;
				bool generateMipMaps;
				bool hdr;
			};

			// Samplers that need to be loaded from file
			SamplerCreateInfo samplerCreateInfos[] =
			{
				{ shader.shader.needAlbedoSampler, mat.material.generateAlbedoSampler, &mat.albedoSamplerID, 
				createInfo->albedoTexturePath, "albedoSampler", 3, false, true, false },
				{ shader.shader.needMetallicSampler, mat.material.generateMetallicSampler, &mat.metallicSamplerID, 
				createInfo->metallicTexturePath, "metallicSampler", 3, false, true, false },
				{ shader.shader.needRoughnessSampler, mat.material.generateRoughnessSampler, &mat.roughnessSamplerID, 
				createInfo->roughnessTexturePath, "roughnessSampler", 3, false, true, false },
				{ shader.shader.needAOSampler, mat.material.generateAOSampler, &mat.aoSamplerID, 
				createInfo->aoTexturePath, "aoSampler", 3, false, true, false },
				{ shader.shader.needDiffuseSampler, mat.material.generateDiffuseSampler, &mat.diffuseSamplerID, 
				createInfo->diffuseTexturePath, "diffuseSampler", 3, false, true, false },
				{ shader.shader.needNormalSampler, mat.material.generateNormalSampler, &mat.normalSamplerID, 
				createInfo->normalTexturePath, "normalSampler", 3, false, true, false },
				{ shader.shader.needHDREquirectangularSampler, mat.material.generateHDREquirectangularSampler, &mat.hdrTextureID, 
				createInfo->hdrEquirectangularTexturePath, "hdrEquirectangularSampler", 4, true, false, true },
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
							PrintError("Empty file path specified in SamplerCreateInfo for texture %s in material %s\n", 
									   samplerCreateInfo.textureName.c_str(), mat.material.name.c_str());
						}
						else
						{
							GLTexture* loadedTexture = nullptr;
							if (GetLoadedTexture(samplerCreateInfo.filepath, &loadedTexture))
							{
								// TODO: not just this
								*samplerCreateInfo.id = loadedTexture->handle;
							}
							else
							{
								std::string fileNameClean = samplerCreateInfo.filepath;
								StripLeadingDirectories(fileNameClean);
								std::string textureProfileBlockName = "load texture " + fileNameClean;
								PROFILE_BEGIN(textureProfileBlockName);

								GLTexture* newTexture = new GLTexture(samplerCreateInfo.filepath,
																	  samplerCreateInfo.channelCount,
																	  samplerCreateInfo.flipVertically,
																	  samplerCreateInfo.generateMipMaps, 
																	  samplerCreateInfo.hdr);

								newTexture->LoadFromFile();

								PROFILE_END(textureProfileBlockName);
								Profiler::PrintBlockDuration(textureProfileBlockName);

								if (newTexture->bLoaded)
								{
									*samplerCreateInfo.id = newTexture->handle;
									m_LoadedTextures.push_back(newTexture);
								}
							}

							i32 uniformLocation = glGetUniformLocation(shader.program, samplerCreateInfo.textureName.c_str());
							if (uniformLocation == -1)
							{
								PrintWarn("texture uniform %s was not found in material %s (shader: %s)\n",
										  samplerCreateInfo.textureName.c_str(), mat.material.name.c_str(), shader.shader.name.c_str());
							}
							else
							{
								glUniform1i(uniformLocation, binding);
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
				if (positionLocation == -1)
				{
					PrintWarn("%s was not found in material %s, (shader %s)\n",
							  frameBufferPair.first.c_str(), mat.material.name.c_str(), shader.shader.name.c_str());
				}
				else
				{
					glUniform1i(positionLocation, binding);
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
					if (uniformLocation == -1)
					{
						PrintWarn("uniform cubemapSampler was not found in material %s (shader %s)\n",
								  mat.material.name.c_str(), shader.shader.name.c_str());
					}
					else
					{
						glUniform1i(uniformLocation, binding);
					}
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
				const char* uniformName = "cubemapSampler";
				i32 uniformLocation = glGetUniformLocation(shader.program, uniformName);
				if (uniformLocation == -1)
				{
					PrintWarn("uniform %s was not found in material %s (shader %s)\n", 
							  uniformName, mat.material.name.c_str(), shader.shader.name.c_str());
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				++binding;
			}

			if (shader.shader.needBRDFLUT)
			{
				const char* uniformName = "brdfLUT";
				i32 uniformLocation = glGetUniformLocation(shader.program, uniformName);
				if (uniformLocation == -1)
				{
					PrintWarn("uniform %s was not found in material %s (shader %s)\n", 
							  uniformName, mat.material.name.c_str(), shader.shader.name.c_str());
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
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
				const char* uniformName = "irradianceSampler";
				i32 uniformLocation = glGetUniformLocation(shader.program, uniformName);
				if (uniformLocation == -1)
				{
					PrintWarn("uniform %s was not found in material %s (shader %s)\n", 
							  uniformName, mat.material.name.c_str(), shader.shader.name.c_str());
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
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
				const char* uniformName = "prefilterMap";
				i32 uniformLocation = glGetUniformLocation(shader.program, uniformName);
				if (uniformLocation == -1)
				{
					PrintWarn("uniform %s was not found in material %s (shader %s)\n",
							  uniformName, mat.material.name.c_str(), shader.shader.name.c_str());
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				++binding;
			}

			return matID;
		}

		TextureID GLRenderer::InitializeTexture(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR)
		{
			GLTexture* newTexture = new GLTexture(relativeFilePath, channelCount, bFlipVertically, bGenerateMipMaps, bHDR);
			if (newTexture->LoadFromFile())
			{
				m_LoadedTextures.push_back(newTexture);
			}

			TextureID id = m_LoadedTextures.size() - 1;
			return id;
		}

		u32 GLRenderer::InitializeRenderObject(const RenderObjectCreateInfo* createInfo)
		{
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
				PrintError("Render object's materialID has not been set in its createInfo!\n");

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
					PrintWarn("Render object created with empty material name!\n");
				}
			}

			if (renderObject->gameObject->GetName().empty())
			{
				PrintWarn("Render object created with empty name!\n");
			}

			if (m_Materials.empty())
			{
				PrintError("Render object is being created before any materials have been created!\n");
			}

			if (m_Materials.find(renderObject->materialID) == m_Materials.end())
			{
				PrintError("Uninitialized material with MaterialID %i\n", renderObject->materialID);
				return renderID;
			}

			GLMaterial& material = m_Materials[renderObject->materialID];
			GLShader& shader = m_Shaders[material.material.shaderID];

			glUseProgram(shader.program);

			if (createInfo->vertexBufferData)
			{
				glGenVertexArrays(1, &renderObject->VAO);
				glBindVertexArray(renderObject->VAO);

				glGenBuffers(1, &renderObject->VBO);
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)createInfo->vertexBufferData->BufferSize, createInfo->vertexBufferData->pDataStart, GL_STATIC_DRAW);
			}

			if (createInfo->indices != nullptr &&
				!createInfo->indices->empty())
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

		void GLRenderer::PostInitializeRenderObject(RenderID renderID)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			GLMaterial& material = m_Materials[renderObject->materialID];

			// glFlush calls help RenderDoc replay frames without crashing

			if (material.material.generateReflectionProbeMaps)
			{
				BatchRenderObjects();

				std::string profileBlockName = "capturing scene to cubemap " + renderObject->gameObject->GetName();
				PROFILE_BEGIN(profileBlockName);
				CaptureSceneToCubemap(renderID);
				PROFILE_END(profileBlockName);
				Profiler::PrintBlockDuration(profileBlockName);
				//glFlush();

				profileBlockName = "generating irradiance sampler for " + renderObject->gameObject->GetName();
				PROFILE_BEGIN(profileBlockName);
				GenerateIrradianceSamplerFromCubemap(renderObject->materialID);
				PROFILE_END(profileBlockName);
				Profiler::PrintBlockDuration(profileBlockName);
				//glFlush();
				
				profileBlockName = "generating prefiltered map for " + renderObject->gameObject->GetName();
				PROFILE_BEGIN(profileBlockName);
				GeneratePrefilteredMapFromCubemap(renderObject->materialID);
				PROFILE_END(profileBlockName);
				Profiler::PrintBlockDuration(profileBlockName);
				//glFlush();


				// Display captured cubemap as skybox
				//m_Materials[m_RenderObjects[cubemapID]->materialID].cubemapSamplerID =
				//	m_Materials[m_RenderObjects[renderID]->materialID].cubemapSamplerID;
			}
			else if (material.material.generateIrradianceSampler)
			{
				std::string profileBlockName = "generating cubemap for " + renderObject->gameObject->GetName();
				PROFILE_BEGIN(profileBlockName);
				GenerateCubemapFromHDREquirectangular(renderObject->materialID, material.material.environmentMapPath);
				PROFILE_END(profileBlockName);
				Profiler::PrintBlockDuration(profileBlockName);
				//glFlush();

				
				profileBlockName = "generating irradiance sampler for " + renderObject->gameObject->GetName();
				PROFILE_BEGIN(profileBlockName);
				GenerateIrradianceSamplerFromCubemap(renderObject->materialID);
				PROFILE_END(profileBlockName);
				Profiler::PrintBlockDuration(profileBlockName);
				//glFlush();


				profileBlockName = "generating prefiltered map for " + renderObject->gameObject->GetName();
				PROFILE_BEGIN(profileBlockName);
				GeneratePrefilteredMapFromCubemap(renderObject->materialID);
				PROFILE_END(profileBlockName);
				Profiler::PrintBlockDuration(profileBlockName);
				//glFlush();
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

		void GLRenderer::GenerateCubemapFromHDREquirectangular(MaterialID cubemapMaterialID, 
															   const std::string& environmentMapPath)
		{
			if (!m_SkyBoxMesh)
			{
				PrintError("Attempted to generate cubemap before skybox object was created!\n");
				return;
			}

			MaterialID equirectangularToCubeMatID = InvalidMaterialID;
			if (!GetMaterialID("Equirectangular to Cube", equirectangularToCubeMatID))
			{
				std::string profileBlockName = "generating equirectangular mat";
				PROFILE_BEGIN(profileBlockName);
				MaterialCreateInfo equirectangularToCubeMatCreateInfo = {};
				equirectangularToCubeMatCreateInfo.name = "Equirectangular to Cube";
				equirectangularToCubeMatCreateInfo.shaderName = "equirectangular_to_cube";
				equirectangularToCubeMatCreateInfo.enableHDREquirectangularSampler = true;
				equirectangularToCubeMatCreateInfo.generateHDREquirectangularSampler = true;
				equirectangularToCubeMatCreateInfo.engineMaterial = true;
				// TODO: Make cyclable at runtime
				equirectangularToCubeMatCreateInfo.hdrEquirectangularTexturePath = environmentMapPath;
				equirectangularToCubeMatID = InitializeMaterial(&equirectangularToCubeMatCreateInfo);
				PROFILE_END(profileBlockName);
				Profiler::PrintBlockDuration(profileBlockName);
			}

			GLMaterial& equirectangularToCubemapMaterial = m_Materials[equirectangularToCubeMatID];
			GLShader& equirectangularToCubemapShader = m_Shaders[equirectangularToCubemapMaterial.material.shaderID];

			// TODO: Handle no skybox being set gracefully
			GLRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetRenderID());
			GLMaterial& skyboxGLMaterial = m_Materials[skyboxRenderObject->materialID];

			glUseProgram(equirectangularToCubemapShader.program);
			
			// TODO: Store what location this texture is at (might not be 0)
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_Materials[equirectangularToCubeMatID].hdrTextureID);

			// Update object's uniforms under this shader's program
			glUniformMatrix4fv(equirectangularToCubemapMaterial.uniformIDs.model, 1, false,
							   &m_SkyBoxMesh->GetTransform()->GetWorldTransform()[0][0]);

			glUniformMatrix4fv(equirectangularToCubemapMaterial.uniformIDs.projection, 1, false, 
				&m_CaptureProjection[0][0]);

			glm::vec2 cubemapSize = skyboxGLMaterial.material.cubemapSamplerSize;

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			glRenderbufferStorage(GL_RENDERBUFFER, m_CaptureDepthInternalFormat, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);

			glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);

			glBindVertexArray(skyboxRenderObject->VAO);
			glBindBuffer(GL_ARRAY_BUFFER, skyboxRenderObject->VBO);

			glCullFace(skyboxRenderObject->cullFace);

			if (skyboxRenderObject->enableCulling)
			{
				glEnable(GL_CULL_FACE);
			}
			else
			{
				glDisable(GL_CULL_FACE);
			}

			glDepthFunc(skyboxRenderObject->depthTestReadFunc);

			for (u32 i = 0; i < 6; ++i)
			{
				glUniformMatrix4fv(equirectangularToCubemapMaterial.uniformIDs.view, 1, false, 
					&m_CaptureViews[i][0][0]);

				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[cubemapMaterialID].cubemapSamplerID, 0);

				glDepthMask(GL_TRUE);

				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

				glDepthMask(skyboxRenderObject->depthWriteEnable);

				glDrawArrays(skyboxRenderObject->topology, 0, 
					(GLsizei)skyboxRenderObject->vertexBufferData->VertexCount);
			}

			// Generate mip maps for generated cubemap
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[cubemapMaterialID].cubemapSamplerID);
			glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
		}

		void GLRenderer::GeneratePrefilteredMapFromCubemap(MaterialID cubemapMaterialID)
		{
			if (!m_SkyBoxMesh)
			{
				PrintError("Attempted to generate prefiltered map before skybox object was created!\n");
				return;
			}

			MaterialID prefilterMatID = InvalidMaterialID;
			if (!GetMaterialID("Prefilter", prefilterMatID))
			{
				MaterialCreateInfo prefilterMaterialCreateInfo = {};
				prefilterMaterialCreateInfo.name = "Prefilter";
				prefilterMaterialCreateInfo.shaderName = "prefilter";
				prefilterMaterialCreateInfo.engineMaterial = true;
				prefilterMatID = InitializeMaterial(&prefilterMaterialCreateInfo);
			}

			GLMaterial& prefilterMat = m_Materials[prefilterMatID];
			GLShader& prefilterShader = m_Shaders[prefilterMat.material.shaderID];

			GLRenderObject* skybox = GetRenderObject(m_SkyBoxMesh->GetRenderID());

			glUseProgram(prefilterShader.program);

			glUniformMatrix4fv(prefilterMat.uniformIDs.model, 1, false, 
				&m_SkyBoxMesh->GetTransform()->GetWorldTransform()[0][0]);

			glUniformMatrix4fv(prefilterMat.uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);

			glActiveTexture(GL_TEXTURE0); // TODO: Remove constant
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[cubemapMaterialID].cubemapSamplerID);

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);

			glBindVertexArray(skybox->VAO);
			glBindBuffer(GL_ARRAY_BUFFER, skybox->VBO);

			if (skybox->enableCulling)
			{
				glEnable(GL_CULL_FACE);
			}
			else
			{
				glDisable(GL_CULL_FACE);
			}

			glCullFace(skybox->cullFace);

			glDepthFunc(skybox->depthTestReadFunc);

			glDepthMask(skybox->depthWriteEnable);

			u32 maxMipLevels = 5;
			for (u32 mip = 0; mip < maxMipLevels; ++mip)
			{
				u32 mipWidth = (u32)(m_Materials[cubemapMaterialID].material.prefilteredMapSize.x * pow(0.5f, mip));
				u32 mipHeight = (u32)(m_Materials[cubemapMaterialID].material.prefilteredMapSize.y * pow(0.5f, mip));
				assert(mipWidth <= Renderer::MAX_TEXTURE_DIM);
				assert(mipHeight <= Renderer::MAX_TEXTURE_DIM);

				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				glRenderbufferStorage(GL_RENDERBUFFER, m_CaptureDepthInternalFormat, mipWidth, mipHeight);

				glViewport(0, 0, mipWidth, mipHeight);

				real roughness = (real)mip / (real(maxMipLevels - 1));
				i32 roughnessUniformLocation = glGetUniformLocation(prefilterShader.program, "roughness");
				glUniform1f(roughnessUniformLocation, roughness);
				for (u32 i = 0; i < 6; ++i)
				{
					glUniformMatrix4fv(prefilterMat.uniformIDs.view, 1, false, &m_CaptureViews[i][0][0]);

					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[cubemapMaterialID].prefilteredMapSamplerID, mip);
					
					glDrawArrays(skybox->topology, 0, (GLsizei)skybox->vertexBufferData->VertexCount);
				}
			}

			// TODO: Make this a togglable bool param for the shader (or roughness param)
			// Visualize prefiltered map as skybox:
			//m_Materials[renderObject->materialID].cubemapSamplerID = m_Materials[renderObject->materialID].prefilteredMapSamplerID;
		}

		void GLRenderer::GenerateBRDFLUT(u32 brdfLUTTextureID, i32 brdfLUTSize)
		{
			if (m_1x1_NDC_Quad)
			{
				// Don't re-create material or object
				return;
			}

			MaterialCreateInfo brdfMaterialCreateInfo = {};
			brdfMaterialCreateInfo.name = "BRDF";
			brdfMaterialCreateInfo.shaderName = "brdf";
			brdfMaterialCreateInfo.engineMaterial = true;
			MaterialID brdfMatID = InitializeMaterial(&brdfMaterialCreateInfo);

			// Generate 1x1 NDC quad
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

				RenderID quadRenderID = InitializeRenderObject(&quadCreateInfo);
				m_1x1_NDC_Quad = GetRenderObject(quadRenderID);

				if (!m_1x1_NDC_Quad)
				{
					PrintError("Failed to create 1x1 NDC quad!\n");
				}
				else
				{
					SetTopologyMode(quadRenderID, TopologyMode::TRIANGLE_STRIP);
					m_1x1_NDC_QuadVertexBufferData.DescribeShaderVariables(g_Renderer, quadRenderID);
				}
			}

			glUseProgram(m_Shaders[m_Materials[brdfMatID].material.shaderID].program);

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTextureID, 0);

			glBindVertexArray(m_1x1_NDC_Quad->VAO);
			glBindBuffer(GL_ARRAY_BUFFER, m_1x1_NDC_Quad->VBO);

			glViewport(0, 0, brdfLUTSize, brdfLUTSize);

			if (m_1x1_NDC_Quad->enableCulling)
			{
				glEnable(GL_CULL_FACE);
			}
			else
			{
				glDisable(GL_CULL_FACE);
			}

			glCullFace(m_1x1_NDC_Quad->cullFace);

			glDepthFunc(GL_ALWAYS);

			glDepthMask(GL_FALSE);

			// Render quad
			glDrawArrays(m_1x1_NDC_Quad->topology, 0, (GLsizei)m_1x1_NDC_Quad->vertexBufferData->VertexCount);
		}

		void GLRenderer::GenerateIrradianceSamplerFromCubemap(MaterialID cubemapMaterialID)
		{
			if (!m_SkyBoxMesh)
			{
				PrintError("Attempted to generate irradiance sampler before skybox object was created!\n");
				return;
			}

			MaterialID irrandianceMatID = InvalidMaterialID;
			if (!GetMaterialID("Irradiance", irrandianceMatID))
			{
				std::string profileBlockName = "generating irradiance mat";
				PROFILE_BEGIN(profileBlockName);
				MaterialCreateInfo irrandianceMatCreateInfo = {};
				irrandianceMatCreateInfo.name = "Irradiance";
				irrandianceMatCreateInfo.shaderName = "irradiance";
				irrandianceMatCreateInfo.enableCubemapSampler = true;
				irrandianceMatCreateInfo.engineMaterial = true;
				irrandianceMatID = InitializeMaterial(&irrandianceMatCreateInfo);
				PROFILE_END(profileBlockName);
				Profiler::PrintBlockDuration(profileBlockName);
			}

			GLMaterial& irradianceMat = m_Materials[irrandianceMatID];
			GLShader& shader = m_Shaders[irradianceMat.material.shaderID];

			GLRenderObject* skybox = GetRenderObject(m_SkyBoxMesh->GetRenderID());

			glUseProgram(shader.program);
			
			glUniformMatrix4fv(irradianceMat.uniformIDs.model, 1, false, 
				&m_SkyBoxMesh->GetTransform()->GetWorldTransform()[0][0]);

			glUniformMatrix4fv(irradianceMat.uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);

			glActiveTexture(GL_TEXTURE0); // TODO: Remove constant
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[cubemapMaterialID].cubemapSamplerID);

			glm::vec2 cubemapSize = m_Materials[cubemapMaterialID].material.irradianceSamplerSize;

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			glRenderbufferStorage(GL_RENDERBUFFER, m_CaptureDepthInternalFormat, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);

			glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);

			if (skybox->enableCulling)
			{
				glEnable(GL_CULL_FACE);
			}
			else
			{
				glDisable(GL_CULL_FACE);
			}

			glCullFace(skybox->cullFace);

			glDepthFunc(skybox->depthTestReadFunc);

			glDepthMask(skybox->depthWriteEnable);

			glBindVertexArray(skybox->VAO);
			glBindBuffer(GL_ARRAY_BUFFER, skybox->VBO);

			for (u32 i = 0; i < 6; ++i)
			{
				glUniformMatrix4fv(irradianceMat.uniformIDs.view, 1, false, &m_CaptureViews[i][0][0]);

				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[cubemapMaterialID].irradianceSamplerID, 0);

				// Should be drawing cube here, not object (reflection probe's sphere is being drawn
				glDrawArrays(skybox->topology, 0, (GLsizei)skybox->vertexBufferData->VertexCount);
			}
		}

		void GLRenderer::CaptureSceneToCubemap(RenderID cubemapRenderID)
		{
			DrawCallInfo drawCallInfo = {};
			drawCallInfo.cubemapObjectRenderID = cubemapRenderID;
			drawCallInfo.bRenderToCubemap = true;

			// Clear cubemap faces
			GLRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
			GLMaterial* cubemapMaterial = &m_Materials[cubemapRenderObject->materialID];

			glm::vec2 cubemapSize = cubemapMaterial->material.cubemapSamplerSize;

			// Must be enabled to clear depth buffer
			glDepthMask(GL_TRUE);
			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			glRenderbufferStorage(GL_RENDERBUFFER, m_CaptureDepthInternalFormat, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);

			glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);

			for (size_t face = 0; face < 6; ++face)
			{
				// Clear all G-Buffers
				if (!cubemapMaterial->cubemapSamplerGBuffersIDs.empty())
				{
					// Skip first buffer, it'll be cleared below
					for (size_t i = 1; i < cubemapMaterial->cubemapSamplerGBuffersIDs.size(); ++i)
					{
						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, 
							GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerGBuffersIDs[i].id, 0);

						glClear(GL_COLOR_BUFFER_BIT);
					}
				}

				// Clear base cubemap framebuffer + depth buffer
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerID, 0);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
									   GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapDepthSamplerID, 0);

				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			}

			drawCallInfo.bDeferred = true;
			DrawDeferredObjects(drawCallInfo);
			drawCallInfo.bDeferred = false;
			DrawGBufferContents(drawCallInfo);
			DrawForwardObjects(drawCallInfo);
		}

		void GLRenderer::SwapBuffers()
		{
			glfwSwapBuffers(static_cast<GLFWWindowWrapper*>(g_Window)->GetWindow());
		}

		void GLRenderer::RecaptureReflectionProbe()
		{
			for (GLRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject &&
					m_Materials[renderObject->materialID].material.generateReflectionProbeMaps)
				{
					CaptureSceneToCubemap(renderObject->renderID);
					GenerateIrradianceSamplerFromCubemap(renderObject->materialID);
					GeneratePrefilteredMapFromCubemap(renderObject->materialID);
				}
			}

			AddEditorString("Captured reflection probe");
		}

		u32 GLRenderer::GetTextureHandle(TextureID textureID) const
		{
			assert(textureID >= 0);
			assert(textureID < m_LoadedTextures.size());
			return m_LoadedTextures[textureID]->handle;
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
			for (auto& materialPair : m_Materials)
			{
				if (materialPair.second.material.name.compare(materialName) == 0)
				{
					materialID = materialPair.first;
					return true;
				}
			}

			for (auto& materialObj : BaseScene::s_ParsedMaterials)
			{
				if (materialObj.GetString("name").compare(materialName) == 0)
				{
					// Material exists in library, but hasn't been initialized yet
					MaterialCreateInfo matCreateInfo = {};
					Material::ParseJSONObject(materialObj, matCreateInfo);

					materialID = g_Renderer->InitializeMaterial(&matCreateInfo);
					g_SceneManager->CurrentScene()->AddMaterialID(materialID);

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
				PrintError("Invalid renderID passed to SetTopologyMode: %i\n", renderID);
				return;
			}

			GLenum glMode = TopologyModeToGLMode(topology);

			if (glMode == GL_INVALID_ENUM)
			{
				PrintError("Unhandled TopologyMode passed to GLRenderer::SetTopologyMode: %i\n", (i32)topology);
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
		}

		void GLRenderer::GenerateFrameBufferTexture(u32* handle, i32 index, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size)
		{
			glGenTextures(1, handle);
			glBindTexture(GL_TEXTURE_2D, *handle);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.x, size.y, 0, format, type, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, *handle, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		void GLRenderer::ResizeFrameBufferTexture(u32 handle, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size)
		{
			glBindTexture(GL_TEXTURE_2D, handle);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.x, size.y, 0, format, type, NULL);
		}

		void GLRenderer::ResizeRenderBuffer(u32 handle, const glm::vec2i& size, GLenum internalFormat)
		{
			glBindRenderbuffer(GL_RENDERBUFFER, handle);
			glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, size.x, size.y);
		}

		void GLRenderer::Update()
		{
			if (m_EditorStrSecRemaining > 0.0f)
			{
				m_EditorStrSecRemaining -= g_DeltaTime;
				if (m_EditorStrSecRemaining <= 0.0f)
				{
					m_EditorStrSecRemaining = 0.0f;
				}
			}

			m_PhysicsDebugDrawer->UpdateDebugMode();

			// This fixes the weird artifacts in refl probes, but obviously isn't ideal...
			//static i32 count = 0;
			//if (++count == 1)
			//{
			//	RecaptureReflectionProbe();
			//}

			if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_U))
			{
				RecaptureReflectionProbe();
			}
		}

		void GLRenderer::Draw()
		{
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			DrawCallInfo drawCallInfo = {};

			// TDDO: Find an alternative to so many PROFILE macros...

			// TODO: Don't sort render objects frame! Only when things are added/removed
			PROFILE_BEGIN("Render > BatchRenderObjects");
			BatchRenderObjects();
			PROFILE_END("Render > BatchRenderObjects");

			// World-space objects
			drawCallInfo.bDeferred = true;
			PROFILE_BEGIN("Render > DrawDeferredObjects");
			DrawDeferredObjects(drawCallInfo);
			drawCallInfo.bDeferred = false;
			PROFILE_END("Render > DrawDeferredObjects");
			PROFILE_BEGIN("Render > DrawGBufferContents");
			DrawGBufferContents(drawCallInfo);
			PROFILE_END("Render > DrawGBufferContents");
			PROFILE_BEGIN("Render > DrawForwardObjects");
			DrawForwardObjects(drawCallInfo);
			PROFILE_END("Render > DrawForwardObjects");
			PROFILE_BEGIN("Render > DrawWorldSpaceSprites");
			DrawWorldSpaceSprites();
			PROFILE_END("Render > DrawWorldSpaceSprites");
			PROFILE_BEGIN("Render > DrawOffscreenTexture");
			DrawOffscreenTexture();
			PROFILE_END("Render > DrawOffscreenTexture");

			if (!m_PhysicsDebuggingSettings.DisableAll)
			{
				PROFILE_BEGIN("Render > PhysicsDebugRender");
				PhysicsDebugRender();
				PROFILE_END("Render > PhysicsDebugRender");
			}

			if (g_EngineInstance->IsRenderingEditorObjects())
			{
				PROFILE_BEGIN("Render > DrawEditorObjects");
				DrawEditorObjects(drawCallInfo);
				PROFILE_END("Render > DrawEditorObjects");
			}

			// Screen-space objects
#if 1
			std::vector<real> letterYOffsetsEmpty;
			//std::vector<real> letterYOffsets1;
			//letterYOffsets1.reserve(26);
			//for (i32 i = 0; i < 26; ++i)
			//{
			//	letterYOffsets1.push_back(sin(i * 0.75f + g_SecElapsedSinceProgramStart * 3.0f) * 5.0f);
			//}
			//std::vector<real> letterYOffsets2;
			//letterYOffsets2.reserve(26);
			//for (i32 i = 0; i < 26; ++i)
			//{
			//	letterYOffsets2.push_back(cos(i + g_SecElapsedSinceProgramStart * 10.0f) * 5.0f);
			//}
			//std::vector<real> letterYOffsets3;
			//letterYOffsets3.reserve(44);
			//for (i32 i = 0; i < 44; ++i)
			//{
			//	letterYOffsets3.push_back(sin(i * 0.5f + 0.5f + g_SecElapsedSinceProgramStart * 6.0f) * 4.0f);
			//}
			
			std::string str;

			// Text stress test:
			/*SetFont(m_FntSourceCodePro);
			DrawString(str, glm::vec4(0.95f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.65f), 3.5f);
			str = std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ");
			DrawString(str, glm::vec4(0.95f, 0.6f, 0.95f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.6f), 3.5f);
			str = std::string("0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"");
			DrawString(str, glm::vec4(0.8f, 0.9f, 0.1f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.55f), 3.5f);
			str = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
			DrawString(str, glm::vec4(0.95f, 0.1f, 0.5f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.5f), 3.5f);
			str = std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ");
			DrawString(str, glm::vec4(0.1f, 0.9f, 0.1f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.45f), 3.5f);
			str = std::string("0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"");
			DrawString(str, glm::vec4(0.2f, 0.4f, 0.7f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.4f), 3.5f);
			str = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
			DrawString(str, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.35f), 3.5f);
			str = std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ");
			DrawString(str, glm::vec4(0.55f, 0.6f, 0.95f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.3f), 3.5f);
			str = std::string("0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"");
			DrawString(str, glm::vec4(0.3f, 0.3f, 0.9f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.25f), 3.5f);
			str = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
			DrawString(str, glm::vec4(0.5f, 0.9f, 0.9f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.2f), 3.5f);
			str = std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ");
			DrawString(str, glm::vec4(0.0f, 0.8f, 0.3f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.15f), 3.5f);
			str = std::string("0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"");
			DrawString(str, glm::vec4(0.8f, 0.4f, 0.1f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.1f), 3.5f);

			SetFont(m_FntUbuntuCondensed);
			str = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
			DrawString(str, glm::vec4(0.95f, 0.5f, 0.1f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.0f), 6);
			str = std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ");
			DrawString(str, glm::vec4(0.55f, 0.6f, 0.95f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.1f), 6);
			str = std::string("0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"");
			DrawString(str, glm::vec4(0.0f, 0.9f, 0.7f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.2f), 6);
			str = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
			DrawString(str, glm::vec4(0.55f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.3f), 6);
			str = std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ");
			DrawString(str, glm::vec4(0.25f, 0.0f, 0.95f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.4f), 6);
			str = std::string("0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"");
			DrawString(str, glm::vec4(0.8f, 0.2f, 0.7f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.5f), 6);
			str = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
			DrawString(str, glm::vec4(0.95f, 0.8f, 0.6f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.6f), 6);
			str = std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ");
			DrawString(str, glm::vec4(0.6f, 0.4f, 0.0f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.7f), 6);
			str = std::string("0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"");
			DrawString(str, glm::vec4(0.9f, 0.9f, 0.0f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.8f), 6);*/

			//std::string str = std::string("XYZ");
			//DrawString(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP_LEFT, glm::vec2(0.0f), 3, &letterYOffsetsEmpty);
			//DrawString(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP, glm::vec2(0.0f), 3, &letterYOffsetsEmpty);
			//DrawString(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP_RIGHT, glm::vec2(0.0f), 3, &letterYOffsetsEmpty);
			//DrawString(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::RIGHT, glm::vec2(0.0f), 3, &letterYOffsetsEmpty);
			//DrawString(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::BOTTOM_RIGHT, glm::vec2(0.0f), 3, &letterYOffsetsEmpty);
			//DrawString(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::BOTTOM, glm::vec2(0.0f), 3, &letterYOffsetsEmpty);
			//DrawString(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::BOTTOM_LEFT, glm::vec2(0.0f), 3, &letterYOffsetsEmpty);
			//DrawString(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::LEFT, glm::vec2(0.0f), 3, &letterYOffsetsEmpty);
			//DrawString(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f), 3, &letterYOffsetsEmpty);

			//std::string fxaaEnabledStr = std::string("FXAA: ") + (m_PostProcessSettings.bEnableFXAA ? "1" : "0");
			//DrawString(fxaaEnabledStr, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP_RIGHT, glm::vec2(-0.01f, 0.0f), 5, &letterYOffsetsEmpty);
			//glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			//std::string resolutionStr = "Frame buffer size: " +  IntToString(frameBufferSize.x) + "x" + IntToString(frameBufferSize.y);
			//DrawString(resolutionStr, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP_RIGHT, glm::vec2(-0.01f, 0.04f), 5, &letterYOffsetsEmpty);
#endif

			if (m_EditorStrSecRemaining > 0.0f)
			{
				SetFont(m_FntUbuntuCondensed);
				real alpha = glm::clamp(m_EditorStrSecRemaining / (m_EditorStrSecDuration*m_EditorStrFadeDurationPercent),
										0.0f, 1.0f);
				DrawString(m_EditorMessage, glm::vec4(1.0f, 1.0f, 1.0f, alpha), AnchorPoint::CENTER, glm::vec2(0.0f), 3, false);
			}

			PROFILE_BEGIN("Render > DrawScreenSpaceSprites");
			DrawScreenSpaceSprites();
			PROFILE_END("Render > DrawScreenSpaceSprites");

			PROFILE_BEGIN("Render > UpdateTextBuffer & DrawText");
			UpdateTextBuffer();
			DrawText();
			PROFILE_END("Render > UpdateTextBuffer & DrawText");

			if (g_EngineInstance->IsRenderingImGui())
			{
				PROFILE_BEGIN("Render > ImGuiRender");
				ImGui::Render();
				ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
				PROFILE_END("Render > ImGuiRender");
			}

			PROFILE_BEGIN("Render > SwapBuffers");
			SwapBuffers();
			PROFILE_END("Render > SwapBuffers");
		}

		void GLRenderer::BatchRenderObjects()
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
			
			// Sort render objects into deferred + forward buckets
			for (auto& materialPair : m_Materials)
			{
				MaterialID matID = materialPair.first;
				ShaderID shaderID = materialPair.second.material.shaderID;
				if (shaderID == InvalidShaderID)
				{
					PrintWarn("GLRenderer::BatchRenderObjects > Material has invalid shaderID: %s\n",
							  materialPair.second.material.name.c_str());
					continue;
				}
				GLShader* shader = &m_Shaders[shaderID];

				UpdateMaterialUniforms(matID);

				if (shader->shader.deferred)
				{
					m_DeferredRenderObjectBatches.emplace_back();
					for (GLRenderObject* renderObject : m_RenderObjects)
					{
						if (renderObject &&
							renderObject->gameObject->IsVisible() &&
							renderObject->materialID == matID &&
							!renderObject->editorObject &&
							renderObject->vertexBufferData)
						{
							m_DeferredRenderObjectBatches.back().push_back(renderObject);
						}
					}
				}
				else
				{
					m_ForwardRenderObjectBatches.emplace_back();
					for (GLRenderObject* renderObject : m_RenderObjects)
					{
						if (renderObject &&
							renderObject->gameObject->IsVisible() &&
							renderObject->materialID == matID &&
							!renderObject->editorObject &&
							renderObject->vertexBufferData)
						{
							m_ForwardRenderObjectBatches.back().push_back(renderObject);
						}
					}
				}
			}
			
			for (GLRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject &&
					renderObject->gameObject->IsVisible() &&
					renderObject->editorObject &&
					renderObject->vertexBufferData)
				{
					m_EditorRenderObjectBatch.push_back(renderObject);
				}
			}

#if _DEBUG
			u32 visibleObjectCount = 0;
			for (GLRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject &&
					renderObject->gameObject->IsVisible() &&
					!renderObject->editorObject &&
					renderObject->vertexBufferData)
				{
					++visibleObjectCount;
				}
			}

			u32 accountedForObjectCount = 0;
			for (std::vector<GLRenderObject*>& batch : m_DeferredRenderObjectBatches)
			{
				accountedForObjectCount += batch.size();
			}

			for (std::vector<GLRenderObject*>& batch : m_ForwardRenderObjectBatches)
			{
				accountedForObjectCount += batch.size();
			}

			if (visibleObjectCount != accountedForObjectCount)
			{
				PrintError("BatchRenderObjects didn't account for every visible object!\n");
			}
#endif
		}

		void GLRenderer::DrawDeferredObjects(const DrawCallInfo& drawCallInfo)
		{
			if (!drawCallInfo.bDeferred)
			{
				PrintError("DrawDeferredObjects was called with a drawCallInfo which isn't set to be deferred!\n");
			}

			if (drawCallInfo.bRenderToCubemap)
			{
				// TODO: Bind depth buffer to cubemap's depth buffer (needs to generated?)

				GLRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
				GLMaterial* cubemapMaterial = &m_Materials[cubemapRenderObject->materialID];

				glm::vec2 cubemapSize = cubemapMaterial->material.cubemapSamplerSize;

				glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				glRenderbufferStorage(GL_RENDERBUFFER, m_CaptureDepthInternalFormat, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);

				glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);
			}
			else
			{
				glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
				glViewport(0, 0, (GLsizei)frameBufferSize.x, (GLsizei)frameBufferSize.y);

				glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);
				glBindRenderbuffer(GL_RENDERBUFFER, m_gBufferDepthHandle);
			}

			{
				const i32 FRAMEBUFFER_COUNT = 3;
				GLenum attachments[FRAMEBUFFER_COUNT] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
				glDrawBuffers(FRAMEBUFFER_COUNT, attachments);
			}

			glDepthMask(GL_TRUE);

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			for (std::vector<GLRenderObject*>& batch : m_DeferredRenderObjectBatches)
			{
				DrawRenderObjectBatch(batch, drawCallInfo);
			}

			glUseProgram(0);
			glBindVertexArray(0);

			{
				const i32 FRAMEBUFFER_COUNT = 1;
				u32 attachments[FRAMEBUFFER_COUNT] = { GL_COLOR_ATTACHMENT0 };
				glDrawBuffers(FRAMEBUFFER_COUNT, attachments);
			}

			// Copy depth from G-Buffer to default render target
			if (drawCallInfo.bRenderToCubemap)
			{
				// No blit is needed, since we already drew to the cubemap depth
			}
			else
			{
				const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();

				glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferHandle);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_Offscreen0RBO);
				glBlitFramebuffer(0, 0, frameBufferSize.x, frameBufferSize.y, 0, 0, frameBufferSize.x, 
					frameBufferSize.y, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			}
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		}

		void GLRenderer::DrawGBufferContents(const DrawCallInfo& drawCallInfo)
		{
			if (drawCallInfo.bDeferred)
			{
				PrintError("DrawGBufferContents was called with a drawCallInfo set to deferred!\n");
			}

			if (!m_SkyBoxMesh)
			{
				PrintError("Attempted to draw GBUffer contents to cubemap before skybox object was created!\n");
				return;
			}

			if (!m_gBufferQuadVertexBufferData.pDataStart)
			{
				// Generate GBuffer if not already generated
				GenerateGBuffer();
			}
			
			if (drawCallInfo.bRenderToCubemap)
			{
				GLRenderObject* skybox = GetRenderObject(m_SkyBoxMesh->GetRenderID());

				GLRenderObject* cubemapObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
				GLMaterial* cubemapMaterial = &m_Materials[cubemapObject->materialID];
				GLShader* cubemapShader = &m_Shaders[cubemapMaterial->material.shaderID];

				glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				glRenderbufferStorage(GL_RENDERBUFFER, m_CaptureDepthInternalFormat,
					(GLsizei)cubemapMaterial->material.cubemapSamplerSize.x, 
					(GLsizei)cubemapMaterial->material.cubemapSamplerSize.y);

				glUseProgram(cubemapShader->program);

				glBindVertexArray(skybox->VAO);
				glBindBuffer(GL_ARRAY_BUFFER, skybox->VBO);

				UpdateMaterialUniforms(cubemapObject->materialID);
				UpdatePerObjectUniforms(cubemapObject->materialID, 
					skybox->gameObject->GetTransform()->GetWorldTransform());

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
				glDepthFunc(GL_ALWAYS);
				glDepthMask(GL_FALSE);
				glUniformMatrix4fv(cubemapMaterial->uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);

				glBindRenderbuffer(GL_RENDERBUFFER, 0);

				// Override cam pos with reflection probe pos
				glm::vec3 reflectionProbePos(cubemapObject->gameObject->GetTransform()->GetWorldPosition());
				glUniform4fv(cubemapMaterial->uniformIDs.camPos, 1, &reflectionProbePos.x);

				for (i32 face = 0; face < 6; ++face)
				{
					glUniformMatrix4fv(cubemapMaterial->uniformIDs.view, 1, false, &m_CaptureViews[face][0][0]);

					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerID, 0);

					// Draw cube (TODO: Use "cube" object to be less confusing)
					glDrawArrays(skybox->topology, 0, (GLsizei)skybox->vertexBufferData->VertexCount);
				}
			}
			else
			{
				glBindFramebuffer(GL_FRAMEBUFFER, m_Offscreen0FBO);
				glBindRenderbuffer(GL_RENDERBUFFER, m_Offscreen0RBO);

				glDepthMask(GL_TRUE);

				glClear(GL_COLOR_BUFFER_BIT); // Don't clear depth - we just copied it over from the GBuffer

				GLRenderObject* gBufferQuad = GetRenderObject(m_GBufferQuadRenderID);

				GLMaterial* material = &m_Materials[gBufferQuad->materialID];
				GLShader* glShader = &m_Shaders[material->material.shaderID];
				Shader* shader = &glShader->shader;
				glUseProgram(glShader->program);

				glBindVertexArray(gBufferQuad->VAO);
				glBindBuffer(GL_ARRAY_BUFFER, gBufferQuad->VBO);

				UpdateMaterialUniforms(gBufferQuad->materialID);
				UpdatePerObjectUniforms(gBufferQuad->renderID);

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

				glDepthFunc(gBufferQuad->depthTestReadFunc);

				glDepthMask(gBufferQuad->depthWriteEnable);

				glDrawArrays(gBufferQuad->topology, 0, (GLsizei)gBufferQuad->vertexBufferData->VertexCount);
			}
		}

		void GLRenderer::DrawForwardObjects(const DrawCallInfo& drawCallInfo)
		{
			if (drawCallInfo.bDeferred)
			{
				PrintError("DrawForwardObjects was called with a drawCallInfo which is set to be deferred!\n");
			}

			for (std::vector<GLRenderObject*>& batch : m_ForwardRenderObjectBatches)
			{
				DrawRenderObjectBatch(batch, drawCallInfo);
			}
		}

		void GLRenderer::DrawEditorObjects(const DrawCallInfo& drawCallInfo)
		{
			DrawRenderObjectBatch(m_EditorRenderObjectBatch, drawCallInfo);

			const std::vector<GameObject*> selectedObjects = g_EngineInstance->GetSelectedObjects();
			if (!selectedObjects.empty())
			{
				std::vector<GLRenderObject*> selectedObjectRenderBatch;
				for (GameObject* selectedObject : selectedObjects)
				{
					RenderID renderID = selectedObject->GetRenderID();
					if (renderID != InvalidRenderID)
					{
						GLRenderObject* renderObject = GetRenderObject(renderID);
						if (renderObject)
						{
							selectedObjectRenderBatch.push_back(renderObject);
						}
					}
				}


				static const glm::vec4 color0 = { 0.95f, 0.95f, 0.95f, 0.4f };
				static const glm::vec4 color1 = { 0.85f, 0.15f, 0.85f, 0.4f };
				real pulseSpeed = 8.0f;
				GetMaterial(m_SelectedObjectMatID).colorMultiplier = Lerp(color0, color1, sin(g_SecElapsedSinceProgramStart * pulseSpeed) * 0.5f + 0.5f);

				DrawCallInfo selectedObjectsDrawInfo = {};
				selectedObjectsDrawInfo.materialOverride = m_SelectedObjectMatID;
				selectedObjectsDrawInfo.bWireframe = true;
				DrawRenderObjectBatch(selectedObjectRenderBatch, selectedObjectsDrawInfo);
			}
		}

		void GLRenderer::DrawOffscreenTexture()
		{
			SpriteQuadDrawInfo drawInfo = {};

			drawInfo.FBO = m_Offscreen1FBO;
			drawInfo.RBO = m_Offscreen1RBO;

			bool bFXAAEnabled = (m_bPostProcessingEnabled && m_PostProcessSettings.bEnableFXAA);

			if (!bFXAAEnabled)
			{
				drawInfo.FBO = 0;
				drawInfo.RBO = 0;
			}

			glm::vec3 pos(0.0f);
			glm::quat rot = glm::quat(glm::vec3(0.0));
			glm::vec3 scale(1.0f);
			glm::vec4 color(1.0f);

			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = false;
			drawInfo.bWriteDepth = false;
			drawInfo.pos = pos;
			drawInfo.rotation = rot;
			drawInfo.scale = scale;
			drawInfo.materialID = m_PostProcessMatID;
			drawInfo.color = color;
			drawInfo.anchor = AnchorPoint::CENTER;
			drawInfo.textureHandleID = m_OffscreenTexture0Handle.id;
			drawInfo.spriteObjectRenderID = m_Quad2DRenderID;

			DrawSpriteQuad(drawInfo);

			if (bFXAAEnabled)
			{
				drawInfo.FBO = 0;
				drawInfo.RBO = 0;
				scale.y = -1.0f;

				drawInfo.textureHandleID = m_OffscreenTexture1Handle.id;
				drawInfo.materialID = m_PostFXAAMatID;
				DrawSpriteQuad(drawInfo);
			}

			{
				const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
				glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Offscreen0RBO);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
				glBlitFramebuffer(0, 0, frameBufferSize.x, frameBufferSize.y,
								  0, 0, frameBufferSize.x, frameBufferSize.y,
								  GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			}

		}

		void GLRenderer::DrawScreenSpaceSprites()
		{
			PROFILE_BEGIN("Render > DrawScreenSpaceSprites > Display profiler frame");
			Profiler::DrawDisplayedFrame();
			PROFILE_END("Render > DrawScreenSpaceSprites > Display profiler frame");

			for (const SpriteQuadDrawInfo& drawInfo : m_QueuedSSSprites)
			{
				DrawSpriteQuad(drawInfo);
			}
			m_QueuedSSSprites.clear();

			static glm::vec3 pos(0.01f, -0.01f, 0.0f);

			if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_RIGHT))
			{
				pos.x += g_DeltaTime * 1.0f;
			}
			if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT))
			{
				pos.x -= g_DeltaTime * 1.0f;
			}
			if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_UP))
			{
				pos.y += g_DeltaTime * 1.0f;
			}
			if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_DOWN))
			{
				pos.y -= g_DeltaTime * 1.0f;
			}

			//glm::vec4 color(1.0f);
			//
			//SpriteQuadDrawInfo drawInfo = {};
			//drawInfo.bScreenSpace = true;
			//drawInfo.bReadDepth = false;
			//drawInfo.bWriteDepth = false;
			//drawInfo.pos = pos;
			//drawInfo.scale = glm::vec3(1.0, -1.0, 1.0f);
			//drawInfo.materialID = m_SpriteMatID;
			//drawInfo.anchor = AnchorPoint::WHOLE;
			//drawInfo.inputTextureHandle = m_WorkTexture->handle;
			//drawInfo.spriteObjectRenderID = m_Quad3DRenderID;
			//DrawSpriteQuad(drawInfo);

			//drawInfo.scale = glm::vec3(0.25f, -0.25f, 1.0f);
			//drawInfo.anchor = AnchorPoint::BOTTOM_LEFT;
			//DrawSpriteQuad(drawInfo);

			//drawInfo.anchor = AnchorPoint::LEFT;
			//DrawSpriteQuad(drawInfo);

			//drawInfo.anchor = AnchorPoint::TOP_LEFT;
			//DrawSpriteQuad(drawInfo);

			//drawInfo.anchor = AnchorPoint::TOP;
			//DrawSpriteQuad(drawInfo);

			//drawInfo.color = glm::vec4(1.0f, sin(g_SecElapsedSinceProgramStart) * 0.3f + 0.7f, 0.5f, 1.0f);
			//drawInfo.anchor = AnchorPoint::TOP_RIGHT;
			//DrawSpriteQuad(drawInfo);

			//drawInfo.anchor = AnchorPoint::RIGHT;
			//DrawSpriteQuad(drawInfo);

			//drawInfo.anchor = AnchorPoint::BOTTOM_RIGHT;
			//DrawSpriteQuad(drawInfo);

			//drawInfo.anchor = AnchorPoint::BOTTOM;
			//DrawSpriteQuad(drawInfo);

			//drawInfo.anchor = AnchorPoint::CENTER;
			//DrawSpriteQuad(drawInfo);
		}

		void GLRenderer::DrawWorldSpaceSprites()
		{
			for (SpriteQuadDrawInfo& drawInfo : m_QueuedWSSprites)
			{
				drawInfo.FBO = m_Offscreen0FBO;
				drawInfo.RBO = m_Offscreen0RBO;
				DrawSpriteQuad(drawInfo);
			}
			m_QueuedWSSprites.clear();

			glm::vec3 scale(1.0f, -1.0f, 1.0f);

			SpriteQuadDrawInfo drawInfo = {};
			drawInfo.FBO = m_Offscreen0FBO;
			drawInfo.RBO = m_Offscreen0RBO;
			drawInfo.bScreenSpace = false;
			drawInfo.bReadDepth = true;
			drawInfo.bWriteDepth = true;
			drawInfo.scale = scale;
			drawInfo.materialID = m_SpriteMatID;
			drawInfo.spriteObjectRenderID = m_Quad3DRenderID;

			BaseCamera* cam = g_CameraManager->CurrentCamera();
			glm::vec3 camPos = cam->GetPosition();
			glm::vec3 camUp = cam->GetUp();
			for (const PointLight& pointLight : m_PointLights)
			{
				if (pointLight.enabled)
				{
					drawInfo.pos = pointLight.position;
					drawInfo.color = pointLight.color * 1.5f;
					drawInfo.textureHandleID = m_LoadedTextures[m_PointLightIconID]->handle;
					glm::mat4 rotMat = glm::lookAt(camPos, (glm::vec3)pointLight.position, camUp);
					drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
					DrawSpriteQuad(drawInfo);
				}
			}
			
			if (m_DirectionalLight.enabled)
			{
				drawInfo.color = m_DirectionalLight.color * 1.5f;
				drawInfo.pos = m_DirectionalLight.position;
				drawInfo.textureHandleID = m_LoadedTextures[m_DirectionalLightIconID]->handle;
				glm::mat4 rotMat = glm::lookAt(camPos, (glm::vec3)m_DirectionalLight.position, camUp);
				drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
				DrawSpriteQuad(drawInfo);
			}
		}

		void GLRenderer::DrawSpriteQuad(const SpriteQuadDrawInfo& drawInfo)
		{
			RenderID spriteRenderID = drawInfo.spriteObjectRenderID;
			if (spriteRenderID == InvalidRenderID)
			{
				if (drawInfo.bScreenSpace)
				{
					spriteRenderID = m_Quad2DRenderID;
				}
				else
				{
					spriteRenderID = m_Quad3DRenderID;
				}
			}
			GLRenderObject* spriteRenderObject = GetRenderObject(spriteRenderID);
			if (!spriteRenderObject ||
				(spriteRenderObject->editorObject && !g_EngineInstance->IsRenderingEditorObjects()))
			{
				return;
			}

			spriteRenderObject->materialID = drawInfo.materialID;
			if (spriteRenderObject->materialID == InvalidMaterialID)
			{
				spriteRenderObject->materialID = m_SpriteMatID;
			}

			GLMaterial& spriteMaterial = m_Materials[spriteRenderObject->materialID];
			GLShader& spriteShader = m_Shaders[spriteMaterial.material.shaderID];

			glUseProgram(spriteShader.program);

			const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			const real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;

			if (spriteShader.shader.dynamicBufferUniforms.HasUniform("model"))
			{
				glm::vec3 translation = drawInfo.pos;
				glm::quat rotation = drawInfo.rotation;
				glm::vec3 scale = drawInfo.scale;

				if (!drawInfo.bRaw)
				{
					if (drawInfo.bScreenSpace)
					{
						glm::vec2 normalizedTranslation;
						glm::vec2 normalizedScale;
						NormalizeSpritePos(translation, drawInfo.anchor, scale, normalizedTranslation, normalizedScale);

						translation = glm::vec3(normalizedTranslation, 0.0f);
						scale = glm::vec3(normalizedScale, 1.0f);
					}
					else
					{
						translation.x /= aspectRatio;
					}
				}

				glm::mat4 model = (glm::translate(glm::mat4(1.0f), translation) *
								   glm::mat4(rotation) *
								   glm::scale(glm::mat4(1.0f), scale));

				glUniformMatrix4fv(spriteMaterial.uniformIDs.model, 1, true, &model[0][0]);
			}

			if (spriteShader.shader.constantBufferUniforms.HasUniform("view"))
			{
				if (drawInfo.bScreenSpace)
				{
					glm::mat4 view = glm::mat4(1.0f);

					glUniformMatrix4fv(spriteMaterial.uniformIDs.view, 1, false, &view[0][0]);
				}
				else
				{
					glm::mat4 view = g_CameraManager->CurrentCamera()->GetView();

					glUniformMatrix4fv(spriteMaterial.uniformIDs.view, 1, false, &view[0][0]);
				}
			}

			if (spriteShader.shader.constantBufferUniforms.HasUniform("projection"))
			{
				if (drawInfo.bScreenSpace)
				{
					real r = aspectRatio;
					real t = 1.0f;
					glm::mat4 projection = glm::ortho(-r, r, -t, t);

					glUniformMatrix4fv(spriteMaterial.uniformIDs.projection, 1, false, &projection[0][0]);
				}
				else
				{
					glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();

					glUniformMatrix4fv(spriteMaterial.uniformIDs.projection, 1, false, &projection[0][0]);
				}
			}

			if (spriteShader.shader.dynamicBufferUniforms.HasUniform("colorMultiplier"))
			{
				glUniform4fv(spriteMaterial.uniformIDs.colorMultiplier, 1, &drawInfo.color.r);
			}

			bool bEnableAlbedoSampler = (drawInfo.textureHandleID != 0 && drawInfo.bEnableAlbedoSampler);
			if (spriteShader.shader.dynamicBufferUniforms.HasUniform("enableAlbedoSampler"))
			{
				// TODO: glUniform1ui vs glUniform1i ?
				glUniform1ui(spriteMaterial.uniformIDs.enableAlbedoSampler, bEnableAlbedoSampler ? 1 : 0);
			}

			// http://www.graficaobscura.com/matrix/
			if (spriteShader.shader.dynamicBufferUniforms.HasUniform("contrastBrightnessSaturation"))
			{
				glm::mat4 contrastBrightnessSaturation;
				if (m_bPostProcessingEnabled)
				{
					real sat = m_PostProcessSettings.saturation;
					glm::vec3 brightness = m_PostProcessSettings.brightness;
					glm::vec3 offset = m_PostProcessSettings.offset;

					glm::vec3 wgt(0.3086f, 0.6094f, 0.0820f);
					real a = (1.0f - sat) * wgt.r + sat;
					real b = (1.0f - sat) * wgt.r;
					real c = (1.0f - sat) * wgt.r;
					real d = (1.0f - sat) * wgt.g;
					real e = (1.0f - sat) * wgt.g + sat;
					real f = (1.0f - sat) * wgt.g;
					real g = (1.0f - sat) * wgt.b;
					real h = (1.0f - sat) * wgt.b;
					real i = (1.0f - sat) * wgt.b + sat;
					glm::mat4 satMat = {
						a, b, c, 0,
						d, e, f, 0,
						g, h, i, 0,
						0, 0, 0, 1
					};

					contrastBrightnessSaturation = glm::translate(glm::scale(satMat, brightness), offset);
				}
				else
				{
					contrastBrightnessSaturation = glm::mat4(1.0f);
				}

				glUniformMatrix4fv(spriteMaterial.uniformIDs.contrastBrightnessSaturation, 1, false, &contrastBrightnessSaturation[0][0]);
			}

			glViewport(0, 0, (GLsizei)frameBufferSize.x, (GLsizei)frameBufferSize.y);

			glBindFramebuffer(GL_FRAMEBUFFER, drawInfo.FBO);
			glBindRenderbuffer(GL_RENDERBUFFER, drawInfo.RBO);

			glBindVertexArray(spriteRenderObject->VAO);
			glBindBuffer(GL_ARRAY_BUFFER, spriteRenderObject->VBO);

			if (bEnableAlbedoSampler)
			{
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, drawInfo.textureHandleID);
			}

			// TODO: Use member
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			if (drawInfo.bReadDepth)
			{
				glDepthFunc(GL_LEQUAL);
			}
			else
			{
				glDepthFunc(GL_ALWAYS);
			}

			if (drawInfo.bWriteDepth)
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
			glDepthFunc(spriteRenderObject->depthTestReadFunc);
			glDepthMask(spriteRenderObject->depthWriteEnable);
			glDrawArrays(spriteRenderObject->topology, 0, (GLsizei)spriteRenderObject->vertexBufferData->VertexCount);
		}

		void GLRenderer::DrawText()
		{
			bool bHasText = false;
			for (BitmapFont* font : m_Fonts)
			{
				if (font->m_BufferSize > 0)
				{
					bHasText = true;
					break;
				}
			}

			if (!bHasText)
			{
				return;
			}

			GLMaterial& fontMaterial = m_Materials[m_FontMatID];
			GLShader& fontShader = m_Shaders[fontMaterial.material.shaderID];

			glUseProgram(fontShader.program);

			// TODO: Allow per-string shadows? (currently only per-font is doable)
			//if (fontShader.shader.dynamicBufferUniforms.HasUniform("shadow"))
			//{
			//	static glm::vec2 shadow(0.01f, 0.1f);
			//	glUniform2f(glGetUniformLocation(fontShader.program, "shadow"), shadow.x, shadow.y);
			//}
			
			//if (fontShader.shader.dynamicBufferUniforms.HasUniform("colorMultiplier"))
			//{
			//	glm::vec4 color(1.0f);
			//	glUniform4fv(fontMaterial.uniformIDs.colorMultiplier, 1, &color.r);
			//}

			glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();

			glViewport(0, 0, (GLsizei)(frameBufferSize.x), (GLsizei)(frameBufferSize.y));

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glBindRenderbuffer(GL_RENDERBUFFER, 0);

			glBindVertexArray(m_TextQuadVAO);
			glBindBuffer(GL_ARRAY_BUFFER, m_TextQuadVBO);

			for (BitmapFont* font : m_Fonts)
			{
				if (font->m_BufferSize > 0)
				{
					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, font->GetTexture()->handle);

					real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;
					real r = aspectRatio;
					real t = 1.0f;
					glm::mat4 ortho = glm::ortho(-r, r, -t, t);

					// TODO: Find out how font sizes actually work
					//real scale = ((real)font->GetFontSize()) / 12.0f + sin(g_SecElapsedSinceProgramStart) * 2.0f;
					glm::vec3 scaleVec(1.0f);

					glm::mat4 transformMat = glm::scale(glm::mat4(1.0f), scaleVec) * ortho;
					glUniformMatrix4fv(fontMaterial.uniformIDs.transformMat, 1, true, &transformMat[0][0]);

					glm::vec2 texSize = (glm::vec2)font->GetTexture()->GetResolution();
					glUniform2fv(fontMaterial.uniformIDs.texSize, 1, &texSize.r);

					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

					glDisable(GL_CULL_FACE);

					glDepthFunc(GL_ALWAYS);
					glDepthMask(GL_FALSE);

					glEnable(GL_BLEND);
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

					glDrawArrays(GL_POINTS, font->m_BufferStart, font->m_BufferSize);
				}
			}
		}

		bool GLRenderer::LoadFont(BitmapFont** font, 
								  i16 size,
								  const std::string& fontFilePath,
								  const std::string& renderedFontFilePath)
		{
			FT_Error error;
			error = FT_Init_FreeType(&ft);
			if (error != FT_Err_Ok)
			{
				PrintError("Could not init FreeType\n");
				return false;
			}

			std::vector<char> fileMemory;
			ReadFile(fontFilePath, fileMemory, true);

			FT_Face face;
			error = FT_New_Memory_Face(ft, (FT_Byte*)fileMemory.data(), (FT_Long)fileMemory.size(), 0, &face);
			if (error == FT_Err_Unknown_File_Format)
			{
				PrintError("Unhandled font file format: %s\n", fontFilePath.c_str());
				return false;
			}
			else if (error != FT_Err_Ok || !face)
			{
				PrintError("Failed to create new font face: %s\n", fontFilePath.c_str());
				return false;
			}

			error = FT_Set_Char_Size(face,
									 0, size * 64,
									 (FT_UInt)g_Monitor->DPI.x, 
									 (FT_UInt)g_Monitor->DPI.y);

			//FT_Set_Pixel_Sizes(face, 0, fontPixelSize);

			std::string fileName = fontFilePath;
			StripLeadingDirectories(fileName);
			Print("Loaded font file %s\n", fileName.c_str());

			std::string fontName = std::string(face->family_name) + " - " + face->style_name;
			*font = new BitmapFont(size, fontName, face->num_glyphs);
			BitmapFont* newFont = *font; // Shortcut

			m_Fonts.push_back(newFont);

			newFont->m_bUseKerning = FT_HAS_KERNING(face) != 0;

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

				//if (newFont->UseKerning() && glyphIndex)
				//{
				//	for (i32 previous = 0; previous < BitmapFont::CHAR_COUNT - 1; ++previous)
				//	{
				//		FT_Vector delta;
				//
				//		u32 prevIdx = FT_Get_Char_Index(face, previous);
				//		FT_Get_Kerning(face, prevIdx, glyphIndex, FT_KERNING_DEFAULT, &delta);
				//
				//		if (delta.x != 0 || delta.y != 0)
				//		{
				//			std::wstring charKey(std::wstring(1, (wchar_t)previous) + std::wstring(1, (wchar_t)c));
				//			metric->kerning[charKey] =
				//				glm::vec2((real)delta.x / 64.0f, (real)delta.y / 64.0f);
				//		}
				//	}
				//}

				if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER))
				{
					PrintError("Failed to load glyph with index %i\n", glyphIndex);
					continue;
				}


				u32 width = face->glyph->bitmap.width + totPadding * 2;
				u32 height = face->glyph->bitmap.rows + totPadding * 2;


				metric->width = (u16)width;
				metric->height = (u16)height;
				metric->offsetX = (i16)(face->glyph->bitmap_left + totPadding);
				metric->offsetY = -(i16)(face->glyph->bitmap_top + totPadding);
				metric->advanceX = (real)face->glyph->advance.x / 64.0f;

				// Generate atlas coordinates
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

			bool bUsingPreRenderedTexture = false;
			if (FileExists(renderedFontFilePath))
			{
				TextureParameters params(false);
				params.wrapS = GL_CLAMP_TO_EDGE;
				params.wrapT = GL_CLAMP_TO_EDGE;

				GLTexture* fontTex = newFont->SetTexture(new GLTexture(renderedFontFilePath, 4, true, false, false));

				if (fontTex->LoadFromFile())
				{
					bUsingPreRenderedTexture = true;

					for (auto& charPair : characters)
					{
						FontMetric* metric = charPair.second;

						u32 glyphIndex = FT_Get_Char_Index(face, metric->character);
						if (glyphIndex == 0)
						{
							continue;
						}

						metric->texCoord = metric->texCoord / glm::vec2((real)fontTex->width, (real)fontTex->height);
					}
				}
				else
				{
					newFont->m_Texture = nullptr;
					SafeDelete(fontTex);
				}
			}

			if (!bUsingPreRenderedTexture)
			{
				glm::vec2i textureSize(
					std::max(std::max(maxPos[0].x, maxPos[1].x), std::max(maxPos[2].x, maxPos[3].x)),
					std::max(std::max(maxPos[0].y, maxPos[1].y), std::max(maxPos[2].y, maxPos[3].y)));
				newFont->SetTextureSize(textureSize);

				//Setup rendering
				TextureParameters params(false);
				params.wrapS = GL_CLAMP_TO_EDGE;
				params.wrapT = GL_CLAMP_TO_EDGE;

				GLTexture* fontTex = newFont->SetTexture(new GLTexture(fileName, textureSize.x, textureSize.y, GL_RGBA16F, GL_RGBA, GL_FLOAT));
				//fontTex->GenerateEmpty();
				fontTex->Build();
				fontTex->SetParameters(params);

				GLuint captureFBO;
				GLuint captureRBO;

				glGenFramebuffers(1, &captureFBO);
				glGenRenderbuffers(1, &captureRBO);

				glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
				glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);

				glRenderbufferStorage(GL_RENDERBUFFER, m_CaptureDepthInternalFormat, textureSize.x, textureSize.y);
				// TODO: Don't use depth buffer
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fontTex->handle, 0);

				glViewport(0, 0, textureSize.x, textureSize.y);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

				ShaderID computeSDFShaderID;
				GetShaderID("compute_sdf", computeSDFShaderID);
				GLShader& computeSDFShader = m_Shaders[computeSDFShaderID];

				glUseProgram(computeSDFShader.program);

				glUniform1i(glGetUniformLocation(computeSDFShader.program, "highResTex"), 0);
				GLint texChannel = glGetUniformLocation(computeSDFShader.program, "texChannel");
				GLint charResolution = glGetUniformLocation(computeSDFShader.program, "charResolution");
				glUniform1f(glGetUniformLocation(computeSDFShader.program, "spread"), (real)spread);
				glUniform1f(glGetUniformLocation(computeSDFShader.program, "sampleDensity"), (real)sampleDensity);

				glEnable(GL_BLEND);
				glBlendEquation(GL_FUNC_ADD);
				glBlendFunc(GL_ONE, GL_ONE);

				GLRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);

				//Render to Glyphs atlas
				FT_Set_Pixel_Sizes(face, 0, size * sampleDensity);

				for (auto& charPair : characters)
				{
					FontMetric* metric = charPair.second;

					u32 glyphIndex = FT_Get_Char_Index(face, metric->character);
					if (glyphIndex == 0)
					{
						continue;
					}

					if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER))
					{
						PrintError("Failed to load glyph with index %i\n", glyphIndex);
						continue;
					}

					if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP)
					{
						if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
						{
							PrintError("Failed to render glyph with index %i\n", glyphIndex);
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
						PrintError("Couldn't align free type bitmap size\n");
						continue;
					}

					u32 width = alignedBitmap.width;
					u32 height = alignedBitmap.rows;

					if (width == 0 || height == 0)
					{
						continue;
					}

					GLuint texHandle;
					glGenTextures(1, &texHandle);

					glActiveTexture(GL_TEXTURE0);

					glBindTexture(GL_TEXTURE_2D, texHandle);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, alignedBitmap.buffer);

					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					glm::vec4 borderColor(0.0f);
					glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &borderColor.r);

					if (metric->width > 0 && metric->height > 0)
					{
						glm::vec2i res = glm::vec2i(metric->width - padding * 2, metric->height - padding * 2);
						glm::vec2i viewportTL = glm::vec2i(metric->texCoord) + glm::vec2i(padding);

						glViewport(viewportTL.x, viewportTL.y, res.x, res.y);
						glActiveTexture(GL_TEXTURE0);
						glBindTexture(GL_TEXTURE_2D, texHandle);

						glUniform1i(texChannel, metric->channel);
						glUniform2f(charResolution, (real)res.x, (real)res.y);
						glActiveTexture(GL_TEXTURE0);
						glBindVertexArray(gBufferRenderObject->VAO);
						glBindBuffer(GL_ARRAY_BUFFER, gBufferRenderObject->VBO);
						glDrawArrays(gBufferRenderObject->topology, 0,
							(GLsizei)gBufferRenderObject->vertexBufferData->VertexCount);
						glBindVertexArray(0);
					}

					glDeleteTextures(1, &texHandle);

					metric->texCoord = metric->texCoord / glm::vec2((real)textureSize.x, (real)textureSize.y);

					FT_Bitmap_Done(ft, &alignedBitmap);
				}


				// Cleanup
				glDisable(GL_BLEND);
				glDeleteRenderbuffers(1, &captureRBO);
				glDeleteFramebuffers(1, &captureFBO);
			}

			FT_Done_Face(face);
			FT_Done_FreeType(ft);


			// Initialize font shader things
			{
				GLMaterial& mat = m_Materials[m_FontMatID];
				GLShader& shader = m_Shaders[mat.material.shaderID];
				glUseProgram(shader.program);

				glGenVertexArrays(1, &m_TextQuadVAO);
				glGenBuffers(1, &m_TextQuadVBO);


				glBindVertexArray(m_TextQuadVAO);
				glBindBuffer(GL_ARRAY_BUFFER, m_TextQuadVBO);

				//set data and attributes
				// TODO: ?
				i32 bufferSize = 50;
				glBufferData(GL_ARRAY_BUFFER, bufferSize, NULL, GL_DYNAMIC_DRAW);

				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);
				glEnableVertexAttribArray(3);
				glEnableVertexAttribArray(4);

				glVertexAttribPointer(0, (GLint)2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(TextVertex), (GLvoid*)offsetof(TextVertex, pos));
				glVertexAttribPointer(1, (GLint)4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(TextVertex), (GLvoid*)offsetof(TextVertex, color));
				glVertexAttribPointer(2, (GLint)2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(TextVertex), (GLvoid*)offsetof(TextVertex, uv));
				glVertexAttribPointer(3, (GLint)4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(TextVertex), (GLvoid*)offsetof(TextVertex, charSizePixelsCharSizeNorm));
				glVertexAttribIPointer(4, (GLint)1, GL_INT, (GLsizei)sizeof(TextVertex), (GLvoid*)offsetof(TextVertex, channel));
			}

			if (bUsingPreRenderedTexture)
			{
				std::string textureFilePath = renderedFontFilePath;
				StripLeadingDirectories(textureFilePath);
				Print("Loaded font atlas texture from %s\n", textureFilePath.c_str());
			}
			else
			{
				Print("Rendered font atlas for %s\n", fileName.c_str());
			}

			return true;
		}

		void GLRenderer::SetFont(BitmapFont* font)
		{
			m_CurrentFont = font;
		}

		void GLRenderer::AddEditorString(const std::string& str)
		{
			m_EditorMessage = str;
			if (str.empty())
			{
				m_EditorStrSecRemaining = 0.0f;
			}
			else
			{
				m_EditorStrSecRemaining = m_EditorStrSecDuration;
			}
		}

		void GLRenderer::DrawString(const std::string& str,
									const glm::vec4& color,
									AnchorPoint anchor,
									const glm::vec2& pos,
									real spacing,
									bool bRaw)
		{
			assert(m_CurrentFont != nullptr);

			m_CurrentFont->m_TextCaches.emplace_back(str, anchor, pos, color, spacing, bRaw);
		}

		real GLRenderer::GetStringWidth(const TextCache& textCache, BitmapFont* font) const
		{
			real strWidth = 0;

			char prevChar = ' ';
			for (char c : textCache.str)
			{
				if (BitmapFont::IsCharValid(c))
				{
					FontMetric* metric = font->GetMetric(c);

					if (font->UseKerning())
					{
						std::wstring charKey(std::wstring(1, prevChar) + std::wstring(1, c));

						auto iter = metric->kerning.find(charKey);
						if (iter != metric->kerning.end())
						{
							strWidth += iter->second.x;
						}
					}

					strWidth += metric->advanceX + textCache.xSpacing;
				}
			}

			return strWidth;
		}

		real GLRenderer::GetStringHeight(const TextCache& textCache, BitmapFont* font) const
		{
			real strHeight = 0;

			for (char c : textCache.str)
			{
				if (BitmapFont::IsCharValid(c))
				{
					FontMetric* metric = font->GetMetric(c);
					strHeight = glm::max(strHeight, (real)(metric->height));
				}
			}

			return strHeight;
		}

		void GLRenderer::UpdateTextBuffer()
		{
			glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;

			std::vector<TextVertex> textVertices;
			for (BitmapFont* font : m_Fonts)
			{
				real textScale = glm::max(2.0f / (real)frameBufferSize.x, 2.0f / (real)frameBufferSize.y) * 
					(font->GetFontSize() / 12.0f);

				font->m_BufferStart = (i32)(textVertices.size());
				font->m_BufferSize = 0;

				for (TextCache& textCache : font->m_TextCaches)
				{
					std::string currentStr = textCache.str;

					bool bUseLetterYOffsets = !textCache.letterYOffsets.empty();
					if (bUseLetterYOffsets)
					{
						assert(textCache.letterYOffsets.size() == currentStr.size());
					}

					real totalAdvanceX = 0;

					glm::vec2 basePos(0.0f);

					if (!textCache.bRaw)
					{
						real strWidth = GetStringWidth(textCache, font) * textScale;
						real strHeight = GetStringHeight(textCache, font) * textScale;

						switch (textCache.anchor)
						{
						case AnchorPoint::TOP_LEFT:
							basePos = glm::vec3(-aspectRatio, -1.0f + strHeight / 2.0f, 0.0f);
							break;
						case AnchorPoint::TOP:
							basePos = glm::vec3(-strWidth / 2.0f, -1.0f + strHeight / 2.0f, 0.0f);
							break;
						case AnchorPoint::TOP_RIGHT:
							basePos = glm::vec3(aspectRatio - strWidth, -1.0f + strHeight / 2.0f, 0.0f);
							break;
						case AnchorPoint::RIGHT:
							basePos = glm::vec3(aspectRatio - strWidth, 0.0f, 0.0f);
							break;
						case AnchorPoint::BOTTOM_RIGHT:
							basePos = glm::vec3(aspectRatio - strWidth, 1.0f - strHeight / 2.0f, 0.0f);
							break;
						case AnchorPoint::BOTTOM:
							basePos = glm::vec3(-strWidth / 2.0f, 1.0f - strHeight / 2.0f, 0.0f);
							break;
						case AnchorPoint::BOTTOM_LEFT:
							basePos = glm::vec3(-aspectRatio, 1.0f - strHeight / 2.0f, 0.0f);
							break;
						case AnchorPoint::LEFT:
							basePos = glm::vec3(-aspectRatio, 0.0f, 0.0f);
							break;
						case AnchorPoint::CENTER: // Fall through
						case AnchorPoint::WHOLE:
							basePos = glm::vec3(-strWidth / 2.0f, 0.0f, 0.0f);
							break;
						default:
							break;
						}
					}

					char prevChar = ' ';
					for (u32 j = 0; j < currentStr.length(); ++j)
					{
						char c = currentStr[j];

						if (BitmapFont::IsCharValid(c))
						{
							FontMetric* metric = font->GetMetric(c);
							if (metric->bIsValid)
							{
								if (c == ' ')
								{
									totalAdvanceX += metric->advanceX + textCache.xSpacing;
									prevChar = c;
									continue;
								}
								
								real yOff = (bUseLetterYOffsets ? textCache.letterYOffsets[j] : 0.0f);

								glm::vec2 pos = 
									glm::vec2(textCache.pos.x * (textCache.bRaw ? 1.0f : aspectRatio), textCache.pos.y) +
									glm::vec2(totalAdvanceX + metric->offsetX, -metric->offsetY + yOff) * textScale;

								if (font->UseKerning())
								{
									std::wstring charKey(std::wstring(1, prevChar) + std::wstring(1, c));
								
									auto iter = metric->kerning.find(charKey);
									if (iter != metric->kerning.end())
									{
										pos += iter->second * textScale;
									}
								}

								glm::vec4 charSizePixelsCharSizeNorm(
									metric->width, metric->height,
									metric->width * textScale, metric->height * textScale);

								i32 texChannel = (i32)metric->channel;

								TextVertex vert{};
								vert.pos = basePos + pos;
								vert.uv = metric->texCoord;
								vert.color = textCache.color;
								vert.charSizePixelsCharSizeNorm = charSizePixelsCharSizeNorm;
								vert.channel = texChannel;

								textVertices.push_back(vert);

								totalAdvanceX += metric->advanceX + textCache.xSpacing;
							}
							else
							{
								PrintWarn("Attempted to draw char with invalid metric: %c in font %s\n", c, font->m_Name.c_str());
							}
						}
						else
						{
							PrintWarn("Attempted to draw invalid char: %c in font %s\n", c, font->m_Name.c_str());
						}

						prevChar = c;
					}
				}

				font->m_BufferSize = (i32)(textVertices.size()) - font->m_BufferStart;
				font->m_TextCaches.clear();
			}

			u32 bufferByteCount = (u32)(textVertices.size() * sizeof(TextVertex));

			glBindVertexArray(m_TextQuadVAO);
			glBindBuffer(GL_ARRAY_BUFFER, m_TextQuadVBO);
			glBufferData(GL_ARRAY_BUFFER, bufferByteCount, textVertices.data(), GL_DYNAMIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindVertexArray(0);
		}

		void GLRenderer::DrawRenderObjectBatch(const std::vector<GLRenderObject*>& batchedRenderObjects, const DrawCallInfo& drawCallInfo)
		{
			if (batchedRenderObjects.empty())
			{
				return;
			}

			MaterialID materialID = drawCallInfo.materialOverride;

			if (materialID == InvalidMaterialID)
			{
				materialID = batchedRenderObjects[0]->materialID;
			}
			GLMaterial* material = &m_Materials[materialID];
			GLShader* glShader = &m_Shaders[material->material.shaderID];
			Shader* shader = &glShader->shader;
			glUseProgram(glShader->program);

			if (drawCallInfo.bWireframe)
			{
				glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			}

			for (GLRenderObject* renderObject : batchedRenderObjects)
			{
				if (!renderObject->gameObject->IsVisible())
				{
					continue;
				}

				if (!renderObject->vertexBufferData)
				{
					PrintError("Attempted to draw object which contains no vertex buffer data: %s\n", renderObject->gameObject->GetName().c_str());
					return;
				}

				glBindVertexArray(renderObject->VAO);
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);

				if (renderObject->enableCulling)
				{
					glEnable(GL_CULL_FACE);

					glCullFace(renderObject->cullFace);
				}
				else
				{
					glDisable(GL_CULL_FACE);
				}

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

				glDepthFunc(renderObject->depthTestReadFunc);
				glDepthMask(renderObject->depthWriteEnable);

				UpdatePerObjectUniforms(renderObject->renderID, materialID);

				BindTextures(shader, material);

				if (drawCallInfo.bRenderToCubemap)
				{
					// renderObject->gameObject->IsStatic()

					GLRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
					GLMaterial* cubemapMaterial = &m_Materials[cubemapRenderObject->materialID];

					glm::vec2 cubemapSize = cubemapMaterial->material.cubemapSamplerSize;
					
					glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
					glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
					glRenderbufferStorage(GL_RENDERBUFFER, m_CaptureDepthInternalFormat, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);
					glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);

					if (material->uniformIDs.projection == 0)
					{
						PrintWarn("Attempted to draw object to cubemap but uniformIDs.projection is not set on object: %s\n",
								  renderObject->gameObject->GetName().c_str());
						continue;
					}

					// Use capture projection matrix
					glUniformMatrix4fv(material->uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
					
					// TODO: Test if this is actually correct
					glm::vec3 cubemapTranslation = -cubemapRenderObject->gameObject->GetTransform()->GetWorldPosition();
					for (size_t face = 0; face < 6; ++face)
					{
						glm::mat4 view = glm::translate(m_CaptureViews[face], cubemapTranslation);

						// This doesn't work because it flips the winding order of things (I think), maybe just account for that?
						// Flip vertically to match cubemap, cubemap shouldn't even be captured here eventually?
						//glm::mat4 view = glm::translate(glm::scale(m_CaptureViews[face], glm::vec3(1.0f, -1.0f, 1.0f)), cubemapTranslation);
						
						glUniformMatrix4fv(material->uniformIDs.view, 1, false, &view[0][0]);

						if (drawCallInfo.bDeferred)
						{
							//constexpr i32 numBuffers = 3;
							//u32 attachments[numBuffers] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
							//glDrawBuffers(numBuffers, attachments);

							for (size_t j = 0; j < cubemapMaterial->cubemapSamplerGBuffersIDs.size(); ++j)
							{
								glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + j, 
									GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerGBuffersIDs[j].id, 0);
							}
						}
						else
						{
							glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
								GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerID, 0);
						}

						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 
							GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapDepthSamplerID, 0);

						if (renderObject->indexed)
						{
							glDrawElements(renderObject->topology, (GLsizei)renderObject->indices->size(), 
								GL_UNSIGNED_INT, (void*)renderObject->indices->data());
						}
						else
						{
							glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
						}

					}
				}
				else
				{
					glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
					glViewport(0, 0, (GLsizei)frameBufferSize.x, (GLsizei)frameBufferSize.y);

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
					}
					else
					{
						glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
					}
				}

				if (m_bDisplayBoundingVolumes && renderObject->gameObject)
				{
					MeshComponent* mesh = renderObject->gameObject->GetMeshComponent();
					if (mesh)
					{
						btVector3 centerWS = Vec3ToBtVec3(mesh->GetBoundingSphereCenterPointWS());
						m_PhysicsDebugDrawer->drawSphere(centerWS, 0.1f, btVector3(0.8f, 0.2f, 0.1f));
						m_PhysicsDebugDrawer->drawSphere(centerWS, mesh->GetScaledBoundingSphereRadius(), btVector3(0.2f, 0.8f, 0.1f));

						Transform* transform = renderObject->gameObject->GetTransform();

						//glm::vec3 transformedMin = glm::vec3(transform->GetWorldTransform() * glm::vec4(mesh->m_MinPoint, 1.0f));
						//glm::vec3 transformedMax = glm::vec3(transform->GetWorldTransform() * glm::vec4(mesh->m_MaxPoint, 1.0f));
						//btVector3 minPos = Vec3ToBtVec3(transformedMin);
						//btVector3 maxPos = Vec3ToBtVec3(transformedMax);
						//m_PhysicsDebugDrawer->drawSphere(minPos, 0.1f, btVector3(0.2f, 0.8f, 0.1f));
						//m_PhysicsDebugDrawer->drawSphere(maxPos, 0.1f, btVector3(0.2f, 0.8f, 0.1f));

						btVector3 scaledMin = Vec3ToBtVec3(transform->GetWorldScale() * mesh->m_MinPoint);
						btVector3 scaledMax = Vec3ToBtVec3(transform->GetWorldScale() * mesh->m_MaxPoint);

						btTransform transformBT = TransformToBtTransform(*transform);
						m_PhysicsDebugDrawer->drawBox(scaledMin, scaledMax, transformBT, btVector3(0.85f, 0.8f, 0.85f));
					}
				}
			}

			if (drawCallInfo.bWireframe)
			{
				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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
						if (tex.textureID == InvalidID)
						{
							PrintError("TextureID is invalid! material: %s, binding: %i\n", 
									   glMaterial->material.name.c_str(), binding);
						}
						else
						{
							GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)binding);
							glActiveTexture(activeTexture);
							glBindTexture(tex.target, (GLuint)tex.textureID);
						}
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
				PrintWarn("Attempted to bind frame buffers on material that doesn't contain any framebuffers!\n");
				return startingBinding;
			}

			u32 binding = startingBinding;
			for (auto& frameBufferPair : material->frameBuffers)
			{
				GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)binding);
				glActiveTexture(activeTexture);
				glBindTexture(GL_TEXTURE_2D, *((GLuint*)frameBufferPair.second));
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
				PrintWarn("Attempted to bind GBuffer samplers on material doesn't contain any GBuffer samplers!\n");
				return startingBinding;
			}

			u32 binding = startingBinding;
			for (gl::GLCubemapGBuffer& cubemapGBuffer : glMaterial->cubemapSamplerGBuffersIDs)
			{
				GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)binding);
				glActiveTexture(activeTexture);
				glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapGBuffer.id);
				++binding;
			}

			return binding;
		}

		void GLRenderer::CreateOffscreenFrameBuffer(u32* FBO, u32* RBO, const glm::vec2i& size, TextureHandle& handle)
		{
			glGenFramebuffers(1, FBO);
			glBindFramebuffer(GL_FRAMEBUFFER, *FBO);

			glGenRenderbuffers(1, RBO);
			glBindRenderbuffer(GL_RENDERBUFFER, *RBO);
			glRenderbufferStorage(GL_RENDERBUFFER, m_OffscreenDepthBufferInternalFormat, size.x, size.y);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *RBO);

			GenerateFrameBufferTexture(&handle.id,
									   0,
									   handle.internalFormat,
									   handle.format,
									   handle.type,
									   size);

			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				PrintError("Offscreen frame buffer is incomplete!\n");
			}
		}

		void GLRenderer::RemoveMaterial(MaterialID materialID)
		{
			assert(materialID != InvalidMaterialID);

			m_Materials.erase(materialID);
		}

		void GLRenderer::DoGameObjectContextMenu(GameObject** gameObjectRef)
		{
			static const char* renameObjectPopupLabel = "##rename-game-object";
			static const char* renameObjectButtonStr = "Rename";
			static const char* duplicateObjectButtonStr = "Duplicate...";
			static const char* deletePopupStr = "Delete object";
			static const char* deleteButtonStr = "Delete";
			static const char* deleteCancelButtonStr = "Cancel";

			GameObject* gameObject = *gameObjectRef;

			std::string contextMenuIDStr = "context window game object " + gameObject->GetName() + std::to_string(gameObject->GetRenderID());
			static std::string newObjectName = gameObject->GetName();
			const size_t maxStrLen = 256;

			bool bItemClicked = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && 
								ImGui::IsMouseClicked(1);
			if (bItemClicked)
			{
				newObjectName = gameObject->GetName();
				newObjectName.resize(maxStrLen);
			}

			if (ImGui::BeginPopupContextItem(contextMenuIDStr.c_str()))
			{
				bool bRename = ImGui::InputText(renameObjectPopupLabel,
												(char*)newObjectName.data(), 
												maxStrLen, 
												ImGuiInputTextFlags_EnterReturnsTrue);

				ImGui::SameLine();

				bRename |= ImGui::Button(renameObjectButtonStr);

				bool bInvalidName = std::string(newObjectName.c_str()).empty();

				if (bRename && !bInvalidName)
				{
					// Remove excess trailing \0 chars
					newObjectName = std::string(newObjectName.c_str());

					gameObject->SetName(newObjectName);

					ImGui::CloseCurrentPopup();
				}

				if (DoDuplicateGameObjectButton(gameObject, duplicateObjectButtonStr))
				{
					*gameObjectRef = nullptr;
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button(deleteButtonStr))
				{
					ImGui::OpenPopup(deletePopupStr);
				}

				if (ImGui::BeginPopupModal(deletePopupStr, NULL,
					ImGuiWindowFlags_AlwaysAutoResize |
					ImGuiWindowFlags_NoSavedSettings |
					ImGuiWindowFlags_NoNavInputs))
				{
					static std::string objectName = gameObject->GetName();

					ImGui::PushStyleColor(ImGuiCol_Text, g_WarningTextColor);
					std::string textStr = "Are you sure you want to permanently delete " + objectName + "?";
					ImGui::Text(textStr.c_str());
					ImGui::PopStyleColor();

					ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);
					if (ImGui::Button(deleteButtonStr))
					{
						if (g_SceneManager->CurrentScene()->DestroyGameObject(gameObject, true))
						{
							*gameObjectRef = nullptr;
							ImGui::CloseCurrentPopup();
						}
						else
						{
							PrintWarn("Failed to delete game object: %s\n", gameObject->GetName().c_str());
						}
					}
					ImGui::PopStyleColor();
					ImGui::PopStyleColor();
					ImGui::PopStyleColor();

					ImGui::SameLine();

					if (ImGui::Button(deleteCancelButtonStr))
					{
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}

				ImGui::EndPopup();
			}
		}

		void GLRenderer::DoCreateGameObjectButton(const char* buttonName, const char* popupName)
		{
			static const char* defaultNewNameBase = "New_Object_";
			static const char* newObjectNameInputLabel = "##new-object-name";
			static const char* createButtonStr = "Create";
			static const char* cancelStr = "Cancel";

			static std::string newObjectName;

			if (ImGui::Button(buttonName))
			{
				ImGui::OpenPopup(popupName);
				i32 highestNoNameObj = -1;
				i16 maxNumChars = 2;
				const std::vector<GameObject*> allObjects = g_SceneManager->CurrentScene()->GetAllObjects();
				for (GameObject* gameObject : allObjects)
				{
					if (StartsWith(gameObject->GetName(), defaultNewNameBase))
					{
						i16 numChars;
						i32 num = GetNumberEndingWith(gameObject->GetName(), numChars);
						if (num != -1)
						{
							highestNoNameObj = glm::max(highestNoNameObj, num);
							maxNumChars = glm::max(maxNumChars, maxNumChars);
						}
					}
				}
				newObjectName = defaultNewNameBase + IntToString(highestNoNameObj + 1, maxNumChars);
			}

			if (ImGui::BeginPopupModal(popupName, NULL,
				ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoNavInputs))
			{
				const size_t maxStrLen = 256;
				newObjectName.resize(maxStrLen);


				bool bCreate = ImGui::InputText(newObjectNameInputLabel,
												(char*)newObjectName.data(),
												maxStrLen,
												ImGuiInputTextFlags_EnterReturnsTrue);

				bCreate |= ImGui::Button(createButtonStr);

				bool bInvalidName = std::string(newObjectName.c_str()).empty();

				if (bCreate && !bInvalidName)
				{
					// Remove excess trailing \0 chars
					newObjectName = std::string(newObjectName.c_str());

					if (!newObjectName.empty())
					{
						GameObject* newGameObject = new GameObject(newObjectName, GameObjectType::OBJECT);

						g_SceneManager->CurrentScene()->AddRootObject(newGameObject);

						ImGui::CloseCurrentPopup();
					}
				}

				ImGui::SameLine();

				if (ImGui::Button(cancelStr))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}
		}

		bool GLRenderer::DoDuplicateGameObjectButton(GameObject* objectToCopy, const char* buttonName)
		{
			static const char* duplicateObjectButtonStr = "Duplicate";

			if (ImGui::Button(buttonName))
			{
				GameObject* newGameObject = objectToCopy->CopySelfAndAddToScene(nullptr, true);

				g_EngineInstance->SetSelectedObject(newGameObject);

				return true;
			}

			return false;
		}

		bool GLRenderer::DoTextureSelector(const char* label,
										   const std::vector<GLTexture*>& textures,
										   i32* selectedIndex, 
										   bool* bGenerateSampler)
		{
			bool bValueChanged = false;

			std::string currentTexName = (*selectedIndex == 0 ? "NONE" : textures[*selectedIndex - 1]->GetName().c_str());
			if (ImGui::BeginCombo(label, currentTexName.c_str()))
			{
				for (i32 i = 0; i < (i32)textures.size() + 1; i++)
				{
					bool bTextureSelected = (*selectedIndex == i);

					if (i == 0)
					{
						if (ImGui::Selectable("NONE", bTextureSelected))
						{
							*bGenerateSampler = false;

							*selectedIndex = i;
							bValueChanged = true;
						}
					}
					else
					{
						std::string textureName = textures[i - 1]->GetName();
						if (ImGui::Selectable(textureName.c_str(), bTextureSelected))
						{
							if (*selectedIndex == 0)
							{
								*bGenerateSampler = true;
							}

							*selectedIndex = i;
							bValueChanged = true;
						}

						if (ImGui::IsItemHovered())
						{
							DoTexturePreviewTooltip(textures[i - 1]);
						}
					}
					if (bTextureSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			return bValueChanged;
		}

		void GLRenderer::ImGuiUpdateTextureIndexOrMaterial(bool bUpdateTextureMaterial,
			const std::string& texturePath,
			std::string& matTexturePath,
			GLTexture* texture,
			i32 i, 
			i32* textureIndex, 
			u32* samplerID)
		{
			if (bUpdateTextureMaterial)
			{
				if (*textureIndex == 0)
				{
					matTexturePath = "";
					*samplerID = 0;
				}
				else if (i == *textureIndex - 1)
				{
					matTexturePath = texturePath;
					if (texture)
					{
						*samplerID = texture->handle;
					}
				}
			}
			else
			{
				if (matTexturePath.empty())
				{
					*textureIndex = 0;
				}
				else if (texturePath.compare(matTexturePath) == 0)
				{
					*textureIndex = i + 1;
				}
			}
		}

		void GLRenderer::DoTexturePreviewTooltip(GLTexture* texture)
		{
			ImGui::BeginTooltip();

			ImVec2 cursorPos = ImGui::GetCursorPos();

			real textureAspectRatio = (real)texture->width / (real)texture->height;
			real texSize = 128.0f;

			if (texture->channelCount == 4)
			{
				real tiling = 3.0f;
				ImVec2 uv0(0.0f, 0.0f);
				ImVec2 uv1(tiling * textureAspectRatio, tiling);
				GLTexture* alphaBGTexture = m_LoadedTextures[m_AlphaBGTextureID];
				ImGui::Image((void*)alphaBGTexture->handle, ImVec2(texSize * textureAspectRatio, texSize), uv0, uv1);
			}

			ImGui::SetCursorPos(cursorPos);

			ImGui::Image((void*)texture->handle, ImVec2(texSize * textureAspectRatio, texSize));

			ImGui::EndTooltip();
		}

		void GLRenderer::DrawLoadingTextureQuad()
		{
			SpriteQuadDrawInfo drawInfo = {};
			GLTexture* loadingTexture = m_LoadedTextures[m_LoadingTextureID];
			real textureAspectRatio = loadingTexture->width / (real)loadingTexture->height;
			drawInfo.scale = glm::vec3(textureAspectRatio, -1.0f, 1.0f);
			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = false;
			drawInfo.bWriteDepth = false;
			drawInfo.materialID = m_SpriteMatID;
			drawInfo.anchor = AnchorPoint::WHOLE;
			drawInfo.textureHandleID = m_LoadedTextures[m_LoadingTextureID]->handle;
			drawInfo.spriteObjectRenderID = m_Quad3DRenderID;

			DrawSpriteQuad(drawInfo);
		}

		bool GLRenderer::GetLoadedTexture(const std::string& filePath, GLTexture** texture)
		{
			for (auto iter = m_LoadedTextures.begin(); iter != m_LoadedTextures.end(); ++iter)
			{
				if ((*iter)->GetRelativeFilePath().compare(filePath) == 0)
				{
					*texture = *iter;
					return true;
				}
			}

			return false;
		}

		void GLRenderer::ReloadShaders()
		{
			UnloadShaders();
			LoadShaders();

			g_Renderer->AddEditorString("Reloaded shaders");
		}

		void GLRenderer::UnloadShaders()
		{
			for (GLShader& shader : m_Shaders)
			{
				glDeleteProgram(shader.program);
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
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT |
				(u32)VertexAttribute::TANGENT |
				(u32)VertexAttribute::BITANGENT |
				(u32)VertexAttribute::NORMAL;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("modelInvTranspose");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableDiffuseSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableNormalSampler");
			m_Shaders[shaderID].shader.vertexAttributes =
			++shaderID;

			// Deferred combine
			m_Shaders[shaderID].shader.deferred = false; // Sounds strange but this isn't deferred
			// m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.depthWriteEnable = false; // Disable depth writing
			m_Shaders[shaderID].shader.needBRDFLUT = true;
			m_Shaders[shaderID].shader.needIrradianceSampler = true;
			m_Shaders[shaderID].shader.needPrefilteredMap = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("camPos");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("exposure");
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
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION; // Used as 3D texture coord into cubemap

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("camPos");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("exposure");
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
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

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
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::TANGENT |
				(u32)VertexAttribute::BITANGENT |
				(u32)VertexAttribute::NORMAL;

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
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("exposure");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableCubemapSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("cubemapSampler");
			++shaderID;

			// Equirectangular to Cube
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needHDREquirectangularSampler = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("hdrEquirectangularSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Irradiance
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Prefilter
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// BRDF
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Sprite
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("colorMultiplier");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableAlbedoSampler");
			++shaderID;

			// Post processing
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION_2D |
				(u32)VertexAttribute::UV;

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("colorMultiplier");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("contrastBrightnessSaturation");
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
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION_2D |
				(u32)VertexAttribute::UV;

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Compute SDF
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

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
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION_2D |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::EXTRA_VEC4 |
				(u32)VertexAttribute::EXTRA_INT;

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("transformMat");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("texSize");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("threshold");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("shadow");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("soften");
			++shaderID;

			for (GLShader& shader : m_Shaders)
			{
				shader.program = glCreateProgram();

				if (!LoadGLShaders(shader.program, shader))
				{
					std::string fileNames = shader.shader.vertexShaderFilePath + " & " + shader.shader.fragmentShaderFilePath;
					if (!shader.shader.geometryShaderFilePath.empty())
					{
						fileNames += " & " + shader.shader.geometryShaderFilePath;
					}
					PrintError("Couldn't load/compile shaders: %s\n", fileNames.c_str());
				}

				LinkProgram(shader.program);

				// No need to keep the code in memory
				shader.shader.vertexShaderCode.clear();
				shader.shader.vertexShaderCode.shrink_to_fit();
				shader.shader.fragmentShaderCode.clear();
				shader.shader.fragmentShaderCode.shrink_to_fit();
				shader.shader.geometryShaderCode.clear();
				shader.shader.geometryShaderCode.shrink_to_fit();
			}
		}

		void GLRenderer::UpdateMaterialUniforms(MaterialID materialID)
		{
			GLMaterial* material = &m_Materials[materialID];
			GLShader* shader = &m_Shaders[material->material.shaderID];
			
			glUseProgram(shader->program);

			glm::mat4 proj = g_CameraManager->CurrentCamera()->GetProjection();
			glm::mat4 view = g_CameraManager->CurrentCamera()->GetView();
			glm::mat4 viewInv = glm::inverse(view);
			glm::mat4 viewProj = proj * view;
			glm::vec4 camPos = glm::vec4(g_CameraManager->CurrentCamera()->GetPosition(), 0.0f);
			real exposure = g_CameraManager->CurrentCamera()->exposure;

			static const char* viewStr = "view";
			if (shader->shader.constantBufferUniforms.HasUniform(viewStr))
			{
				glUniformMatrix4fv(material->uniformIDs.view, 1, false, &view[0][0]);
			}

			static const char* viewInvStr = "viewInv";
			if (shader->shader.constantBufferUniforms.HasUniform(viewInvStr))
			{
				glUniformMatrix4fv(material->uniformIDs.viewInv, 1, false, &viewInv[0][0]);
			}

			static const char* projectionStr = "projection";
			if (shader->shader.constantBufferUniforms.HasUniform(projectionStr))
			{
				glUniformMatrix4fv(material->uniformIDs.projection, 1, false, &proj[0][0]);
			}

			static const char* viewProjectionStr = "viewProjection";
			if (shader->shader.constantBufferUniforms.HasUniform(viewProjectionStr))
			{
				glUniformMatrix4fv(material->uniformIDs.viewProjection, 1, false, &viewProj[0][0]);
			}

			static const char* camPosStr = "camPos";
			if (shader->shader.constantBufferUniforms.HasUniform(camPosStr))
			{
				glUniform4f(material->uniformIDs.camPos,
					camPos.x,
					camPos.y,
					camPos.z,
					camPos.w);
			}

			static const char* exposureStr = "exposure";
			if (shader->shader.constantBufferUniforms.HasUniform(exposureStr))
			{
				glUniform1f(material->uniformIDs.exposure, exposure);
			}

			static const char* dirLightStr = "dirLight";
			if (shader->shader.constantBufferUniforms.HasUniform(dirLightStr))
			{
				static const char* dirLightEnabledStr = "dirLight.enabled";
				if (m_DirectionalLight.enabled)
				{
					SetUInt(material->material.shaderID, dirLightEnabledStr, 1);
					static const char* dirLightDirectionStr = "dirLight.direction";
					SetVec4f(material->material.shaderID, dirLightDirectionStr, m_DirectionalLight.direction);
					static const char* dirLightColorStr = "dirLight.color";
					SetVec4f(material->material.shaderID, dirLightColorStr, m_DirectionalLight.color * m_DirectionalLight.brightness);
				}
				else
				{
					SetUInt(material->material.shaderID, dirLightEnabledStr, 0);
				}
			}

			static const char* pointLightsStr = "pointLights";
			if (shader->shader.constantBufferUniforms.HasUniform(pointLightsStr))
			{
				for (size_t i = 0; i < MAX_POINT_LIGHT_COUNT; ++i)
				{
					const std::string numberStr(std::to_string(i));
					const char* numberCStr = numberStr.c_str();
					static const i32 strStartLen = 16;
					char pointLightStrStart[strStartLen]; // Handles up to 99 numbers
					strcpy_s(pointLightStrStart, "pointLights[");
					strcat_s(pointLightStrStart, numberCStr);
					strcat_s(pointLightStrStart, "]");
					strcat_s(pointLightStrStart, "\0");

					char enabledStr[strStartLen + 8];
					strcpy_s(enabledStr, pointLightStrStart);
					static const char* dotEnabledStr = ".enabled";
					strcat_s(enabledStr, dotEnabledStr);
					if (i < m_PointLights.size())
					{
						if (m_PointLights[i].enabled)
						{
							SetUInt(material->material.shaderID, enabledStr, 1);

							char positionStr[strStartLen + 9];
							strcpy_s(positionStr, pointLightStrStart);
							static const char* dotPositionStr = ".position";
							strcat_s(positionStr, dotPositionStr);
							SetVec4f(material->material.shaderID, positionStr, m_PointLights[i].position);

							char colorStr[strStartLen + 6];
							strcpy_s(colorStr, pointLightStrStart);
							static const char* dotColorStr = ".color";
							strcat_s(colorStr, dotColorStr);
							SetVec4f(material->material.shaderID, colorStr, m_PointLights[i].color * m_PointLights[i].brightness);
						}
						else
						{
							SetUInt(material->material.shaderID, enabledStr, 0);
						}
					}
					else
					{
						SetUInt(material->material.shaderID, enabledStr, 0);
					}
				}
			}

			static const char* texelStepStr = "texelStep";
			if (shader->shader.constantBufferUniforms.HasUniform(texelStepStr))
			{
				glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
				glm::vec2 texelStep(1.0f / frameBufferSize.x, 1.0f / frameBufferSize.y);
				SetVec2f(material->material.shaderID, texelStepStr, texelStep);
			}

			static const char* bDEBUGShowEdgesStr = "bDEBUGShowEdges";
			if (shader->shader.constantBufferUniforms.HasUniform(bDEBUGShowEdgesStr))
			{
				SetInt(material->material.shaderID, bDEBUGShowEdgesStr, m_PostProcessSettings.bEnableFXAADEBUGShowEdges ? 1 : 0);
			}
		}

		void GLRenderer::UpdatePerObjectUniforms(RenderID renderID, MaterialID materialIDOverride /* = InvalidMaterialID */)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject)
			{
				PrintError("Invalid renderID passed to UpdatePerObjectUniforms: %i\n", renderID);
				return;
			}

			glm::mat4 model = renderObject->gameObject->GetTransform()->GetWorldTransform();
			MaterialID matID = materialIDOverride;
			if (matID == InvalidMaterialID)
			{
				matID = renderObject->materialID;
			}
			UpdatePerObjectUniforms(matID, model);
		}

		void GLRenderer::UpdatePerObjectUniforms(MaterialID materialID, const glm::mat4& model)
		{
			// TODO: OPTIMIZATION: Investigate performance impact of caching each uniform and preventing updates to data that hasn't changed

			GLMaterial* material = &m_Materials[materialID];
			GLShader* shader = &m_Shaders[material->material.shaderID];

			glm::mat4 modelInv = glm::inverse(model);
			glm::mat4 proj = g_CameraManager->CurrentCamera()->GetProjection();
			glm::mat4 view = g_CameraManager->CurrentCamera()->GetView();
			glm::mat4 MVP = proj * view * model;
			glm::vec4 colorMultiplier = material->material.colorMultiplier;

			// TODO: Use set functions here (SetFloat, SetMatrix, ...)
			if (shader->shader.dynamicBufferUniforms.HasUniform("model"))
			{
				glUniformMatrix4fv(material->uniformIDs.model, 1, false, &model[0][0]);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("modelInvTranspose"))
			{
				// OpenGL will transpose for us if we set the third param to true
				glUniformMatrix4fv(material->uniformIDs.modelInvTranspose, 1, true, &modelInv[0][0]);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("colorMultiplier"))
			{
				// OpenGL will transpose for us if we set the third param to true
				glUniform4fv(material->uniformIDs.colorMultiplier, 1, &colorMultiplier[0]);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableDiffuseSampler"))
			{
				glUniform1i(material->uniformIDs.enableDiffuseTexture, material->material.enableDiffuseSampler);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableNormalSampler"))
			{
				glUniform1i(material->uniformIDs.enableNormalTexture, material->material.enableNormalSampler);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableCubemapSampler"))
			{
				glUniform1i(material->uniformIDs.enableCubemapTexture, material->material.enableCubemapSampler);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableAlbedoSampler"))
			{
				glUniform1ui(material->uniformIDs.enableAlbedoSampler, material->material.enableAlbedoSampler);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constAlbedo"))
			{
				glUniform4f(material->uniformIDs.constAlbedo, material->material.constAlbedo.x, material->material.constAlbedo.y, material->material.constAlbedo.z, 0);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableMetallicSampler"))
			{
				glUniform1ui(material->uniformIDs.enableMetallicSampler, material->material.enableMetallicSampler);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constMetallic"))
			{
				glUniform1f(material->uniformIDs.constMetallic, material->material.constMetallic);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableRoughnessSampler"))
			{
				glUniform1ui(material->uniformIDs.enableRoughnessSampler, material->material.enableRoughnessSampler);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constRoughness"))
			{
				glUniform1f(material->uniformIDs.constRoughness, material->material.constRoughness);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableAOSampler"))
			{
				glUniform1ui(material->uniformIDs.enableAOSampler, material->material.enableAOSampler);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constAO"))
			{
				glUniform1f(material->uniformIDs.constAO, material->material.constAO);
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableIrradianceSampler"))
			{
				glUniform1i(material->uniformIDs.enableIrradianceSampler, material->material.enableIrradianceSampler);
			}
		}

		void GLRenderer::OnWindowSizeChanged(i32 width, i32 height)
		{
			if (width == 0 || height == 0 || m_gBufferHandle == 0)
			{
				return;
			}

			const glm::vec2i newFrameBufferSize(width, height);

			glBindFramebuffer(GL_FRAMEBUFFER, m_Offscreen0FBO);
			ResizeFrameBufferTexture(m_OffscreenTexture0Handle.id,
									 m_OffscreenTexture0Handle.internalFormat,
									 m_OffscreenTexture0Handle.format,
									 m_OffscreenTexture0Handle.type,
									 newFrameBufferSize);

			ResizeRenderBuffer(m_Offscreen0RBO, newFrameBufferSize, m_OffscreenDepthBufferInternalFormat);


			glBindFramebuffer(GL_FRAMEBUFFER, m_Offscreen1FBO);
			ResizeFrameBufferTexture(m_OffscreenTexture1Handle.id,
									 m_OffscreenTexture1Handle.internalFormat,
									 m_OffscreenTexture1Handle.format,
									 m_OffscreenTexture1Handle.type,
									 newFrameBufferSize);

			ResizeRenderBuffer(m_Offscreen1RBO, newFrameBufferSize, m_OffscreenDepthBufferInternalFormat);


			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);
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

			ResizeRenderBuffer(m_gBufferDepthHandle, newFrameBufferSize, m_OffscreenDepthBufferInternalFormat);
		}

		void GLRenderer::OnSceneChanged()
		{
			// G-Buffer needs to be regenerated using new scene's reflection probe mat ID
			GenerateGBuffer();
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
			outInfo.enableCulling = (renderObject->enableCulling == GL_TRUE);
			outInfo.depthTestReadFunc = GlenumToDepthTestFunc(renderObject->depthTestReadFunc);
			outInfo.depthWriteEnable = (renderObject->depthWriteEnable == GL_TRUE);

			return true;
		}

		void GLRenderer::SetVSyncEnabled(bool enableVSync)
		{
			m_bVSyncEnabled = enableVSync;
			glfwSwapInterval(enableVSync ? 1 : 0);
		}

		bool GLRenderer::GetVSyncEnabled()
		{
			return m_bVSyncEnabled;
		}

		void GLRenderer::SetFloat(ShaderID shaderID, const char* valName, real val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName);
			if (location == -1)
			{
				PrintWarn("Float %s couldn't be found!\n", valName);
			}

			glUniform1f(location, val);
		}

		void GLRenderer::SetInt(ShaderID shaderID, const char* valName, i32 val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName);
			if (location == -1)
			{
				PrintWarn("i32 %s couldn't be found!\n", valName);
			}

			glUniform1i(location, val);
		}

		void GLRenderer::SetUInt(ShaderID shaderID, const char* valName, u32 val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName);
			if (location == -1)
			{
				PrintWarn("u32 %s couldn't be found!\n", valName);
			}

			glUniform1ui(location, val);
		}

		void GLRenderer::SetVec2f(ShaderID shaderID, const char* vecName, const glm::vec2& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName);
			if (location == -1)
			{
				PrintWarn("Vec2f %s couldn't be found!\n", vecName);
			}

			glUniform2f(location, vec[0], vec[1]);
		}

		void GLRenderer::SetVec3f(ShaderID shaderID, const char* vecName, const glm::vec3& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName);
			if (location == -1)
			{
				PrintWarn("Vec3f %s couldn't be found!\n", vecName);
			}

			glUniform3f(location, vec[0], vec[1], vec[2]);
		}

		void GLRenderer::SetVec4f(ShaderID shaderID, const char* vecName, const glm::vec4& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName);
			if (location == -1)
			{
				PrintWarn("Vec4f %s couldn't be found!\n", vecName);
			}

			glUniform4f(location, vec[0], vec[1], vec[2], vec[3]);
		}

		void GLRenderer::SetMat4f(ShaderID shaderID, const char* matName, const glm::mat4& mat)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, matName);
			if (location == -1)
			{
				PrintWarn("Mat4f %s couldn't be found!\n", matName);
			}

			glUniformMatrix4fv(location, 1, false, &mat[0][0]);
		}


		void GLRenderer::GenerateGBufferVertexBuffer()
		{
			if (m_gBufferQuadVertexBufferData.pDataStart == nullptr)
			{
				VertexBufferData::CreateInfo gBufferQuadVertexBufferDataCreateInfo = {};

				gBufferQuadVertexBufferDataCreateInfo.positions_3D = {
					glm::vec3(-1.0f,  -1.0f, 0.0f),
					glm::vec3(3.0f,  -1.0f, 0.0f),
					glm::vec3(-1.0f, 3.0f, 0.0f),
				};

				gBufferQuadVertexBufferDataCreateInfo.texCoords_UV = {
					glm::vec2(0.0f, 0.0f),
					glm::vec2(2.0f, 0.0f),
					glm::vec2(0.0f, 2.0f),
				};

				gBufferQuadVertexBufferDataCreateInfo.attributes = (u32)VertexAttribute::POSITION | (u32)VertexAttribute::UV;

				m_gBufferQuadVertexBufferData.Initialize(&gBufferQuadVertexBufferDataCreateInfo);
			}
		}

		void GLRenderer::GenerateGBuffer()
		{
			if (!m_gBufferQuadVertexBufferData.pDataStart)
			{
				GenerateGBufferVertexBuffer();
			}

			// TODO: Allow user to not set this and have a backup plan (disable deferred rendering?)
			assert(m_ReflectionProbeMaterialID != InvalidMaterialID);

			std::string gBufferMatName = "GBuffer material";
			std::string gBufferName = "GBuffer quad";
			// Remove existing material if present (this be true when reloading the scene)
			{
				MaterialID existingGBufferMatID = InvalidMaterialID;
				if (GetMaterialID(gBufferMatName, existingGBufferMatID))
				{
					RemoveMaterial(existingGBufferMatID);
				}

				for (auto iter = m_PersistentObjects.begin(); iter != m_PersistentObjects.end(); ++iter)
				{
					GameObject* gameObject = *iter;
					if (gameObject->GetName().compare(gBufferName) == 0)
					{
						SafeDelete(gameObject);
						m_PersistentObjects.erase(iter);
						break;
					}
				}

				if (m_GBufferQuadRenderID != InvalidID)
				{
					DestroyRenderObject(m_GBufferQuadRenderID);
				}
			}

			MaterialCreateInfo gBufferMaterialCreateInfo = {};
			gBufferMaterialCreateInfo.name = gBufferMatName;
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

			MaterialID gBufferMatID = InitializeMaterial(&gBufferMaterialCreateInfo);


			GameObject* gBufferQuadGameObject = new GameObject(gBufferName, GameObjectType::NONE);
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

			m_GBufferQuadRenderID = InitializeRenderObject(&gBufferQuadCreateInfo);

			m_gBufferQuadVertexBufferData.DescribeShaderVariables(this, m_GBufferQuadRenderID);

			//GLRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);
		}

		u32 GLRenderer::GetRenderObjectCount() const
		{
			// TODO: Replace function with m_RenderObjects.size()? (only if no nullptr objects exist)
			u32 count = 0;

			for (GLRenderObject* renderObject : m_RenderObjects)
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
			for (GLRenderObject* renderObject : m_RenderObjects)
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
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject)
			{
				PrintError("Invalid renderID passed to DescribeShaderVariable: %i\n", renderID);
				return;
			}

			GLMaterial* material = &m_Materials[renderObject->materialID];
			u32 program = m_Shaders[material->material.shaderID].program;


			glUseProgram(program);

			glBindVertexArray(renderObject->VAO);

			GLint location = glGetAttribLocation(program, variableName.c_str());
			if (location == -1)
			{
				//PrintWarn("Invalid shader variable name: " + variableName);
				glBindVertexArray(0);
				return;
			}
			glEnableVertexAttribArray((GLuint)location);

			GLenum glRenderType = DataTypeToGLType(dataType);
			glVertexAttribPointer((GLuint)location, size, glRenderType, (GLboolean)normalized, stride, pointer);
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
				PrintError("Skybox doesn't have a valid material! Irradiance textures can't be generated\n");
				return;
			}

			for (GLRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject)
				{
					if (m_Materials.find(renderObject->materialID) == m_Materials.end())
					{
						PrintError("Render object contains invalid material ID: %i, material name: %s\n", 
								   renderObject->materialID, renderObject->materialName.c_str());
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
				PrintError("SetRenderObjectMaterialID couldn't find render object with ID %i\n", renderID);
			}
		}

		Material& GLRenderer::GetMaterial(MaterialID matID)
		{
			assert(matID != InvalidMaterialID);

			return m_Materials[matID].material;
		}

		Shader& GLRenderer::GetShader(ShaderID shaderID)
		{
			assert(shaderID != InvalidShaderID);

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

			if (g_EngineInstance->IsRenderingImGui())
			{
				ImGui_ImplGlfwGL3_NewFrame();
			}
		}

		btIDebugDraw* GLRenderer::GetDebugDrawer()
		{
			return m_PhysicsDebugDrawer;
		}

		void GLRenderer::PhysicsDebugRender()
		{
			btDiscreteDynamicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			physicsWorld->debugDrawWorld();
		}

		void GLRenderer::SaveSettingsToDisk(bool bSaveOverDefaults /* = false */, bool bAddEditorStr /* = true */)
		{
			std::string filePath = (bSaveOverDefaults ? m_DefaultSettingsFilePathAbs : m_SettingsFilePathAbs);

			if (bSaveOverDefaults && FileExists(m_SettingsFilePathAbs))
			{
				DeleteFile(m_SettingsFilePathAbs);
			}

			if (filePath.empty())
			{
				PrintError("Failed to save renderer settings to disk: file path is not set!\n");
				return;
			}

			JSONObject rootObject = {};
			rootObject.fields.emplace_back("enable post-processing", JSONValue(m_bPostProcessingEnabled));
			rootObject.fields.emplace_back("enable v-sync", JSONValue(m_bVSyncEnabled));
			rootObject.fields.emplace_back("enable fxaa", JSONValue(m_PostProcessSettings.bEnableFXAA));
			rootObject.fields.emplace_back("brightness", JSONValue(Vec3ToString(m_PostProcessSettings.brightness, 3)));
			rootObject.fields.emplace_back("offset", JSONValue(Vec3ToString(m_PostProcessSettings.offset, 3)));
			rootObject.fields.emplace_back("saturation", JSONValue(m_PostProcessSettings.saturation));

			BaseCamera* cam = g_CameraManager->CurrentCamera();
			rootObject.fields.emplace_back("aperture", JSONValue(cam->aperture));
			rootObject.fields.emplace_back("shutter speed", JSONValue(cam->shutterSpeed));
			rootObject.fields.emplace_back("light sensitivity", JSONValue(cam->lightSensitivity));
			std::string fileContents = rootObject.Print(0);

			if (WriteFile(filePath, fileContents, false))
			{
				if (bAddEditorStr)
				{
					if (bSaveOverDefaults)
					{
						AddEditorString("Saved default renderer settings");
					}
					else
					{
						AddEditorString("Saved renderer settings");
					}
				}
			}
		}

		void GLRenderer::LoadSettingsFromDisk(bool bLoadDefaults /* = false */)
		{
			std::string filePath = (bLoadDefaults ? m_DefaultSettingsFilePathAbs : m_SettingsFilePathAbs);

			if (!bLoadDefaults && !FileExists(m_SettingsFilePathAbs))
			{
				filePath = m_DefaultSettingsFilePathAbs;

				if (!FileExists(filePath))
				{
					PrintError("Failed to find renderer settings files on disk!\n");
					return;
				}
			}

			if (bLoadDefaults && FileExists(m_SettingsFilePathAbs))
			{
				DeleteFile(m_SettingsFilePathAbs);
			}

			JSONObject rootObject;
			if (JSONParser::Parse(filePath, rootObject))
			{
				m_bPostProcessingEnabled = rootObject.GetBool("enable post-processing");
				SetVSyncEnabled(rootObject.GetBool("enable v-sync"));
				m_PostProcessSettings.bEnableFXAA = rootObject.GetBool("enable fxaa");
				m_PostProcessSettings.brightness = ParseVec3(rootObject.GetString("brightness"));
				m_PostProcessSettings.offset = ParseVec3(rootObject.GetString("offset"));
				m_PostProcessSettings.saturation = rootObject.GetFloat("saturation");

				if (rootObject.HasField("aperture"))
				{
					// Assume all exposure control fields are present if one is
					BaseCamera* cam = g_CameraManager->CurrentCamera();
					cam->aperture = rootObject.GetFloat("aperture");
					cam->shutterSpeed = rootObject.GetFloat("shutter speed");
					cam->lightSensitivity = rootObject.GetFloat("light sensitivity");
					cam->CalculateExposure();
				}
			}
			else
			{
				PrintError("Failed to read renderer settings file, but it exists!\n");
			}
		}

		real GLRenderer::GetStringWidth(const std::string& str, BitmapFont* font, real letterSpacing, bool bNormalized) const
		{
			real strWidth = 0;

			char prevChar = ' ';
			for (char c : str)
			{
				if (BitmapFont::IsCharValid(c))
				{
					FontMetric* metric = font->GetMetric(c);

					if (font->UseKerning())
					{
						std::wstring charKey(std::wstring(1, prevChar) + std::wstring(1, c));

						auto iter = metric->kerning.find(charKey);
						if (iter != metric->kerning.end())
						{
							strWidth += iter->second.x;
						}
					}

					strWidth += metric->advanceX + letterSpacing;
				}
			}

			if (bNormalized)
			{
				strWidth /= (real)g_Window->GetFrameBufferSize().x;
			}

			return strWidth;
		}

		real GLRenderer::GetStringHeight(const std::string& str, BitmapFont* font, bool bNormalized) const
		{
			real strHeight = 0;

			for (char c : str)
			{
				if (BitmapFont::IsCharValid(c))
				{
					FontMetric* metric = font->GetMetric(c);
					strHeight = glm::max(strHeight, (real)(metric->height));
				}
			}

			if (bNormalized)
			{
				strHeight /= (real)g_Window->GetFrameBufferSize().y;
			}

			return strHeight;
		}

		void GLRenderer::DrawAssetBrowserImGui()
		{
			static i32 MAX_CHAR_COUNT = 128;

			ImGui::SetNextWindowSize(ImVec2(400.0f, 350.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Asset browser", &m_bShowingAssetBrowser))
			{
				if (ImGui::CollapsingHeader("Materials"))
				{
					static bool bUpdateFields = true;
					const i32 MAX_NAME_LEN = 128;
					static i32 selectedMaterialIndexShort = 0; // Index into shortened array
					static MaterialID selectedMaterialID = 0;
					while (m_Materials[selectedMaterialID].material.engineMaterial &&
						   selectedMaterialID < m_Materials.size() - 1)
					{
						++selectedMaterialID;
					}
					static std::string matName = "";
					static i32 selectedShaderIndex = 0;
					// Texture index values of 0 represent no texture, 1 = first index into textures array and so on
					static i32 albedoTextureIndex = 0;
					static bool bUpdateAlbedoTextureMaterial = false;
					static i32 metallicTextureIndex = 0;
					static bool bUpdateMetallicTextureMaterial = false;
					static i32 roughnessTextureIndex = 0;
					static bool bUpdateRoughessTextureMaterial = false;
					static i32 normalTextureIndex = 0;
					static bool bUpdateNormalTextureMaterial = false;
					static i32 aoTextureIndex = 0;
					static bool bUpdateAOTextureMaterial = false;
					GLMaterial& mat = m_Materials[selectedMaterialID];

					if (bUpdateFields)
					{
						bUpdateFields = false;

						matName = mat.material.name;
						matName.resize(MAX_NAME_LEN);

						i32 i = 0;
						for (GLTexture* texture : m_LoadedTextures)
						{
							std::string texturePath = texture->GetRelativeFilePath();

							ImGuiUpdateTextureIndexOrMaterial(bUpdateAlbedoTextureMaterial,
														 texturePath,
														 mat.material.albedoTexturePath,
														 texture,
														 i,
														 &albedoTextureIndex,
														 &mat.albedoSamplerID);

							ImGuiUpdateTextureIndexOrMaterial(bUpdateMetallicTextureMaterial,
														 texturePath,
														 mat.material.metallicTexturePath,
														 texture,
														 i,
														 &metallicTextureIndex,
														 &mat.metallicSamplerID);

							ImGuiUpdateTextureIndexOrMaterial(bUpdateRoughessTextureMaterial,
														 texturePath,
														 mat.material.roughnessTexturePath,
														 texture,
														 i,
														 &roughnessTextureIndex,
														 &mat.roughnessSamplerID);

							ImGuiUpdateTextureIndexOrMaterial(bUpdateNormalTextureMaterial,
														 texturePath,
														 mat.material.normalTexturePath,
														 texture,
														 i,
														 &normalTextureIndex,
														 &mat.normalSamplerID);

							ImGuiUpdateTextureIndexOrMaterial(bUpdateAOTextureMaterial,
														 texturePath,
														 mat.material.aoTexturePath,
														 texture,
														 i,
														 &aoTextureIndex,
														 &mat.aoSamplerID);

							++i;
						}

						mat.material.enableAlbedoSampler = (albedoTextureIndex > 0);
						mat.material.enableMetallicSampler = (metallicTextureIndex > 0);
						mat.material.enableRoughnessSampler = (roughnessTextureIndex > 0);
						mat.material.enableNormalSampler = (normalTextureIndex > 0);
						mat.material.enableAOSampler = (aoTextureIndex > 0);

						selectedShaderIndex = mat.material.shaderID;
					}

					ImGui::PushItemWidth(160.0f);
					if (ImGui::InputText("Name", (char*)matName.data(), MAX_NAME_LEN, ImGuiInputTextFlags_EnterReturnsTrue))
					{
						// Remove trailing \0 characters
						matName = std::string(matName.c_str());
						mat.material.name = matName;
					}
					ImGui::PopItemWidth();

					ImGui::SameLine();

					struct ShaderFunctor
					{
						static bool GetShaderName(void* data, int idx, const char** out_str)
						{
							*out_str = ((GLShader*)data)[idx].shader.name.c_str();
							return true;
						}
					};
					ImGui::PushItemWidth(240.0f);
					if (ImGui::Combo("Shader", &selectedShaderIndex, &ShaderFunctor::GetShaderName,
						(void*)m_Shaders.data(), m_Shaders.size()))
					{
						mat = m_Materials[selectedMaterialID];
						mat.material.shaderID = selectedShaderIndex;

						bUpdateFields = true;
					}
					ImGui::PopItemWidth();

					ImGui::NewLine();

					ImGui::Columns(2);
					ImGui::SetColumnWidth(0, 240.0f);

					ImGui::ColorEdit3("Albedo", &mat.material.constAlbedo.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);

					if (mat.material.enableMetallicSampler)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
					}
					ImGui::SliderFloat("Metallic", &mat.material.constMetallic, 0.0f, 1.0f, "%.2f");
					if (mat.material.enableMetallicSampler)
					{
						ImGui::PopStyleColor();
					}

					if (mat.material.enableRoughnessSampler)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
					}
					ImGui::SliderFloat("Roughness", &mat.material.constRoughness, 0.0f, 1.0f, "%.2f");
					if (mat.material.enableRoughnessSampler)
					{
						ImGui::PopStyleColor();
					}

					if (mat.material.enableAOSampler)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
					}
					ImGui::SliderFloat("AO", &mat.material.constAO, 0.0f, 1.0f, "%.2f");
					if (mat.material.enableAOSampler)
					{
						ImGui::PopStyleColor();
					}

					ImGui::NextColumn();

					struct TextureFunctor
					{
						static bool GetTextureFileName(void* data, i32 idx, const char** out_str)
						{
							if (idx == 0)
							{
								*out_str = "NONE";
							}
							else
							{
								*out_str = ((GLTexture**)data)[idx - 1]->GetName().c_str();
							}
							return true;
						}
					};

					std::vector<GLTexture*> textures;
					textures.reserve(m_LoadedTextures.size());
					for (GLTexture* texture : m_LoadedTextures)
					{
						textures.push_back(texture);
					}

					bUpdateAlbedoTextureMaterial = DoTextureSelector("Albedo texture", textures,
						&albedoTextureIndex, &mat.material.generateAlbedoSampler);
					bUpdateFields |= bUpdateAlbedoTextureMaterial;
					bUpdateMetallicTextureMaterial = DoTextureSelector("Metallic texture", textures,
						&metallicTextureIndex, &mat.material.generateMetallicSampler);
					bUpdateFields |= bUpdateMetallicTextureMaterial;
					bUpdateRoughessTextureMaterial = DoTextureSelector("Roughness texture", textures,
						&roughnessTextureIndex, &mat.material.generateRoughnessSampler);
					bUpdateFields |= bUpdateRoughessTextureMaterial;
					bUpdateNormalTextureMaterial = DoTextureSelector("Normal texture", textures,
						&normalTextureIndex, &mat.material.generateNormalSampler);
					bUpdateFields |= bUpdateNormalTextureMaterial;
					bUpdateAOTextureMaterial = DoTextureSelector("AO texture", textures, &aoTextureIndex,
						&mat.material.generateAOSampler);
					bUpdateFields |= bUpdateAOTextureMaterial;

					ImGui::NewLine();
					
					ImGui::EndColumns();

					if (ImGui::BeginChild("material list", ImVec2(0.0f, 120.0f), true))
					{
						i32 matShortIndex = 0;
						for (i32 i = 0; i < (i32)m_Materials.size(); ++i)
						{
							if (m_Materials[i].material.engineMaterial)
							{
								continue;
							}

							bool bSelected = (matShortIndex == selectedMaterialIndexShort);
							if (ImGui::Selectable(m_Materials[i].material.name.c_str(), &bSelected))
							{
								if (selectedMaterialIndexShort != matShortIndex)
								{
									selectedMaterialIndexShort = matShortIndex;
									selectedMaterialID = i;
									bUpdateFields = true;
								}
							}

							if (ImGui::BeginPopupContextItem())
							{
								if (ImGui::Button("Duplicate"))
								{
									const Material& dupMat = m_Materials[i].material;

									MaterialCreateInfo createInfo = {};
									createInfo.name = GetIncrementedPostFixedStr(dupMat.name, "new material 00");
									createInfo.shaderName = m_Shaders[dupMat.shaderID].shader.name;
									createInfo.constAlbedo = dupMat.constAlbedo;
									createInfo.constRoughness = dupMat.constRoughness;
									createInfo.constMetallic = dupMat.constMetallic;
									createInfo.constAO = dupMat.constAO;
									createInfo.colorMultiplier = dupMat.colorMultiplier;
									// TODO: Copy other fields
									MaterialID newMaterialID = InitializeMaterial(&createInfo);

									g_SceneManager->CurrentScene()->AddMaterialID(newMaterialID);

									ImGui::CloseCurrentPopup();
								}

								ImGui::EndPopup();
							}

							if (ImGui::IsItemActive())
							{
								if (ImGui::BeginDragDropSource())
								{
									MaterialID draggedMaterialID = i;
									const void* data = (void*)(&draggedMaterialID);
									size_t size = sizeof(MaterialID);

									ImGui::SetDragDropPayload(m_MaterialPayloadCStr, data, size);

									ImGui::Text(m_Materials[i].material.name.c_str());

									ImGui::EndDragDropSource();
								}
							}

							++matShortIndex;
						}
					}
					ImGui::EndChild(); // Material list

					const i32 MAX_MAT_NAME_LEN = 128;
					static std::string newMaterialName = "";

					const char* createMaterialPopupStr = "Create material##popup";
					if (ImGui::Button("Create material"))
					{
						ImGui::OpenPopup(createMaterialPopupStr);
						newMaterialName = "New Material 01";
						newMaterialName.resize(MAX_MAT_NAME_LEN);
					}

					if (ImGui::BeginPopupModal(createMaterialPopupStr, NULL, ImGuiWindowFlags_NoResize))
					{
						ImGui::Text("Name:");
						ImGui::InputText("##NameText", (char*)newMaterialName.data(), MAX_MAT_NAME_LEN);

						ImGui::Text("Shader:");
						static i32 newMatShaderIndex = 0;
						if (ImGui::BeginChild("Shader", ImVec2(0, 120), true))
						{
							i32 i = 0;
							for (GLShader& shader : m_Shaders)
							{
								bool bSelectedShader = (i == newMatShaderIndex);
								if (ImGui::Selectable(shader.shader.name.c_str(), &bSelectedShader))
								{
									newMatShaderIndex = i;
								}

								++i;
							}
						}
						ImGui::EndChild(); // Shader list

						if (ImGui::Button("Create new material"))
						{
							// Remove trailing /0 characters
							newMaterialName = std::string(newMaterialName.c_str());

							MaterialCreateInfo createInfo = {};
							createInfo.name = newMaterialName;
							createInfo.shaderName = m_Shaders[newMatShaderIndex].shader.name;
							MaterialID newMaterialID = InitializeMaterial(&createInfo);

							g_SceneManager->CurrentScene()->AddMaterialID(newMaterialID);

							ImGui::CloseCurrentPopup();
						}

						ImGui::SameLine();

						if (ImGui::Button("Cancel"))
						{
							ImGui::CloseCurrentPopup();
						}

						ImGui::EndPopup();
					}

					ImGui::SameLine();

					ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);
					if (ImGui::Button("Delete material"))
					{
						g_SceneManager->CurrentScene()->RemoveMaterialID(selectedMaterialID);
						RemoveMaterial(selectedMaterialID);
					}
					ImGui::PopStyleColor();
					ImGui::PopStyleColor();
					ImGui::PopStyleColor();
				}

				if (ImGui::CollapsingHeader("Textures"))
				{
					static i32 selectedTextureIndex = 0;
					if (ImGui::BeginChild("texture list", ImVec2(0.0f, 120.0f), true))
					{
						i32 i = 0;
						for (GLTexture* texture : m_LoadedTextures)
						{
							bool bSelected = (i == selectedTextureIndex);
							std::string textureFileName = texture->GetName();
							if (ImGui::Selectable(textureFileName.c_str(), &bSelected))
							{
								selectedTextureIndex = i;
							}

							if (ImGui::BeginPopupContextItem())
							{
								if (ImGui::Button("Reload"))
								{
									texture->Reload();
									ImGui::CloseCurrentPopup();
								}

								ImGui::EndPopup();
							}

							if (ImGui::IsItemHovered())
							{
								DoTexturePreviewTooltip(texture);
							}
							++i;
						}
					}
					ImGui::EndChild();

					if (ImGui::Button("Import Texture"))
					{
						// TODO: Not all textures are directly in this directory! CLEANUP to make more robust
						std::string relativeDirPath = RESOURCE_LOCATION + "textures/";
						std::string absoluteDirectoryStr = RelativePathToAbsolute(relativeDirPath);
						std::string selectedAbsFilePath;
						if (OpenFileDialog("Import texture", absoluteDirectoryStr, selectedAbsFilePath))
						{
							std::string fileNameAndExtension = selectedAbsFilePath;
							StripLeadingDirectories(fileNameAndExtension);
							std::string relativeFilePath = relativeDirPath + fileNameAndExtension;

							Print("Importing texture: %s\n", relativeFilePath.c_str());

							GLTexture* newTexture = new GLTexture(relativeFilePath, 3, false, false, false);
							if (newTexture->LoadFromFile())
							{
								m_LoadedTextures.push_back(newTexture);
							}

							ImGui::CloseCurrentPopup();
						}
					}
				}

				if (ImGui::CollapsingHeader("Meshes"))
				{
					static i32 selectedMeshIndex = 0;
					const i32 MAX_NAME_LEN = 128;
					static std::string selectedMeshName = "";
					static bool bUpdateName = true;

					std::string selectedMeshRelativeFilePath;
					MeshComponent::LoadedMesh* selectedMesh = nullptr;
					i32 meshIdx = 0;
					for (auto iter = MeshComponent::m_LoadedMeshes.begin();
						iter != MeshComponent::m_LoadedMeshes.end();
						++iter)
					{
						if (meshIdx == selectedMeshIndex)
						{
							selectedMesh = (iter->second);
							selectedMeshRelativeFilePath = iter->first;
							break;
						}
						++meshIdx;
					}

					ImGui::Text("Import settings");

					ImGui::Columns(2, "import settings columns", false);
					ImGui::Separator();
					ImGui::Checkbox("Flip U", &selectedMesh->importSettings.flipU); ImGui::NextColumn();
					ImGui::Checkbox("Flip V", &selectedMesh->importSettings.flipV); ImGui::NextColumn();
					ImGui::Checkbox("Swap Normal YZ", &selectedMesh->importSettings.swapNormalYZ); ImGui::NextColumn();
					ImGui::Checkbox("Flip Normal Z", &selectedMesh->importSettings.flipNormalZ); ImGui::NextColumn();
					ImGui::Columns(1);

					if (ImGui::Button("Re-import"))
					{
						std::string relativeFilePath = selectedMeshRelativeFilePath;
						for (GLRenderObject* renderObject : m_RenderObjects)
						{
							if (renderObject && renderObject->gameObject)
							{
								MeshComponent* gameObjectMesh = renderObject->gameObject->GetMeshComponent();
								if (gameObjectMesh &&  gameObjectMesh->GetRelativeFilePath().compare(relativeFilePath) == 0)
								{
									MeshComponent::ImportSettings importSettings = selectedMesh->importSettings;

									MaterialID matID = renderObject->materialID;
									GameObject* gameObject = renderObject->gameObject;

									DestroyRenderObject(gameObject->GetRenderID());
									gameObject->SetRenderID(InvalidRenderID);

									gameObjectMesh->Destroy();
									gameObjectMesh->SetOwner(gameObject);
									gameObjectMesh->SetRequiredAttributesFromMaterialID(matID);
									gameObjectMesh->LoadFromFile(relativeFilePath, &importSettings);
								}
							}
						}
					}

					ImGui::SameLine();

					if (ImGui::Button("Save"))
					{
						g_SceneManager->CurrentScene()->SerializeToFile(true);
					}

					if (ImGui::BeginChild("mesh list", ImVec2(0.0f, 120.0f), true))
					{
						i32 i = 0;
						for (const auto& meshIter : MeshComponent::m_LoadedMeshes)
						{
							bool bSelected = (i == selectedMeshIndex);
							std::string meshFileName = meshIter.first;
							StripLeadingDirectories(meshFileName);
							if (ImGui::Selectable(meshFileName.c_str(), &bSelected))
							{
								selectedMeshIndex = i;
								bUpdateName = true;
							}

							if (ImGui::BeginPopupContextItem())
							{
								if (ImGui::Button("Reload"))
								{
									// TODO: Reload
									ImGui::CloseCurrentPopup();
								}

								ImGui::EndPopup();
							}
							else
							{
								if (ImGui::IsItemActive())
								{
									if (ImGui::BeginDragDropSource())
									{
										const void* data = (void*)(meshIter.first.c_str());
										size_t size = strlen(meshIter.first.c_str()) * sizeof(char);

										ImGui::SetDragDropPayload(m_MeshPayloadCStr, data, size);

										ImGui::Text(meshFileName.c_str());

										ImGui::EndDragDropSource();
									}
								}
							}

							++i;
						}
					}
					ImGui::EndChild();

					if (ImGui::Button("Import Mesh"))
					{
						// TODO: Not all models are directly in this directory! CLEANUP to make more robust
						std::string relativeDirPath = RESOURCE_LOCATION + "meshes/";
						std::string absoluteDirectoryStr = RelativePathToAbsolute(relativeDirPath);
						std::string selectedAbsFilePath;
						if (OpenFileDialog("Import mesh", absoluteDirectoryStr, selectedAbsFilePath))
						{
							Print("Importing mesh: %s\n", selectedAbsFilePath.c_str());

							std::string fileNameAndExtension = selectedAbsFilePath;
							StripLeadingDirectories(fileNameAndExtension);
							std::string relativeFilePath = relativeDirPath + fileNameAndExtension;

							MeshComponent::LoadedMesh* existingMesh = nullptr;
							if (MeshComponent::GetLoadedMesh(relativeFilePath, &existingMesh))
							{
								i32 j = 0;
								for (auto meshPair : MeshComponent::m_LoadedMeshes)
								{
									if (meshPair.first.compare(relativeFilePath) == 0)
									{
										selectedMeshIndex = j;
										break;
									}

									++j;
								}
							}
							else
							{
								MeshComponent::LoadMesh(relativeFilePath);
							}

							ImGui::CloseCurrentPopup();
						}
					}
				}
			}

			ImGui::End();
		}

		void GLRenderer::DrawImGuiItems()
		{
			ImGui::NewLine();

			ImGui::BeginChild("SelectedObject",
							  ImVec2(0.0f, 220.0f),
							  true);

			const std::vector<GameObject*>& selectedObjects = g_EngineInstance->GetSelectedObjects();
			if (selectedObjects.size() >= 1)
			{
				// TODO: Draw common fields for all selected objects?
				GameObject* selectedObject = selectedObjects[0];
				if (selectedObject)
				{
					DrawGameObjectImGui(selectedObject);
				}
			}

			ImGui::EndChild();

			ImGui::NewLine();

			ImGui::Text("Game Objects");

			// Dropping objects onto this text makes them root objects
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(m_GameObjectPayloadCStr);

				if (payload && payload->Data)
				{
					i32 draggedObjectCount = payload->DataSize / sizeof(GameObject*);

					std::vector<GameObject*> draggedGameObjectsVec;
					draggedGameObjectsVec.reserve(draggedObjectCount);
					for (i32 i = 0; i < draggedObjectCount; ++i)
					{
						draggedGameObjectsVec.push_back(*((GameObject**)payload->Data + i));
					}

					if (!draggedGameObjectsVec.empty())
					{
						std::vector<GameObject*> siblings = draggedGameObjectsVec[0]->GetLaterSiblings();

						for (GameObject* draggedGameObject : draggedGameObjectsVec)
						{
							bool bRootObject = draggedGameObject == draggedGameObjectsVec[0];
							bool bRootSibling = Find(siblings, draggedGameObject) != siblings.end();
							// Only re-parent root-most object (leave sub-hierarchy as-is)
							if ((bRootObject || bRootSibling) &&
								draggedGameObject->GetParent())
							{
								draggedGameObject->GetParent()->RemoveChild(draggedGameObject);
								g_SceneManager->CurrentScene()->AddRootObject(draggedGameObject);
							}
						}
					}

				}
				ImGui::EndDragDropTarget();
			}

			std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();
			for (GameObject* rootObject : rootObjects)
			{
				if (DrawGameObjectNameAndChildren(rootObject))
				{
					break;
				}
			}

			DoCreateGameObjectButton("Add object...", "Add object");

			DrawImGuiLights();
		}

		void GLRenderer::DrawUntexturedQuad(const glm::vec2& pos,
			AnchorPoint anchor,
			const glm::vec2& size,
			const glm::vec4& color)
		{
			SpriteQuadDrawInfo drawInfo = {};

			drawInfo.spriteObjectRenderID = m_Quad3DRenderID;
			drawInfo.materialID = m_SpriteMatID;
			drawInfo.scale = glm::vec3(size.x, size.y, 1.0f);
			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = false;
			drawInfo.bWriteDepth = false;
			drawInfo.anchor = anchor;
			drawInfo.color = color;
			drawInfo.pos = glm::vec3(pos.x, pos.y, 1.0f);
			drawInfo.bEnableAlbedoSampler = false;

			DrawSpriteQuad(drawInfo);
		}

		void GLRenderer::DrawUntexturedQuadRaw(const glm::vec2& pos,
			const glm::vec2& size,
			const glm::vec4& color)
		{
			SpriteQuadDrawInfo drawInfo = {};

			drawInfo.spriteObjectRenderID = m_Quad3DRenderID;
			drawInfo.materialID = m_SpriteMatID;
			drawInfo.scale = glm::vec3(size.x, size.y, 1.0f);
			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = false;
			drawInfo.bWriteDepth = false;
			drawInfo.bRaw = true;
			drawInfo.color = color;
			drawInfo.pos = glm::vec3(pos.x, pos.y, 1.0f);
			drawInfo.bEnableAlbedoSampler = false;

			DrawSpriteQuad(drawInfo);
		}

		void GLRenderer::DrawSprite(const SpriteQuadDrawInfo& drawInfo)
		{
			if (drawInfo.bScreenSpace)
			{
				m_QueuedSSSprites.push_back(drawInfo);
			}
			else
			{
				m_QueuedWSSprites.push_back(drawInfo);
			}
		}

		void GLRenderer::DrawGameObjectImGui(GameObject* gameObject)
		{
			RenderID renderID = gameObject->GetRenderID();
			GLRenderObject* renderObject = nullptr;
			std::string objectID;
			if (renderID != InvalidRenderID)
			{
				renderObject = GetRenderObject(renderID);
				objectID = "##" + std::to_string(renderObject->renderID);

				if (!gameObject->IsVisibleInSceneExplorer())
				{
					return;
				}
			}

			ImGui::Text(gameObject->GetName().c_str());

			DoGameObjectContextMenu(&gameObject);

			if (!gameObject)
			{
				// Early return if object was just deleted
				return;
			}

			bool visible = gameObject->IsVisible();
			const std::string objectVisibleLabel("Visible" + objectID + gameObject->GetName());
			if (ImGui::Checkbox(objectVisibleLabel.c_str(), &visible))
			{
				gameObject->SetVisible(visible);
			}

			DrawImGuiForRenderObjectCommon(gameObject);

			if (renderObject)
			{
				GLMaterial& material = m_Materials[renderObject->materialID];

				MaterialID selectedMaterialID = 0;
				i32 selectedMaterialShortIndex = 0;
				std::string currentMaterialName = "NONE";
				i32 matShortIndex = 0;
				for (i32 i = 0; i < (i32)m_Materials.size(); ++i)
				{
					if (m_Materials[i].material.engineMaterial)
					{
						continue;
					}

					if (i == (i32)renderObject->materialID)
					{
						selectedMaterialID = i;
						selectedMaterialShortIndex = matShortIndex;
						currentMaterialName = material.material.name;
						break;
					}

					++matShortIndex;
				}
				if (ImGui::BeginCombo("Material", currentMaterialName.c_str()))
				{
					matShortIndex = 0;
					for (i32 i = 0; i < (i32)m_Materials.size(); ++i)
					{
						if (m_Materials[i].material.engineMaterial)
						{
							continue;
						}

						bool bSelected = (matShortIndex == selectedMaterialShortIndex);
						std::string materialName = m_Materials[i].material.name;
						if (ImGui::Selectable(materialName.c_str(), &bSelected))
						{
							MeshComponent* mesh = gameObject->GetMeshComponent();
							if (mesh)
							{
								mesh->SetMaterialID(i);
							}
							selectedMaterialShortIndex = matShortIndex;
						}

						++matShortIndex;
					}

					ImGui::EndCombo();
				}

				if (ImGui::BeginDragDropTarget())
				{
					const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(m_MaterialPayloadCStr);

					if (payload && payload->Data)
					{
						MaterialID* draggedMaterialID = (MaterialID*)payload->Data;
						if (draggedMaterialID)
						{
							MeshComponent* mesh = gameObject->GetMeshComponent();
							if (mesh)
							{
								mesh->SetMaterialID(*draggedMaterialID);
							}
						}
					}

					ImGui::EndDragDropTarget();
				}

				MeshComponent* mesh = gameObject->GetMeshComponent();
				if (mesh)
				{
					switch (mesh->GetType())
					{
					case MeshComponent::Type::FILE:
					{
						i32 selectedMeshIndex = 0;
						std::string currentMeshFileName = "NONE";
						i32 i = 0;
						for (auto iter : MeshComponent::m_LoadedMeshes)
						{
							std::string meshFileName = iter.first;
							StripLeadingDirectories(meshFileName);
							if (mesh->GetFileName().compare(meshFileName) == 0)
							{
								selectedMeshIndex = i;
								currentMeshFileName = meshFileName;
								break;
							}

							++i;
						}
						if (ImGui::BeginCombo("Mesh", currentMeshFileName.c_str()))
						{
							i = 0;

							for (auto iter = MeshComponent::m_LoadedMeshes.begin();
								iter != MeshComponent::m_LoadedMeshes.end();
								++iter)
							{
								bool bSelected = (i == selectedMeshIndex);
								std::string meshFileName = (*iter).first;
								StripLeadingDirectories(meshFileName);
								if (ImGui::Selectable(meshFileName.c_str(), &bSelected))
								{
									if (selectedMeshIndex != i)
									{
										selectedMeshIndex = i;
										std::string relativeFilePath = (*iter).first;
										MaterialID matID = mesh->GetMaterialID();
										DestroyRenderObject(gameObject->GetRenderID());
										gameObject->SetRenderID(InvalidRenderID);
										mesh->Destroy();
										mesh->SetOwner(gameObject);
										mesh->SetRequiredAttributesFromMaterialID(matID);
										mesh->LoadFromFile(relativeFilePath);
									}
								}

								++i;
							}

							ImGui::EndCombo();
						}

						if (ImGui::BeginPopupContextItem("mesh context menu"))
						{
							if (ImGui::Button("Remove mesh component"))
							{
								gameObject->SetMeshComponent(nullptr);
							}

							ImGui::EndPopup();
						}

						if (ImGui::BeginDragDropTarget())
						{
							const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(m_MeshPayloadCStr);

							if (payload && payload->Data)
							{
								std::string draggedMeshFileName((const char*)payload->Data, payload->DataSize);
								auto meshIter = MeshComponent::m_LoadedMeshes.find(draggedMeshFileName);
								if (meshIter != MeshComponent::m_LoadedMeshes.end())
								{
									std::string newMeshFilePath = meshIter->first;

									if (mesh->GetRelativeFilePath().compare(newMeshFilePath) != 0)
									{
										MaterialID matID = mesh->GetMaterialID();
										DestroyRenderObject(gameObject->GetRenderID());
										gameObject->SetRenderID(InvalidRenderID);
										mesh->Destroy();
										mesh->SetOwner(gameObject);
										mesh->SetRequiredAttributesFromMaterialID(matID);
										mesh->LoadFromFile(newMeshFilePath);
									}
								}
							}
							ImGui::EndDragDropTarget();
						}
					} break;
					case MeshComponent::Type::PREFAB:
					{
						i32 selectedMeshIndex = (i32)mesh->GetShape();
						std::string currentMeshName = MeshComponent::PrefabShapeToString(mesh->GetShape());

						if (ImGui::BeginCombo("Prefab", currentMeshName.c_str()))
						{
							for (i32 i = 0; i < (i32)MeshComponent::PrefabShape::NONE; ++i)
							{
								std::string shapeStr = MeshComponent::PrefabShapeToString((MeshComponent::PrefabShape)i);
								bool bSelected = (selectedMeshIndex == i);
								if (ImGui::Selectable(shapeStr.c_str(), &bSelected))
								{
									if (selectedMeshIndex != i)
									{
										selectedMeshIndex = i;
										MaterialID matID = mesh->GetMaterialID();
										DestroyRenderObject(gameObject->GetRenderID());
										gameObject->SetRenderID(InvalidRenderID);
										mesh->Destroy();
										mesh->SetOwner(gameObject);
										mesh->SetRequiredAttributesFromMaterialID(matID);
										mesh->LoadPrefabShape((MeshComponent::PrefabShape)i);
									}
								}
							}

							ImGui::EndCombo();
						}
					} break;
					}
				}
			}
			else
			{
				if (ImGui::Button("Add mesh component"))
				{
					MaterialID matID = InvalidMaterialID;
					g_Renderer->GetMaterialID("pbr chrome", matID);

					MeshComponent* mesh = gameObject->SetMeshComponent(new MeshComponent(matID, gameObject));
					mesh->SetRequiredAttributesFromMaterialID(matID);
					mesh->LoadFromFile(RESOURCE_LOCATION + "meshes/cube.gltf");
				}
			}

			if (gameObject->GetRigidBody())
			{
				ImGui::Spacing();

				RigidBody* rb = gameObject->GetRigidBody();
				btRigidBody* rbInternal = rb->GetRigidBodyInternal();

				ImGui::Text("Rigid body");

				if (ImGui::BeginPopupContextItem("rb context menu"))
				{
					if (ImGui::Button("Remove rigid body"))
					{
						btDiscreteDynamicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
						physicsWorld->removeRigidBody(rb->GetRigidBodyInternal());
						gameObject->SetRigidBody(nullptr);
						rb = nullptr;
					}

					ImGui::EndPopup();
				}

				if (rb != nullptr)
				{
					bool bStatic = rb->IsStatic();
					if (ImGui::Checkbox("Static", &bStatic))
					{
						rb->SetStatic(bStatic);
					}

					bool bKinematic = rb->IsKinematic();
					if (ImGui::Checkbox("Kinematic", &bKinematic))
					{
						rb->SetKinematic(bKinematic);
					}

					ImGui::PushItemWidth(80.0f);
					{
						i32 group = rb->GetGroup();
						if (ImGui::InputInt("Group", &group, 1, 16))
						{
							group = glm::clamp(group, -1, 16);
							rb->SetGroup(group);
						}

						ImGui::SameLine();

						i32 mask = rb->GetMask();
						if (ImGui::InputInt("Mask", &mask, 1, 16))
						{
							mask = glm::clamp(mask, -1, 16);
							rb->SetMask(mask);
						}
					}
					ImGui::PopItemWidth();

					// TODO: Array of buttons for each category
					i32 flags = rb->GetPhysicsFlags();
					if (ImGui::SliderInt("Flags", &flags, 0, 16))
					{
						rb->SetPhysicsFlags(flags);
					}

					real mass = rb->GetMass();
					if (ImGui::SliderFloat("Mass", &mass, 0.0f, 1000.0f))
					{
						rb->SetMass(mass);
					}

					real friction = rb->GetFriction();
					if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f))
					{
						rb->SetFriction(friction);
					}

					ImGui::Spacing();

					btCollisionShape* shape = rb->GetRigidBodyInternal()->getCollisionShape();
					std::string shapeTypeStr = CollisionShapeTypeToString(shape->getShapeType());

					if (ImGui::BeginCombo("Shape", shapeTypeStr.c_str()))
					{
						i32 selectedColliderShape = -1;
						for (i32 i = 0; i < ARRAY_LENGTH(g_CollisionTypes); ++i)
						{
							if (g_CollisionTypes[i] == shape->getShapeType())
							{
								selectedColliderShape = i;
								break;
							}
						}

						if (selectedColliderShape == -1)
						{
							PrintError("Failed to find collider shape in array!\n");
						}
						else
						{
							for (i32 i = 0; i < ARRAY_LENGTH(g_CollisionTypes); ++i)
							{
								bool bSelected = (i == selectedColliderShape);
								const char* colliderShapeName = g_CollisionTypeStrs[i];
								if (ImGui::Selectable(colliderShapeName, &bSelected))
								{
									if (selectedColliderShape != i)
									{
										selectedColliderShape = i;

										switch (g_CollisionTypes[selectedColliderShape])
										{
										case BOX_SHAPE_PROXYTYPE:
										{
											btBoxShape* newShape = new btBoxShape(btVector3(1.0f, 1.0f, 1.0f));
											gameObject->SetCollisionShape(newShape);
										} break;
										case SPHERE_SHAPE_PROXYTYPE:
										{
											btSphereShape* newShape = new btSphereShape(1.0f);
											gameObject->SetCollisionShape(newShape);
										} break;
										case CAPSULE_SHAPE_PROXYTYPE:
										{
											btCapsuleShapeZ* newShape = new btCapsuleShapeZ(1.0f, 1.0f);
											gameObject->SetCollisionShape(newShape);
										} break;
										case CYLINDER_SHAPE_PROXYTYPE:
										{
											btCylinderShape* newShape = new btCylinderShape(btVector3(1.0f, 1.0f, 1.0f));
											gameObject->SetCollisionShape(newShape);
										} break;
										case CONE_SHAPE_PROXYTYPE:
										{
											btConeShape* newShape = new btConeShape(1.0f, 1.0f);
											gameObject->SetCollisionShape(newShape);
										} break;
										}
									}
								}
							}
						}

						ImGui::EndCombo();
					}

					glm::vec3 scale = gameObject->GetTransform()->GetWorldScale();
					switch (shape->getShapeType())
					{
					case BOX_SHAPE_PROXYTYPE:
					{
						btBoxShape* boxShape = (btBoxShape*)shape;
						btVector3 halfExtents = boxShape->getHalfExtentsWithMargin();
						glm::vec3 halfExtentsG = BtVec3ToVec3(halfExtents);
						halfExtentsG /= scale;

						real maxExtent = 1000.0f;
						if (ImGui::DragFloat3("Half extents", &halfExtentsG.x, 0.1f, 0.0f, maxExtent))
						{
							halfExtents = Vec3ToBtVec3(halfExtentsG);
							btBoxShape* newShape = new btBoxShape(halfExtents);
							gameObject->SetCollisionShape(newShape);
						}
					} break;
					case SPHERE_SHAPE_PROXYTYPE:
					{
						btSphereShape* sphereShape = (btSphereShape*)shape;
						real radius = sphereShape->getRadius();
						radius /= scale.x;

						real maxExtent = 1000.0f;
						if (ImGui::DragFloat("radius", &radius, 0.1f, 0.0f, maxExtent))
						{
							btSphereShape* newShape = new btSphereShape(radius);
							gameObject->SetCollisionShape(newShape);
						}
					} break;
					case CAPSULE_SHAPE_PROXYTYPE:
					{
						btCapsuleShapeZ* capsuleShape = (btCapsuleShapeZ*)shape;
						real radius = capsuleShape->getRadius();
						real halfHeight = capsuleShape->getHalfHeight();
						radius /= scale.x;
						halfHeight /= scale.x;

						real maxExtent = 1000.0f;
						bool bUpdateShape = ImGui::DragFloat("radius", &radius, 0.1f, 0.0f, maxExtent);
						bUpdateShape |= ImGui::DragFloat("height", &halfHeight, 0.1f, 0.0f, maxExtent);

						if (bUpdateShape)
						{
							btCapsuleShapeZ* newShape = new btCapsuleShapeZ(radius, halfHeight * 2.0f);
							gameObject->SetCollisionShape(newShape);
						}
					} break;
					case CYLINDER_SHAPE_PROXYTYPE:
					{
						btCylinderShape* cylinderShape = (btCylinderShape*)shape;
						btVector3 halfExtents = cylinderShape->getHalfExtentsWithMargin();
						glm::vec3 halfExtentsG = BtVec3ToVec3(halfExtents);
						halfExtentsG /= scale;

						real maxExtent = 1000.0f;
						if (ImGui::DragFloat3("Half extents", &halfExtentsG.x, 0.1f, 0.0f, maxExtent))
						{
							halfExtents = Vec3ToBtVec3(halfExtentsG);
							btCylinderShape* newShape = new btCylinderShape(halfExtents);
							gameObject->SetCollisionShape(newShape);
						}
					} break;
					}

					glm::vec3 localOffsetPos = rb->GetLocalPosition();
					if (ImGui::DragFloat3("Pos offset", &localOffsetPos.x, 0.05f))
					{
						rb->SetLocalPosition(localOffsetPos);
					}

					glm::vec3 localOffsetRotEuler = glm::eulerAngles(rb->GetLocalRotation()) * 90.0f;
					if (ImGui::DragFloat3("Rot offset", &localOffsetRotEuler.x, 0.1f))
					{
						rb->SetLocalRotation(glm::quat(localOffsetRotEuler / 90.0f));
					}

					ImGui::Spacing();

					glm::vec3 linearVel = BtVec3ToVec3(rb->GetRigidBodyInternal()->getLinearVelocity());
					if (ImGui::DragFloat3("linear vel", &linearVel.x, 0.05f))
					{
						rbInternal->setLinearVelocity(Vec3ToBtVec3(linearVel));
					}

					if (ImGui::IsItemClicked(1))
					{
						rbInternal->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
					}

					glm::vec3 angularVel = BtVec3ToVec3(rb->GetRigidBodyInternal()->getAngularVelocity());
					if (ImGui::DragFloat3("angular vel", &angularVel.x, 0.05f))
					{
						rbInternal->setAngularVelocity(Vec3ToBtVec3(angularVel));
					}

					if (ImGui::IsItemClicked(1))
					{
						rbInternal->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
					}


					//glm::vec3 localOffsetScale = rb->GetLocalScale();
					//if (ImGui::DragFloat3("Scale offset", &localOffsetScale.x, 0.01f))
					//{
					//	real epsilon = 0.001f;
					//	localOffsetScale.x = glm::max(localOffsetScale.x, epsilon);
					//	localOffsetScale.y = glm::max(localOffsetScale.y, epsilon);
					//	localOffsetScale.z = glm::max(localOffsetScale.z, epsilon);

					//	rb->SetLocalScale(localOffsetScale);
					//}

				}
			}
			else
			{
				if (ImGui::Button("Add rigid body"))
				{
					RigidBody* rb = gameObject->SetRigidBody(new RigidBody());
					btVector3 btHalfExtents(0.5f, 0.5f, 0.5f);
					btBoxShape* boxShape = new btBoxShape(btHalfExtents);

					gameObject->SetCollisionShape(boxShape);
					rb->Initialize(boxShape, gameObject->GetTransform());
					rb->GetRigidBodyInternal()->setUserPointer(gameObject);
				}
			}
		}

		bool GLRenderer::DrawGameObjectNameAndChildren(GameObject* gameObject)
		{
			RenderID renderID = gameObject->GetRenderID();
			GLRenderObject* renderObject = nullptr;
			std::string objectName = gameObject->GetName();
			std::string objectID;
			if (renderID == InvalidRenderID)
			{
				objectID = "##" + objectName;
			}
			else
			{
				renderObject = GetRenderObject(renderID);
				objectID = "##" + std::to_string(renderObject->renderID);

				if (!gameObject->IsVisibleInSceneExplorer())
				{
					return false;
				}
			}

			const std::vector<GameObject*>& gameObjectChildren = gameObject->GetChildren();
			bool bHasChildren = !gameObjectChildren.empty();
			if (bHasChildren)
			{
				bool bChildVisibleInSceneExplorer = false;
				// Make sure at least one child is visible in scene explorer
				for (GameObject* child : gameObjectChildren)
				{
					if (child->IsVisibleInSceneExplorer())
					{
						bChildVisibleInSceneExplorer = true;
						break;
					}
				}
	
				if (!bChildVisibleInSceneExplorer)
				{
					bHasChildren = false;
				}
			}
			bool bSelected = g_EngineInstance->IsObjectSelected(gameObject);

			bool visible = gameObject->IsVisible();
			const std::string objectVisibleLabel(objectID + "-visible");
			if (ImGui::Checkbox(objectVisibleLabel.c_str(), &visible))
			{
				gameObject->SetVisible(visible);
			}
			ImGui::SameLine();

			ImGuiTreeNodeFlags node_flags = 
				ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_OpenOnDoubleClick | 
				(bSelected ? ImGuiTreeNodeFlags_Selected : 0);

			if (!bHasChildren)
			{
				node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			}

			bool node_open = ImGui::TreeNodeEx((void*)gameObject, node_flags, "%s", objectName.c_str());

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(m_MaterialPayloadCStr);

				if (payload && payload->Data)
				{
					MaterialID* draggedMaterialID = (MaterialID*)payload->Data;
					if (draggedMaterialID)
					{
						if (gameObject->GetMeshComponent())
						{
							gameObject->GetMeshComponent()->SetMaterialID(*draggedMaterialID);
						}
					}
				}

				ImGui::EndDragDropTarget();
			}

			DoGameObjectContextMenu(&gameObject);

			bool bParentChildTreeChanged = (gameObject == nullptr);
			if (gameObject)
			{
				// TODO: Remove from renderer class
				if (ImGui::IsMouseReleased(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_None))
				{
					if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_CONTROL))
					{
						g_EngineInstance->ToggleSelectedObject(gameObject);
					}
					else if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_SHIFT))
					{
						const std::vector<GameObject*>& selectedObjects = g_EngineInstance->GetSelectedObjects();
						if (selectedObjects.empty() ||
							(selectedObjects.size() == 1 && selectedObjects[0] == gameObject))
						{
							g_EngineInstance->ToggleSelectedObject(gameObject);
						}
						else
						{
							std::vector<GameObject*> objectsToSelect;

							GameObject* objectA = selectedObjects[selectedObjects.size() - 1];
							GameObject* objectB = gameObject;

							objectA->AddSelfAndChildrenToVec(objectsToSelect);
							objectB->AddSelfAndChildrenToVec(objectsToSelect);

							if (objectA->GetParent() == objectB->GetParent() &&
								objectA != objectB)
							{
								// Ensure A comes before B
								if (objectA->GetSiblingIndex() > objectB->GetSiblingIndex())
								{
									std::swap(objectA, objectB);
								}

								const std::vector<GameObject*>& objectALaterSiblings = objectA->GetLaterSiblings();
								auto objectBIter = Find(objectALaterSiblings, objectB);
								assert(objectBIter != objectALaterSiblings.end());
								for (auto iter = objectALaterSiblings.begin(); iter != objectBIter; ++iter)
								{
									(*iter)->AddSelfAndChildrenToVec(objectsToSelect);
								}
							}

							for (GameObject* objectToSelect : objectsToSelect)
							{
								g_EngineInstance->AddSelectedObject(objectToSelect);
							}
						}
					}
					else
					{
						g_EngineInstance->SetSelectedObject(gameObject);
					}
				}

				if (ImGui::IsItemActive())
				{
					if (ImGui::BeginDragDropSource())
					{
						const void* data = nullptr;
						size_t size = 0;

						const std::vector<GameObject*>& selectedObjects = g_EngineInstance->GetSelectedObjects();
						auto iter = Find(selectedObjects, gameObject);
						bool bItemInSelection = iter != selectedObjects.end();
						std::string dragDropText;

						std::vector<GameObject*> draggedGameObjects;
						if (bItemInSelection)
						{
							for (GameObject* selectedObject : selectedObjects)
							{
								// Don't allow children to not be part of dragged selection
								selectedObject->AddSelfAndChildrenToVec(draggedGameObjects);
							}

							// Ensure any children which weren't selected are now in selection
							for (GameObject* draggedGameObject : draggedGameObjects)
							{
								g_EngineInstance->AddSelectedObject(draggedGameObject);
							}

							data = draggedGameObjects.data();
							size = draggedGameObjects.size() * sizeof(GameObject*);

							if (draggedGameObjects.size() == 1)
							{
								dragDropText = draggedGameObjects[0]->GetName();
							}
							else
							{
								dragDropText = IntToString(draggedGameObjects.size()) + " objects";
							}
						}
						else
						{
							g_EngineInstance->SetSelectedObject(gameObject);

							data = (void*)(&gameObject);
							size = sizeof(GameObject*);
							dragDropText = gameObject->GetName();
						}

						ImGui::SetDragDropPayload(m_GameObjectPayloadCStr, data, size);

						ImGui::Text(dragDropText.c_str());

						ImGui::EndDragDropSource();
					}
				}

				if (ImGui::BeginDragDropTarget())
				{
					const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(m_GameObjectPayloadCStr);

					if (payload && payload->Data)
					{
						i32 draggedObjectCount = payload->DataSize / sizeof(GameObject*);

						std::vector<GameObject*> draggedGameObjectsVec;
						draggedGameObjectsVec.reserve(draggedObjectCount);
						for (i32 i = 0; i < draggedObjectCount; ++i)
						{
							draggedGameObjectsVec.push_back(*((GameObject**)payload->Data + i));
						}

						if (!draggedGameObjectsVec.empty())
						{
							bool bContainsChild = false;

							for (GameObject* draggedGameObject : draggedGameObjectsVec)
							{
								if (draggedGameObject == gameObject)
								{
									bContainsChild = true;
									break;
								}

								if (draggedGameObject->HasChild(gameObject, true))
								{
									bContainsChild = true;
									break;
								}
							}

							// If we're a child of the dragged object then don't allow (causes infinite recursion)
							if (!bContainsChild)
							{
								for (GameObject* draggedGameObject : draggedGameObjectsVec)
								{
									if (draggedGameObject->GetParent())
									{
										if (Find(draggedGameObjectsVec, draggedGameObject->GetParent()) == draggedGameObjectsVec.end())
										{
											draggedGameObject->DetachFromParent();
											gameObject->AddChild(draggedGameObject);
											bParentChildTreeChanged = true;
										}
									}
									else
									{
										g_SceneManager->CurrentScene()->RemoveRootObject(draggedGameObject, false);
										gameObject->AddChild(draggedGameObject);
										bParentChildTreeChanged = true;
									}
								}
							}
						}
					}

					ImGui::EndDragDropTarget();
				}
			}

			if (node_open && bHasChildren)
			{
				if (!bParentChildTreeChanged && gameObject)
				{
					ImGui::Indent();
					// Don't cache results since children can change during this recursive call
					for (GameObject* child : gameObject->GetChildren())
					{
						if (DrawGameObjectNameAndChildren(child))
						{
							// If parent-child tree changed then early out

							ImGui::Unindent();
							ImGui::TreePop();

							return true;
						}
					}
					ImGui::Unindent();
				}

				ImGui::TreePop();
			}

			return bParentChildTreeChanged;
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
				PrintError("Invalid renderID passed to GetRenderObject: %i\n", renderID);
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
