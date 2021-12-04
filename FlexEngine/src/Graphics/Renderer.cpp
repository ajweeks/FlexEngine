#include "stdafx.hpp"

#include "Graphics/Renderer.hpp"

IGNORE_WARNINGS_PUSH
#if COMPILE_IMGUI
#include "imgui.h"
#endif

#include <glm/gtx/transform.hpp> // for scale
#include <glm/gtx/quaternion.hpp> // for rotate
IGNORE_WARNINGS_POP

#include <typeinfo>

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Editor.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/ShaderCompiler.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "StringBuilder.hpp"
#include "UIMesh.hpp"
#include "Window/Monitor.hpp"
#include "Window/Window.hpp"

namespace flex
{
	std::array<glm::mat4, 6> Renderer::s_CaptureViews;

	Renderer::Renderer() :
		m_Settings("Renderer settings", RENDERER_SETTINGS_LOCATION, CURRENT_RENDERER_SETTINGS_FILE_VERSION)
	{
		m_Settings.RegisterProperty(1, "enable v-sync", &m_bVSyncEnabled);
		m_Settings.RegisterProperty(1, "enable fxaa", &m_PostProcessSettings.bEnableFXAA);
		m_Settings.RegisterProperty(1, "brightness", &m_PostProcessSettings.brightness, 3);
		m_Settings.RegisterProperty(1, "offset", &m_PostProcessSettings.offset, 3);
		m_Settings.RegisterProperty(1, "saturation", &m_PostProcessSettings.saturation);

		m_Settings.RegisterProperty(2, "shadow cascade count", &m_ShadowCascadeCount);
		m_Settings.RegisterProperty(2, "shadow cascade base resolution", &m_ShadowMapBaseResolution);

		m_Settings.SetOnDeserialize([this]()
		{
			OnSettingsReloaded();
		});
	}

	Renderer::~Renderer()
	{
	}

	void Renderer::Initialize()
	{
		PROFILE_AUTO("Renderer Initialize");

#if COMPILE_SHADER_COMPILER
		{
			PROFILE_AUTO("Start shader compilation");
			m_ShaderCompiler = new ShaderCompiler();
			m_ShaderCompiler->StartCompilation();
		}
#endif

		m_LightData = (u8*)malloc(MAX_POINT_LIGHT_COUNT * sizeof(PointLightData) + MAX_SPOT_LIGHT_COUNT * sizeof(SpotLightData) + MAX_AREA_LIGHT_COUNT * sizeof(AreaLightData));
		CHECK_NE(m_LightData, nullptr);
		m_PointLightData = (PointLightData*)m_LightData;
		m_SpotLightData = (SpotLightData*)(m_PointLightData + MAX_POINT_LIGHT_COUNT);
		m_AreaLightData = (AreaLightData*)(m_SpotLightData + MAX_SPOT_LIGHT_COUNT);
		for (i32 i = 0; i < MAX_POINT_LIGHT_COUNT; ++i)
		{
			memset(&m_PointLightData[i], 0, sizeof(PointLightData));
			m_PointLightData[i].colour = VEC3_NEG_ONE;
		}
		for (i32 i = 0; i < MAX_SPOT_LIGHT_COUNT; ++i)
		{
			memset(&m_SpotLightData[i], 0, sizeof(SpotLightData));
			m_SpotLightData[i].colour = VEC3_NEG_ONE;
		}
		for (i32 i = 0; i < MAX_AREA_LIGHT_COUNT; ++i)
		{
			memset(&m_AreaLightData[i], 0, sizeof(AreaLightData));
			m_AreaLightData[i].colour = VEC3_NEG_ONE;
		}

		// TODO: Move these defaults to config file

		for (u32 i = 0; i < MAX_SSAO_KERNEL_SIZE; ++i)
		{
			glm::vec3 sample(RandomFloat(-0.9f, 0.9f), RandomFloat(-0.9f, 0.9f), RandomFloat(0.0f, 1.0f));
			sample = glm::normalize(sample); // Snap to surface of hemisphere
			sample *= RandomFloat(0.0f, 1.0f); // Space out linearly
			real scale = (real)i / (real)MAX_SSAO_KERNEL_SIZE;
			scale = Lerp(0.1f, 1.0f, scale * scale); // Bring distribution of samples closer to origin
			m_SSAOGenData.samples[i] = glm::vec4(sample * scale, 0.0f);
		}
		m_SSAOGenData.radius = 8.0f;

		m_SSAOBlurDataConstant.radius = 4;
		m_SSAOBlurSamplePixelOffset = 2;

		m_SSAOSamplingData.enabled = 1;
		m_SSAOSamplingData.powExp = 2.0f;

		m_ShadowSamplingData.cascadeDepthSplits = glm::vec4(0.1f, 0.25f, 0.5f, 0.8f);
		m_ShadowSamplingData.baseBias = 0.002f;

		m_UIMesh = new UIMesh();
	}

	void Renderer::PostInitialize()
	{
		PROFILE_AUTO("Renderer PostInitialize");

		// TODO: Use MeshComponent for these objects?
		m_UIMesh->Initialize();

		if (g_EngineInstance->InstallShaderDirectoryWatch())
		{
			m_ShaderDirectoryWatcher = new DirectoryWatcher(SHADER_SOURCE_DIRECTORY, false);
			if (!m_ShaderDirectoryWatcher->Installed())
			{
				PrintWarn("Failed to install shader directory watcher\n");
				delete m_ShaderDirectoryWatcher;
				m_ShaderDirectoryWatcher = nullptr;
			}
		}

		// Full screen Triangle
		{
			VertexBufferDataCreateInfo triVertexBufferDataCreateInfo = {};
			triVertexBufferDataCreateInfo.positions_2D = {
				glm::vec2(-1.0f, -1.0f),
				glm::vec2(-1.0f,  3.0f),
				glm::vec2(3.0f, -1.0f)
			};

			triVertexBufferDataCreateInfo.texCoords_UV = {
				glm::vec2(0.0f,  1.0f),
				glm::vec2(0.0f, -1.0f),
				glm::vec2(2.0f,  1.0f)
			};

			triVertexBufferDataCreateInfo.attributes =
				(u32)VertexAttribute::POSITION2 |
				(u32)VertexAttribute::UV;

			m_FullScreenTriVertexBufferData = {};
			m_FullScreenTriVertexBufferData.Initialize(triVertexBufferDataCreateInfo);


			GameObject* fullScreenTriGameObject = new GameObject("Full screen triangle", SID("object"));
			m_PersistentObjects.push_back(fullScreenTriGameObject);
			fullScreenTriGameObject->SetVisible(false);
			fullScreenTriGameObject->SetCastsShadow(false);

			RenderObjectCreateInfo fullScreenTriCreateInfo = {};
			fullScreenTriCreateInfo.vertexBufferData = &m_FullScreenTriVertexBufferData;
			fullScreenTriCreateInfo.materialID = m_PostProcessMatID;
			fullScreenTriCreateInfo.gameObject = fullScreenTriGameObject;
			fullScreenTriCreateInfo.cullFace = CullFace::NONE;
			fullScreenTriCreateInfo.visibleInSceneExplorer = false;
			fullScreenTriCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
			m_FullScreenTriRenderID = InitializeRenderObject(&fullScreenTriCreateInfo);
		}

		// 3D Quad
		{
			VertexBufferDataCreateInfo quad3DVertexBufferDataCreateInfo = {};
			quad3DVertexBufferDataCreateInfo.positions_3D = {
				glm::vec3(-1.0f, -1.0f, 0.0f),
				glm::vec3(-1.0f,  1.0f, 0.0f),
				glm::vec3(1.0f, -1.0f, 0.0f),

				glm::vec3(1.0f, -1.0f, 0.0f),
				glm::vec3(-1.0f,  1.0f, 0.0f),
				glm::vec3(1.0f,  1.0f, 0.0f),
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
			m_Quad3DVertexBufferData.Initialize(quad3DVertexBufferDataCreateInfo);


			GameObject* quad3DGameObject = new GameObject("Sprite Quad 3D", SID("object"));
			m_PersistentObjects.push_back(quad3DGameObject);
			quad3DGameObject->SetVisible(false);
			quad3DGameObject->SetCastsShadow(false);

			RenderObjectCreateInfo quad3DCreateInfo = {};
			quad3DCreateInfo.vertexBufferData = &m_Quad3DVertexBufferData;
			quad3DCreateInfo.materialID = m_SpriteMatWSID;
			quad3DCreateInfo.gameObject = quad3DGameObject;
			quad3DCreateInfo.cullFace = CullFace::NONE;
			quad3DCreateInfo.visibleInSceneExplorer = false;
			quad3DCreateInfo.depthTestReadFunc = DepthTestFunc::GEQUAL;
			quad3DCreateInfo.bEditorObject = true; // TODO: Create other quad which is identical but is not an editor object for gameplay objects?
			quad3DCreateInfo.renderPassOverride = RenderPassType::FORWARD;
			m_Quad3DRenderID = InitializeRenderObject(&quad3DCreateInfo);

			quad3DCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
			quad3DCreateInfo.materialID = m_SpriteMatSSID;
			quad3DCreateInfo.renderPassOverride = RenderPassType::UI;
			m_Quad3DSSRenderID = InitializeRenderObject(&quad3DCreateInfo);

			// Hologram
			m_HologramMatID = g_Renderer->GetMaterialID("Selection Hologram");
			m_HologramProxyObject = new GameObject("Proxy hologram object", InvalidStringID);
			// Initialize with empty mesh
			m_HologramProxyObject->SetMesh(new Mesh(m_HologramProxyObject));
		}
	}

	void Renderer::Destroy()
	{
		for (GameObject* obj : m_PersistentObjects)
		{
			obj->Destroy();
			delete obj;
		}
		m_PersistentObjects.clear();

#if COMPILE_SHADER_COMPILER
		if (m_ShaderCompiler != nullptr)
		{
			m_ShaderCompiler->JoinAllThreads();
			delete m_ShaderCompiler;
			m_ShaderCompiler = nullptr;
		}
#endif

		free(m_LightData);
		m_LightData = nullptr;
		m_PointLightData = nullptr;
		m_SpotLightData = nullptr;

		m_HologramProxyObject->Destroy();
		delete m_HologramProxyObject;
		m_HologramProxyObject = nullptr;

		delete m_ShaderDirectoryWatcher;

		m_Quad3DVertexBufferData.Destroy();
		m_FullScreenTriVertexBufferData.Destroy();

		m_UIMesh->Destroy();
		delete m_UIMesh;
		m_UIMesh = nullptr;

		DestroyRenderObject(m_FullScreenTriRenderID);
		DestroyRenderObject(m_Quad3DRenderID);
		DestroyRenderObject(m_Quad3DSSRenderID);
		DestroyRenderObject(m_GBufferQuadRenderID);

		if (m_PhysicsDebugDrawer != nullptr)
		{
			m_PhysicsDebugDrawer->Destroy();
			delete m_PhysicsDebugDrawer;
			m_PhysicsDebugDrawer = nullptr;
		}
	}

	void Renderer::NewFrame()
	{
		m_UIMesh->EndFrame();

		m_HologramProxyObject->SetVisible(false);
	}

	void Renderer::SetReflectionProbeMaterial(MaterialID reflectionProbeMaterialID)
	{
		m_ReflectionProbeMaterialID = reflectionProbeMaterialID;
	}

	void Renderer::ToggleRenderGrid()
	{
		SetRenderGrid(!m_bRenderGrid);
	}

	void Renderer::SetRenderGrid(bool bRenderGrid)
	{
		m_bRenderGrid = bRenderGrid;

		if (m_Grid != nullptr)
		{
			m_Grid->SetVisible(bRenderGrid);
		}
		if (m_WorldOrigin != nullptr)
		{
			m_WorldOrigin->SetVisible(bRenderGrid);
		}
	}

	bool Renderer::IsRenderingGrid() const
	{
		return m_bRenderGrid;
	}

	void Renderer::SaveSettingsToDisk(bool bAddEditorStr /* = true */)
	{
		if (m_Settings.Serialize())
		{
			if (bAddEditorStr)
			{
				AddEditorString("Saved renderer settings");
			}
		}
	}

	void Renderer::LoadSettingsFromDisk()
	{
		PROFILE_AUTO("Renderer LoadSettingsFromDisk");

		if (m_Settings.Deserialize())
		{
			OnSettingsReloaded();
		}
	}

	void Renderer::SetShaderQualityLevel(i32 newQualityLevel)
	{
		newQualityLevel = glm::clamp(newQualityLevel, 0, MAX_SHADER_QUALITY_LEVEL);
		SpecializationConstantMetaData& specializationConstantMetaData = m_SpecializationConstants[SID("shader_quality_level")];
		if (newQualityLevel != specializationConstantMetaData.value)
		{
			specializationConstantMetaData.value = newQualityLevel;
			RecreateEverything();
		}
	}

	MaterialID Renderer::GetMaterialID(const std::string& materialName)
	{
		for (auto& matPair : m_Materials)
		{
			Material* material = matPair.second;
			if (material->name.compare(materialName) == 0)
			{
				return matPair.first;
			}
		}

		return InvalidMaterialID;
	}

	void Renderer::TransformRectToScreenSpace(const glm::vec2& pos,
		const glm::vec2& scale,
		glm::vec2& posOut,
		glm::vec2& scaleOut)
	{
		const glm::vec2 frameBufferSize = (glm::vec2)g_Window->GetFrameBufferSize();
		const real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;

		/*
		Sprite space to pixel space:
		- Divide x by aspect ratio
		- + 1
		- / 2
		- y = 1 - y
		- * frameBufferSize
		*/

		posOut = pos;
		posOut.x /= aspectRatio;
		posOut += glm::vec2(1.0f);
		posOut /= 2.0f;
		posOut.y = 1.0f - posOut.y;
		posOut *= frameBufferSize;

		scaleOut = glm::vec2(scale * frameBufferSize);
		scaleOut.x /= aspectRatio;
	}

	void Renderer::NormalizeSpritePos(const glm::vec2& pos,
		AnchorPoint anchor,
		const glm::vec2& scale,
		glm::vec2& posOut,
		glm::vec2& scaleOut)
	{
		const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		const real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;

		posOut = pos;
		posOut.x /= aspectRatio;
		scaleOut = scale;

		glm::vec2 absScale = glm::abs(scale);
		absScale.x /= aspectRatio;

		if (anchor == AnchorPoint::WHOLE)
		{
			//scaleOut.x *= aspectRatio;
		}

		switch (anchor)
		{
		case AnchorPoint::CENTER:
			// Already centered (zero)
			break;
		case AnchorPoint::TOP_LEFT:
			posOut += glm::vec2(-1.0f + (absScale.x), (1.0f - absScale.y));
			break;
		case AnchorPoint::TOP:
			posOut += glm::vec2(0.0f, (1.0f - absScale.y));
			break;
		case AnchorPoint::TOP_RIGHT:
			posOut += glm::vec2(1.0f - absScale.x, (1.0f - absScale.y));
			break;
		case AnchorPoint::RIGHT:
			posOut += glm::vec2(1.0f - absScale.x, 0.0f);
			break;
		case AnchorPoint::BOTTOM_RIGHT:
			posOut += glm::vec2(1.0f - absScale.x, (-1.0f + absScale.y));
			break;
		case AnchorPoint::BOTTOM:
			posOut += glm::vec2(0.0f, (-1.0f + absScale.y));
			break;
		case AnchorPoint::BOTTOM_LEFT:
			posOut += glm::vec2(-1.0f + absScale.x, (-1.0f + absScale.y));
			break;
		case AnchorPoint::LEFT:
			posOut += glm::vec2(-1.0f + absScale.x, 0.0f);
			break;
		case AnchorPoint::WHOLE:
			// Already centered (zero)
			break;
		default:
			break;
		}

		posOut.x *= aspectRatio;
	}

	void Renderer::EnqueueUntexturedQuad(const glm::vec2& pos,
		AnchorPoint anchor,
		const glm::vec2& size,
		const glm::vec4& colour)
	{
		SpriteQuadDrawInfo drawInfo = {};

		drawInfo.materialID = m_SpriteMatSSID;
		drawInfo.scale = glm::vec3(size.x, size.y, 1.0f);
		drawInfo.bScreenSpace = true;
		drawInfo.bReadDepth = false;
		drawInfo.anchor = anchor;
		drawInfo.colour = colour;
		drawInfo.pos = glm::vec3(pos.x, pos.y, 1.0f);
		drawInfo.bEnableAlbedoSampler = false;

		EnqueueSprite(drawInfo);
	}

	void Renderer::EnqueueUntexturedQuadRaw(const glm::vec2& pos,
		const glm::vec2& size,
		const glm::vec4& colour)
	{
		SpriteQuadDrawInfo drawInfo = {};

		drawInfo.materialID = m_SpriteMatSSID;
		drawInfo.scale = glm::vec3(size.x, size.y, 1.0f);
		drawInfo.bScreenSpace = true;
		drawInfo.bReadDepth = false;
		drawInfo.bRaw = true;
		drawInfo.colour = colour;
		drawInfo.pos = glm::vec3(pos.x, pos.y, 1.0f);
		drawInfo.bEnableAlbedoSampler = false;

		EnqueueSprite(drawInfo);
	}

	void Renderer::EnqueueSprite(SpriteQuadDrawInfo drawInfo)
	{
		if (drawInfo.textureID == InvalidTextureID)
		{
			drawInfo.textureID = blankTextureID;
		}

		if (drawInfo.bScreenSpace)
		{
			if (drawInfo.materialID != InvalidMaterialID && GetShader(GetMaterial(drawInfo.materialID)->shaderID)->bTextureArr)
			{
				m_QueuedSSArrSprites.push_back(drawInfo);
			}
			else
			{
				if (drawInfo.zOrder > Z_ORDER_UI)
				{
					m_QueuedSSPostUISprites.push_back(drawInfo);
				}
				else
				{
					m_QueuedSSPreUISprites.push_back(drawInfo);
				}
			}
		}
		else
		{
			m_QueuedWSSprites.push_back(drawInfo);
		}
	}

	void Renderer::SetDisplayBoundingVolumesEnabled(bool bEnabled)
	{
		m_bDisplayBoundingVolumes = bEnabled;
	}

	bool Renderer::IsDisplayBoundingVolumesEnabled() const
	{
		return m_bDisplayBoundingVolumes;
	}

	PhysicsDebuggingSettings& Renderer::GetPhysicsDebuggingSettings()
	{
		return m_PhysicsDebuggingSettings;
	}

	bool Renderer::RegisterDirectionalLight(DirectionalLight* dirLight)
	{
		m_DirectionalLight = dirLight;
		return true;
	}

	void Renderer::RemoveDirectionalLight()
	{
		m_DirectionalLight = nullptr;
	}

	PointLightID Renderer::RegisterPointLight(PointLightData* pointLightData)
	{
		if (m_NumPointLightsEnabled < MAX_POINT_LIGHT_COUNT)
		{
			PointLightID newPointLightID = InvalidPointLightID;

			for (i32 i = 0; i < MAX_POINT_LIGHT_COUNT; ++i)
			{
				PointLightData* data = m_PointLightData + i;
				if (data->colour.x == -1.0f)
				{
					newPointLightID = (PointLightID)i;
					break;
				}
			}

			if (newPointLightID == InvalidPointLightID)
			{
				return InvalidPointLightID;
			}

			UpdatePointLightData(newPointLightID, pointLightData);

			m_NumPointLightsEnabled++;
			return newPointLightID;
		}
		return InvalidPointLightID;
	}

	void Renderer::RemovePointLight(PointLightID ID)
	{
		if (ID != InvalidPointLightID)
		{
			if (m_PointLightData[ID].colour.x != -1.0f)
			{
				m_PointLightData[ID].colour = VEC4_NEG_ONE;
				m_PointLightData[ID].enabled = 0;
				m_NumPointLightsEnabled--;
				CHECK_GE(m_NumPointLightsEnabled, 0);
				UpdatePointLightData(ID, nullptr);
			}
		}
	}

	void Renderer::RemoveAllPointLights()
	{
		for (i32 i = 0; i < MAX_POINT_LIGHT_COUNT; ++i)
		{
			m_PointLightData[i].colour = VEC4_NEG_ONE;
			m_PointLightData[i].enabled = 0;
			UpdatePointLightData(i, nullptr);
		}
		m_NumPointLightsEnabled = 0;
	}

	void Renderer::UpdatePointLightData(PointLightID ID, PointLightData* data)
	{
		if (ID < MAX_POINT_LIGHT_COUNT)
		{
			if (data != nullptr)
			{
				memcpy(m_PointLightData + ID, data, sizeof(PointLightData));
			}
			else
			{
				memset(&m_PointLightData[ID], 0, sizeof(PointLightData));
				m_PointLightData[ID].colour = VEC3_NEG_ONE;
			}
		}
	}

	i32 Renderer::GetNumPointLights()
	{
		return m_NumPointLightsEnabled;
	}

	SpotLightID Renderer::RegisterSpotLight(SpotLightData* spotLightData)
	{
		if (m_NumSpotLightsEnabled < MAX_SPOT_LIGHT_COUNT)
		{
			SpotLightID newSpotLightID = InvalidSpotLightID;

			for (i32 i = 0; i < MAX_SPOT_LIGHT_COUNT; ++i)
			{
				SpotLightData* data = m_SpotLightData + i;
				if (data->colour.x == -1.0f)
				{
					newSpotLightID = (SpotLightID)i;
					break;
				}
			}

			if (newSpotLightID == InvalidSpotLightID)
			{
				return InvalidSpotLightID;
			}

			UpdateSpotLightData(newSpotLightID, spotLightData);

			m_NumSpotLightsEnabled++;
			return newSpotLightID;
		}
		return InvalidSpotLightID;
	}

	void Renderer::RemoveSpotLight(SpotLightID ID)
	{
		if (ID != InvalidSpotLightID)
		{
			if (m_SpotLightData[ID].colour.x != -1.0f)
			{
				m_SpotLightData[ID].colour = VEC4_NEG_ONE;
				m_SpotLightData[ID].enabled = 0;
				m_NumSpotLightsEnabled--;
				CHECK_GE(m_NumSpotLightsEnabled, 0);
				UpdateSpotLightData(ID, nullptr);
			}
		}
	}

	void Renderer::RemoveAllSpotLights()
	{
		for (i32 i = 0; i < MAX_SPOT_LIGHT_COUNT; ++i)
		{
			UpdateSpotLightData(i, nullptr);
		}
		m_NumSpotLightsEnabled = 0;
	}

	void Renderer::UpdateSpotLightData(SpotLightID ID, SpotLightData* data)
	{
		if (ID < MAX_SPOT_LIGHT_COUNT)
		{
			if (data != nullptr)
			{
				memcpy(m_SpotLightData + ID, data, sizeof(SpotLightData));
			}
			else
			{
				memset(&m_SpotLightData[ID], 0, sizeof(SpotLightData));
				m_SpotLightData[ID].colour = VEC3_NEG_ONE;
			}
		}
	}

	i32 Renderer::GetNumSpotLights()
	{
		return m_NumSpotLightsEnabled;
	}

	AreaLightID Renderer::RegisterAreaLight(AreaLightData* areaLightData)
	{
		if (m_NumAreaLightsEnabled < MAX_AREA_LIGHT_COUNT)
		{
			AreaLightID newAreaLightID = InvalidAreaLightID;

			for (i32 i = 0; i < MAX_AREA_LIGHT_COUNT; ++i)
			{
				AreaLightData* data = m_AreaLightData + i;
				if (data->colour.x == -1.0f)
				{
					newAreaLightID = (AreaLightID)i;
					break;
				}
			}

			if (newAreaLightID == InvalidAreaLightID)
			{
				return InvalidAreaLightID;
			}

			UpdateAreaLightData(newAreaLightID, areaLightData);

			m_NumAreaLightsEnabled++;
			return newAreaLightID;
		}
		return InvalidAreaLightID;
	}

	void Renderer::RemoveAreaLight(AreaLightID ID)
	{
		if (ID != InvalidAreaLightID)
		{
			if (m_AreaLightData[ID].colour.x != -1.0f)
			{
				m_AreaLightData[ID].colour = VEC4_NEG_ONE;
				m_AreaLightData[ID].enabled = 0;
				m_NumAreaLightsEnabled--;
				CHECK_GE(m_NumAreaLightsEnabled, 0);
				UpdateAreaLightData(ID, nullptr);
			}
		}
	}

	void Renderer::RemoveAllAreaLights()
	{
		for (i32 i = 0; i < MAX_AREA_LIGHT_COUNT; ++i)
		{
			UpdateAreaLightData(i, nullptr);
		}
		m_NumAreaLightsEnabled = 0;
	}

	void Renderer::UpdateAreaLightData(AreaLightID ID, AreaLightData* data)
	{
		if (ID < MAX_AREA_LIGHT_COUNT)
		{
			if (data != nullptr)
			{
				memcpy(m_AreaLightData + ID, data, sizeof(AreaLightData));
			}
			else
			{
				memset(&m_AreaLightData[ID], 0, sizeof(AreaLightData));
				m_AreaLightData[ID].colour = VEC3_NEG_ONE;
			}
		}
	}

	DirectionalLight* Renderer::GetDirectionalLight()
	{
		if (m_DirectionalLight)
		{
			return m_DirectionalLight;
		}
		return nullptr;
	}

	i32 Renderer::GetNumAreaLights()
	{
		return m_NumAreaLightsEnabled;
	}

	i32 Renderer::GetFramesRenderedCount() const
	{
		return m_FramesRendered;
	}

	BitmapFont* Renderer::SetFont(StringID fontID)
	{
		m_CurrentFont = g_ResourceManager->fontMetaData[fontID].bitmapFont;
		return m_CurrentFont;
	}

	void Renderer::AddEditorString(const std::string& str)
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

	void Renderer::AddNotificationMessage(const std::string& message)
	{
		m_NotificationMessages.emplace_back(message);
	}

	Material* Renderer::GetMaterial(MaterialID materialID)
	{
		return m_Materials.at(materialID);
	}

	void Renderer::RemoveMaterial(MaterialID materialID)
	{
		auto iter = m_Materials.find(materialID);
		if (iter != m_Materials.end())
		{
			delete iter->second;
			m_Materials.erase(iter);
		}
	}

	Shader* Renderer::GetShader(ShaderID shaderID)
	{
		if (shaderID != InvalidShaderID)
		{
			return m_Shaders[shaderID];
		}
		return nullptr;
	}

	i32 Renderer::GetMaterialCount()
	{
		return i32(m_Materials.size());
	}

	MaterialID Renderer::GetNextAvailableMaterialID() const
	{
		// Return lowest available ID
		MaterialID result = 0;
		while (m_Materials.find(result) != m_Materials.end())
		{
			++result;
		}
		return result;
	}

	bool Renderer::MaterialExists(MaterialID materialID)
	{
		return m_Materials.find(materialID) != m_Materials.end();
	}

	bool Renderer::MaterialWithNameExists(const std::string& matName)
	{
		// Search through loaded materials
		for (const auto& matPair : m_Materials)
		{
			if (matPair.second->name.compare(matName) == 0)
			{
				return true;
			}
		}

		// Search through unloaded materials
		for (const MaterialCreateInfo& matCreateInfo : g_ResourceManager->parsedMaterialInfos)
		{
			if (matCreateInfo.name.compare(matName) == 0)
			{
				return true;
			}
		}

		return false;
	}

	bool Renderer::FindOrCreateMaterialByName(const std::string& materialName, MaterialID& outMaterialID)
	{
		const char* matNameCStr = materialName.c_str();
		for (u32 i = 0; i < (u32)m_Materials.size(); ++i)
		{
			auto matIter = m_Materials.find(i);
			if (matIter != m_Materials.end() && matIter->second->name.compare(matNameCStr) == 0)
			{
				outMaterialID = i;
				return true;
			}
		}

		MaterialCreateInfo* matCreateInfo = g_ResourceManager->GetMaterialInfo(matNameCStr);
		if (matCreateInfo != nullptr)
		{
			outMaterialID = InitializeMaterial(matCreateInfo);
			return true;
		}

		return false;
	}

	bool Renderer::GetShaderID(const std::string& shaderName, ShaderID& shaderID)
	{
		// TODO: Store shaders using sorted data structure?
		for (u32 i = 0; i < (u32)m_Shaders.size(); ++i)
		{
			if (m_Shaders[i]->name.compare(shaderName) == 0)
			{
				shaderID = i;
				return true;
			}
		}

		return false;
	}

	Renderer::PostProcessSettings& Renderer::GetPostProcessSettings()
	{
		return m_PostProcessSettings;
	}

	MaterialID Renderer::GetPlaceholderMaterialID() const
	{
		return m_PlaceholderMaterialID;
	}

	void Renderer::SetDisplayShadowCascadePreview(bool bPreview)
	{
		m_bDisplayShadowCascadePreview = bPreview;
	}

	bool Renderer::GetDisplayShadowCascadePreview() const
	{
		return m_bDisplayShadowCascadePreview;
	}

	void Renderer::SetTAAEnabled(bool bEnabled)
	{
		if (m_bEnableTAA != bEnabled)
		{
			m_bEnableTAA = bEnabled;
			m_bTAAStateChanged = true;
		}
	}

	bool Renderer::IsTAAEnabled() const
	{
		return m_bEnableTAA;
	}

	i32 Renderer::GetTAASampleCount() const
	{
		return m_TAASampleCount;
	}

	void Renderer::SetDirtyFlags(RenderBatchDirtyFlags flags)
	{
		m_DirtyFlagBits |= flags;
		m_bRebatchRenderObjects = true;
	}

	bool Renderer::SerializeLoadedMaterials()
	{
		bool bAllSucceeded = true;

		for (auto& matPair : m_Materials)
		{
			Material* material = matPair.second;
			if (material->bSerializable)
			{
				JSONObject materialObj = material->Serialize();
				std::string fileContents = materialObj.ToString();

				std::string hypenatedName = Replace(material->name, ' ', '-');
				const std::string fileName = MATERIALS_DIRECTORY + hypenatedName + ".json";
				if (!WriteFile(fileName, fileContents, false))
				{
					PrintWarn("Failed to serialize material %s to file %s\n", material->name.c_str(), fileName.c_str());
					bAllSucceeded = false;
				}
			}
		}

		return bAllSucceeded;
	}

	void Renderer::SetDynamicGeometryBufferDirty(u32 dynamicVertexBufferIndex)
	{
		if (!Contains(m_DirtyDynamicVertexAndIndexBufferIndices, dynamicVertexBufferIndex))
		{
			m_DirtyDynamicVertexAndIndexBufferIndices.push_back(dynamicVertexBufferIndex);
		}
	}

	void Renderer::SetStaticGeometryBufferDirty(u32 staticVertexBufferIndex)
	{
		if (!Contains(m_DirtyStaticVertexBufferIndices, staticVertexBufferIndex))
		{
			m_DirtyStaticVertexBufferIndices.push_back(staticVertexBufferIndex);
		}
	}

	void Renderer::ToggleWireframeOverlay()
	{
		m_bEnableWireframeOverlay = !m_bEnableWireframeOverlay;
	}

	void Renderer::ToggleWireframeSelectionOverlay()
	{
		m_bEnableSelectionWireframe = !m_bEnableSelectionWireframe;
	}

	UIMesh* Renderer::GetUIMesh()
	{
		return m_UIMesh;
	}

	void Renderer::SetDebugOverlayID(i32 newID)
	{
		newID = glm::clamp(newID, 0, (i32)g_ResourceManager->debugOverlayNames.size());
		SpecializationConstantMetaData& debugOverlayConstant = m_SpecializationConstants[SID("debug_overlay_index")];
		if (newID != debugOverlayConstant.value)
		{
			debugOverlayConstant.value = newID;
			RecreateEverything();
		}
	}

	void Renderer::ToggleFogEnabled()
	{
		m_SpecializationConstants[SID("enable_fog")].value = 1 - m_SpecializationConstants[SID("enable_fog")].value;
		RecreateEverything();
	}

	void Renderer::EnqueueScreenSpaceSprites()
	{
		if (m_bDisplayShadowCascadePreview)
		{
			SpriteQuadDrawInfo drawInfo = {};
			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = true;
			drawInfo.materialID = m_SpriteArrMatID;
			drawInfo.anchor = AnchorPoint::BOTTOM_RIGHT;
			drawInfo.scale = glm::vec3(0.2f);
			for (u32 i = 0; i < (u32)m_ShadowCascadeCount; ++i)
			{
				// TODO:
				drawInfo.textureID = 999 + i;
				drawInfo.textureLayer = i;
				drawInfo.pos = glm::vec3(0.0f, i * drawInfo.scale.x * 2.1f, 0.0f);
				EnqueueSprite(drawInfo);
			}
		}
	}

	void Renderer::EnqueueWorldSpaceSprites()
	{
	}

	void Renderer::Update()
	{
		PROFILE_AUTO("Renderer Update");

#if COMPILE_SHADER_COMPILER
		if (!m_ShaderCompiler->bComplete)
		{
			i32 shadersRemaining = m_ShaderCompiler->WorkItemsRemaining();
			i32 threadCount = m_ShaderCompiler->ThreadCount();

			static std::string dots = "...";
			static std::string spaces = "   ";
			StringBuilder stringBuilder;
			stringBuilder.Append("Compiling ");
			stringBuilder.Append(IntToString(shadersRemaining));
			stringBuilder.Append(shadersRemaining == 1 ? " shader" : " shaders");
			i32 numDots = glm::clamp((i32)(fmod(g_SecElapsedSinceProgramStart, 1.0f) * 3), 0, 3);
			stringBuilder.Append(dots.substr(0, numDots));
			stringBuilder.Append(spaces.substr(0, 3 - numDots));
			AddNotificationMessage(stringBuilder.ToString());

			stringBuilder.Clear();
			stringBuilder.Append("(using ");
			stringBuilder.Append(IntToString(threadCount));
			stringBuilder.Append(threadCount == 1 ? " thread)" : " threads)");
			AddNotificationMessage(stringBuilder.ToString());
		}

		if (m_ShaderCompiler->TickStatus())
		{
			m_bShaderErrorWindowShowing = true;

			if (m_ShaderCompiler->bSuccess)
			{
				AddEditorString("Shader recompile completed successfully");
				RecreateEverything();

				if (m_ShaderCompiler->WasShaderRecompiled("terrain_generate_points") ||
					m_ShaderCompiler->WasShaderRecompiled("terrain_generate_mesh"))
				{
					g_SceneManager->CurrentScene()->RegenerateTerrain();
				}
			}
			else
			{
				PrintError("Shader recompile failed\n");
				AddEditorString("Shader recompile failed");
			}

			m_bSwapChainNeedsRebuilding = true; // This is needed to recreate some resources for SSAO, etc.
		}
#endif

		if (m_EditorStrSecRemaining > 0.0f)
		{
			m_EditorStrSecRemaining -= g_DeltaTime;
			if (m_EditorStrSecRemaining <= 0.0f)
			{
				m_EditorStrSecRemaining = 0.0f;
			}
		}

		if (m_ShaderDirectoryWatcher != nullptr && m_ShaderDirectoryWatcher->Update())
		{
			RecompileShaders(false);
		}

		glm::vec4 depthSplits(0.04f, 0.15f, 0.4f, 1.0f);

		BaseCamera* cam = g_CameraManager->CurrentCamera();
		DirectionalLight* dirLight = g_Renderer->GetDirectionalLight();
		if (dirLight != nullptr)
		{
			// Flip near & far planes
			glm::mat4 modifiedProj = cam->GetProjection();
			modifiedProj[2][2] = 1.0f - modifiedProj[2][2];
			modifiedProj[3][2] = -modifiedProj[3][2];
			glm::mat4 invCam = glm::inverse(modifiedProj * cam->GetView());

			if ((i32)m_ShadowLightViewMats.size() != m_ShadowCascadeCount)
			{
				m_ShadowLightViewMats.resize(m_ShadowCascadeCount);
				m_ShadowLightProjMats.resize(m_ShadowCascadeCount);
			}

			real lastSplitDist = 0.0f;
			for (u32 c = 0; c < (u32)m_ShadowCascadeCount; ++c)
			{
				real splitDist = depthSplits[c];

				glm::vec3 frustumCorners[8] = {
					{ -1.0f,  1.0f, -1.0f },
					{  1.0f,  1.0f, -1.0f },
					{  1.0f, -1.0f, -1.0f },
					{ -1.0f, -1.0f, -1.0f },
					{ -1.0f,  1.0f,  1.0f },
					{  1.0f,  1.0f,  1.0f },
					{  1.0f, -1.0f,  1.0f },
					{ -1.0f, -1.0f,  1.0f },
				};

				// Transform frustum corners from clip space to world space
				for (glm::vec3& frustumCorner : frustumCorners)
				{
					glm::vec4 invCorner = invCam * glm::vec4(frustumCorner, 1.0f);
					frustumCorner = invCorner / invCorner.w;
				}

				for (u32 i = 0; i < 4; ++i)
				{
					glm::vec3 dist = frustumCorners[i + 4] - frustumCorners[i];
					frustumCorners[i + 4] = frustumCorners[i] + (dist * splitDist);
					frustumCorners[i] = frustumCorners[i] + (dist * lastSplitDist);
				}

				glm::vec3 frustumCenter(0.0f);
				for (const glm::vec3& frustumCorner : frustumCorners)
				{
					frustumCenter += frustumCorner;
				}
				frustumCenter /= 8.0f;

				real radius = 0.0f;
				for (const glm::vec3& frustumCorner : frustumCorners)
				{
					real distance = glm::length(frustumCorner - frustumCenter);
					radius = glm::max(radius, distance);
				}
				radius = std::ceil(radius * 16.0f) / 16.0f;

				glm::vec3 maxExtents = glm::vec3(radius);
				glm::vec3 minExtents = -maxExtents;
				real zFar = g_CameraManager->CurrentCamera()->zFar;

				m_ShadowLightViewMats[c] = glm::lookAt(frustumCenter - m_DirectionalLight->data.dir * minExtents.z, frustumCenter, VEC3_UP);
				m_ShadowLightProjMats[c] = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, zFar * 2.0f, -zFar);

				m_ShadowSamplingData.cascadeViewProjMats[c] = m_ShadowLightProjMats[c] * m_ShadowLightViewMats[c];
				m_ShadowSamplingData.cascadeDepthSplits[c] = depthSplits[c];

				lastSplitDist = depthSplits[c];
			}
		}

		m_UIMesh->Draw();

		if (m_QueuedHologramPrefabID.IsValid())
		{
			Mesh* mesh = m_HologramProxyObject->GetMesh();
			GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(m_QueuedHologramPrefabID);
			const std::string meshFilePath = prefabTemplate->GetMesh()->GetRelativeFilePath();

			bool bValid = false;
			if (mesh->GetSubmeshCount() != 0)
			{
				if (mesh->GetRelativeFilePath().compare(meshFilePath) == 0)
				{
					// Same prefab is being drawn as last frame
					bValid = true;
				}
				else
				{
					mesh->Destroy();
					// Restore game object reference
					mesh->SetOwner(m_HologramProxyObject);
				}
			}

			if (m_HologramMatID == InvalidMaterialID)
			{
				g_Renderer->FindOrCreateMaterialByName("Selection Hologram", m_HologramMatID);
			}

			if (!bValid)
			{
				bValid = mesh->LoadFromFile(meshFilePath, m_HologramMatID);
				m_HologramProxyObject->PostInitialize();
			}

			if (bValid)
			{
				m_HologramProxyObject->SetVisible(true);

				Material* hologramMat = GetMaterial(m_HologramMatID);
				if (hologramMat != nullptr)
				{
					hologramMat->constEmissive = m_QueuedHologramColour;

					Transform* transform = m_HologramProxyObject->GetTransform();
					transform->SetWorldPosition(m_QueuedHologramPosWS, false);
					transform->SetWorldRotation(m_QueuedHologramRotWS, false);
					transform->SetWorldScale(m_QueuedHologramScaleWS, true);
				}
			}
			else
			{
				std::string prefabIDStr = m_QueuedHologramPrefabID.ToString();
				std::string prefabName = prefabTemplate->GetName();
				PrintError("Failed to load mesh for selection hologram proxy (prefab ID: %s, prefab name: %s)\n", prefabIDStr.c_str(), prefabName.c_str());
				// Restore game object reference
				mesh->SetOwner(m_HologramProxyObject);
				mesh->Destroy();
			}

			m_QueuedHologramPrefabID = InvalidPrefabID;
		}
	}

	void Renderer::EndOfFrame()
	{
		m_NotificationMessages.clear();
	}

	void Renderer::DrawImGuiWindows()
	{
#if COMPILE_SHADER_COMPILER
		ShaderCompiler::DrawImGuiShaderErrorsWindow(&m_bShaderErrorWindowShowing);
#endif
	}

	i32 Renderer::GetShortMaterialIndex(MaterialID materialID, bool bShowEditorMaterials)
	{
		i32 matShortIndex = 0;
		for (i32 i = 0; i < (i32)m_Materials.size(); ++i)
		{
			auto matIter = m_Materials.find(i);
			if (matIter == m_Materials.end() || (!bShowEditorMaterials && matIter->second->bEditorMaterial))
			{
				continue;
			}

			Material* material = matIter->second;

			if (!m_MaterialFilter.PassFilter(material->name.c_str()))
			{
				continue;
			}

			if (materialID == (MaterialID)i)
			{
				break;
			}

			++matShortIndex;
		}

		for (i32 i = 0; i < (i32)g_ResourceManager->parsedMaterialInfos.size(); ++i)
		{
			auto matIter = m_Materials.find(i);
			if (matIter == m_Materials.end() || (!bShowEditorMaterials && matIter->second->bEditorMaterial))
			{
				continue;
			}

			Material* material = matIter->second;

			if (!m_MaterialFilter.PassFilter(material->name.c_str()))
			{
				continue;
			}

			if (materialID == (MaterialID)i)
			{
				break;
			}

			++matShortIndex;
		}

		return matShortIndex;
	}

	bool Renderer::DrawImGuiMaterialList(MaterialID* selectedMaterialID, bool bShowEditorMaterials, bool bScrollToSelected)
	{
		bool bMaterialSelectionChanged = false;

		m_MaterialFilter.Draw("##material-filter");

		ImGui::SameLine();
		if (ImGui::Button("x"))
		{
			m_MaterialFilter.Clear();
		}

		if (ImGui::BeginChild("material list", ImVec2(0.0f, 120.0f), true))
		{
			i32 selectedMaterialShortIndex = g_Renderer->GetShortMaterialIndex(*selectedMaterialID, bShowEditorMaterials);
			i32 matShortIndex = 0;
			for (i32 i = 0; i < (i32)m_Materials.size(); ++i)
			{
				auto matIter = m_Materials.find(i);
				if (matIter == m_Materials.end() || (!bShowEditorMaterials && matIter->second->bEditorMaterial))
				{
					continue;
				}

				Material* material = matIter->second;

				if (!m_MaterialFilter.PassFilter(material->name.c_str()))
				{
					continue;
				}

				ImGui::PushID(i);

				bool bSelected = (matShortIndex == selectedMaterialShortIndex);
				const bool bWasEditorMat = material->bEditorMaterial;
				if (bWasEditorMat)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				}

				if (ImGui::Selectable(material->name.c_str(), &bSelected))
				{
					if (selectedMaterialShortIndex != matShortIndex)
					{
						selectedMaterialShortIndex = matShortIndex;
						*selectedMaterialID = (MaterialID)i;
						bMaterialSelectionChanged = true;
					}
				}

				if (bScrollToSelected && bSelected)
				{
					ImGui::SetScrollHereY();
				}

				if (bWasEditorMat)
				{
					ImGui::PopStyleColor();
				}

				std::string renamePopupWindowStr = "Rename material##popup" + std::to_string(i);
				static const u32 bufSize = 256;
				static char newNameBuf[bufSize];
				bool bOpenRename = false;
				if (ImGui::BeginPopupContextItem())
				{
					if (ImGui::Button("Duplicate"))
					{
						MaterialCreateInfo createInfo = {};
						createInfo.name = GetIncrementedPostFixedStr(material->name, "new material");
						createInfo.shaderName = m_Shaders[material->shaderID]->name;
						createInfo.constAlbedo = material->constAlbedo;
						createInfo.constRoughness = material->constRoughness;
						createInfo.constMetallic = material->constMetallic;
						createInfo.colourMultiplier = material->colourMultiplier;
						// TODO: Copy other fields
						InitializeMaterial(&createInfo);

						ImGui::CloseCurrentPopup();
					}
					if (ImGui::Button("Rename"))
					{
						bOpenRename = true;
					}

					ImGui::EndPopup();
				}

				if (bOpenRename)
				{
					ImGui::OpenPopup(renamePopupWindowStr.c_str());
					strncpy(newNameBuf, material->name.c_str(), bufSize);
				}

				if (ImGui::BeginPopupModal(renamePopupWindowStr.c_str(), NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
				{
					bool bRename = false;

					if (ImGui::InputText("Material name", newNameBuf, bufSize, ImGuiInputTextFlags_EnterReturnsTrue))
					{
						bRename = true;
						ImGui::CloseCurrentPopup();
					}

					if (ImGui::Button("Rename"))
					{
						bRename = true;
						ImGui::CloseCurrentPopup();
					}

					ImGui::SameLine();

					if (ImGui::Button("Cancel"))
					{
						ImGui::CloseCurrentPopup();
					}

					if (bRename)
					{
						// TODO: Look through scene files and replace with new name

						newNameBuf[bufSize - 1] = '\0';
						material->name = std::string(newNameBuf);
						bMaterialSelectionChanged = true;
					}

					if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
					{
						ImGui::CloseCurrentPopup();
					}

					if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
					{
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

						ImGui::SetDragDropPayload(Editor::MaterialPayloadCStr, data, size);

						ImGui::Text("%s", material->name.c_str());

						ImGui::EndDragDropSource();
					}
				}

				++matShortIndex;

				ImGui::PopID();
			}

			ImGui::Separator();

			// Display unloaded materials
			for (u32 i = 0; i < (u32)g_ResourceManager->parsedMaterialInfos.size(); ++i)
			{
				const MaterialCreateInfo& matCreateInfo = g_ResourceManager->parsedMaterialInfos[i];

				bool bMaterialLoaded = g_Renderer->GetMaterialID(matCreateInfo.name) != InvalidMaterialID;
				if (bMaterialLoaded || !m_MaterialFilter.PassFilter(matCreateInfo.name.c_str()))
				{
					continue;
				}

				ImGui::PushID(i);

				bool bSelected = (matShortIndex == selectedMaterialShortIndex);

				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				if (ImGui::Selectable(matCreateInfo.name.c_str(), &bSelected))
				{
					if (selectedMaterialShortIndex != matShortIndex)
					{
						// Immediately load unloaded materials upon selection
						MaterialID newMatID = InvalidMaterialID;
						if (g_Renderer->FindOrCreateMaterialByName(matCreateInfo.name, newMatID))
						{
							selectedMaterialShortIndex = matShortIndex;
							*selectedMaterialID = newMatID;
							bMaterialSelectionChanged = true;
						}
						else
						{
							PrintError("Failed to create material from create info %s\n", matCreateInfo.name.c_str());
						}
					}
				}
				ImGui::PopStyleColor();

				++matShortIndex;

				ImGui::PopID();
			}
		}
		ImGui::EndChild(); // Material list

		return bMaterialSelectionChanged;
	}

	void Renderer::DrawImGuiTexturePreviewTooltip(Texture* texture)
	{
		ImGui::BeginTooltip();

		ImVec2 cursorPos = ImGui::GetCursorPos();

		real textureAspectRatio = (real)texture->width / (real)texture->height;
		real texSize = 128.0f;

		if (texture->channelCount == 4) // Contains alpha channel, draw checkerboard behind
		{
			real tiling = 3.0f;
			ImVec2 uv0(0.0f, 0.0f);
			ImVec2 uv1(tiling * textureAspectRatio, tiling);
			DrawImGuiTexture(alphaBGTextureID, texSize, uv0, uv1);
		}

		ImGui::SetCursorPos(cursorPos);

		DrawImGuiTexture(texture, texSize);

		ImGui::EndTooltip();
	}

	bool Renderer::DrawImGuiForGameObject(GameObject* gameObject, bool bDrawingEditorObjects)
	{
		Mesh* mesh = gameObject->GetMesh();
		bool bAnyPropertyChanged = false;

		if (mesh != nullptr)
		{
			ImGui::Text("Materials");

			std::vector<MeshComponent*> subMeshes = mesh->GetSubMeshesCopy();

			real windowWidth = ImGui::GetContentRegionAvailWidth();
			real maxWindowHeight = 170.0f;
			real maxItemCount = 5.0f;
			real verticalPad = 8.0f;
			real windowHeight = glm::min(subMeshes.size() * maxWindowHeight / maxItemCount + verticalPad, maxWindowHeight);
			if (ImGui::BeginChild("materials", ImVec2(windowWidth - 4.0f, windowHeight), true))
			{
				for (u32 slotIndex = 0; slotIndex < subMeshes.size(); ++slotIndex)
				{
					MeshComponent* meshComponent = subMeshes[slotIndex];

					if (meshComponent == nullptr || meshComponent->renderID == InvalidRenderID)
					{
						continue;
					}

					if (meshComponent->DrawImGui(slotIndex, bDrawingEditorObjects))
					{
						bAnyPropertyChanged = true;
						break;
					}
				}
			}
			ImGui::EndChild();

			bAnyPropertyChanged = mesh->DrawImGui() || bAnyPropertyChanged;

			bool bCastsShadow = gameObject->CastsShadow();
			if (ImGui::Checkbox("Casts shadow", &bCastsShadow))
			{
				bAnyPropertyChanged = true;
				gameObject->SetCastsShadow(bCastsShadow);
			}
		}

		return bAnyPropertyChanged;
	}

	void Renderer::QueueHologramMesh(PrefabID prefabID, Transform& transform, const glm::vec4& colour)
	{
		QueueHologramMesh(prefabID, transform.GetWorldPosition(), transform.GetWorldRotation(), transform.GetWorldScale(), colour);
	}

	void Renderer::QueueHologramMesh(PrefabID prefabID, const glm::vec3& posWS, const glm::quat& rotWS, const glm::vec3& scaleWS, const glm::vec4& colour)
	{
		m_QueuedHologramPrefabID = prefabID;
		m_QueuedHologramPosWS = posWS;
		m_QueuedHologramRotWS = rotWS;
		m_QueuedHologramScaleWS = scaleWS;
		m_QueuedHologramColour = colour;
	}

	void Renderer::OnPreSceneChange()
	{
		if (m_PhysicsDebugDrawer != nullptr)
		{
			m_PhysicsDebugDrawer->OnPreSceneChange();
		}
	}

	void Renderer::OnPostSceneChange()
	{
		m_UIMesh->OnPostSceneChange();

		if (m_PhysicsDebugDrawer != nullptr)
		{
			m_PhysicsDebugDrawer->OnPostSceneChange();
		}
	}

	u32 Renderer::ParseShaderBufferFields(const std::vector<std::string>& fileLines, u32 j, std::vector<std::string>& outFields)
	{
		u32 lineCount = (u32)fileLines.size();

		while (j < lineCount)
		{
			std::string subFileLine = Trim(fileLines[j]);
			if (!StartsWith(subFileLine, "OpMemberName"))
			{
				break;
			}

			size_t firstQuote = subFileLine.find('\"');
			size_t secondQuote = subFileLine.rfind('\"');
			if (firstQuote != std::string::npos && secondQuote != std::string::npos && firstQuote != secondQuote)
			{
				std::string fieldName = subFileLine.substr(firstQuote + 1, secondQuote - firstQuote - 1);
				if (!Contains(outFields, fieldName))
				{
					outFields.push_back(fieldName);
				}
			}

			++j;
		}

		return j - 1;
	}

	void Renderer::ParseShaderMetaData()
	{
		m_ShaderMetaData = {};

		struct AttributeTypeMetaData
		{
			const char* spvSuffix;
			const std::type_info& type;
			const char* friendlyName;
		};
		static const AttributeTypeMetaData attributeMetaData[] =
		{
			{ "int", typeid(i32), "int" },
			{ "float", typeid(real), "float" },
			{ "v2float", typeid(glm::vec2), "vec2" },
			{ "v3float", typeid(glm::vec3), "vec3" },
			{ "v4float", typeid(glm::vec4), "vec4" },
		};

		std::vector<std::string> shaderFilePaths;
		if (Platform::FindFilesInDirectory(COMPILED_SHADERS_DIRECTORY, shaderFilePaths, "asm"))
		{
			for (const std::string& shaderFilePath : shaderFilePaths)
			{
				std::string fileContents;
				if (ReadFile(shaderFilePath, fileContents, false))
				{
					ShaderMetaData shaderMetaData = {};

					const std::string shaderFileNameOrig = StripFileType(StripLeadingDirectories(shaderFilePath));
					std::string shaderFileName = shaderFileNameOrig;
					std::string shaderType;

					// Remove "vk_" prefix
					if (StartsWith(shaderFileName, "vk_"))
					{
						shaderFileName = shaderFileName.substr(3);
					}

					// Remove shader type from name (e.g. "_frag")
					size_t finalUnderscore = shaderFileName.rfind('_');
					if (finalUnderscore != std::string::npos)
					{
						shaderType = shaderFileName.substr(finalUnderscore + 1);
						shaderFileName = shaderFileName.substr(0, finalUnderscore);
					}

					auto iter = m_ShaderMetaData.find(shaderFileName);
					if (iter != m_ShaderMetaData.end())
					{
						// Another stage of this shader has already been parsed, append any new results from this one
						shaderMetaData = iter->second;
					}

					std::vector<std::string> fileLines = Split(fileContents, '\n');
					u32 lineCount = (u32)fileLines.size();
					for (u32 i = 0; i < lineCount; ++i)
					{
						std::string fileLine = Trim(fileLines[i]);
						if (StartsWith(fileLine, "OpName %UBOConstant \"UBOConstant\""))
						{
							i = ParseShaderBufferFields(fileLines, i + 1, shaderMetaData.uboConstantFields);
						}
						else if (StartsWith(fileLine, "OpName %UBODynamic \"UBODynamic\""))
						{
							i = ParseShaderBufferFields(fileLines, i + 1, shaderMetaData.uboDynamicFields);
						}
						else if (Contains(fileLine, " OpVariable ") && Contains(fileLine, " Input") && shaderType.compare("vert") == 0)
						{
							// Vertex attributes
							size_t percent = fileLine.find('%');
							size_t equals = fileLine.find('=');
							if (percent != std::string::npos && equals != std::string::npos && percent != equals)
							{
								std::string attributeName = Trim(fileLine.substr(percent + 1, equals - percent - 1));
								if (!StartsWith(attributeName, "gl_") && !Contains(shaderMetaData.vertexAttributes, attributeName))
								{
									shaderMetaData.vertexAttributes.push_back(attributeName);

									const char* prefixStr = "_ptr_Input_";
									size_t attributeSizePrefix = fileLine.find(prefixStr);
									if (attributeSizePrefix != std::string::npos)
									{
										size_t attributeSizeSuffix = fileLine.rfind(' ');
										if (attributeSizeSuffix != std::string::npos && attributeSizePrefix != attributeSizeSuffix)
										{
											size_t attrStart = attributeSizePrefix + strlen(prefixStr);
											std::string attributeSizeStr = fileLine.substr(attrStart, attributeSizeSuffix - attrStart);

											// Ensure that attribute has correct size
											for (const AttributeTypeMetaData& metaData : attributeMetaData)
											{
												if (attributeSizeStr.compare(metaData.spvSuffix) == 0)
												{
													if (!CompareVertexAttributeType(attributeName.c_str(), metaData.type))
													{
														PrintError("Mismatched vertex attribute type in shader %s for attribute %s (expected %s)\n", shaderFileNameOrig.c_str(), attributeName.c_str(), metaData.friendlyName);
													}

													break;
												}
											}
										}

									}
								}
							}
						}
						else if (Contains(fileLine, " OpVariable ") && Contains(fileLine, " UniformConstant"))
						{
							// Texture samplers
							size_t percent = fileLine.find('%');
							size_t equals = fileLine.find('=');
							if (percent != std::string::npos && equals != std::string::npos && percent != equals)
							{
								std::string attributeName = Trim(fileLine.substr(percent + 1, equals - percent - 1));
								if (!Contains(shaderMetaData.sampledTextures, attributeName))
								{
									shaderMetaData.sampledTextures.push_back(attributeName);
								}
							}
						}
					}

					m_ShaderMetaData[shaderFileName] = shaderMetaData;
				}
			}
		}
	}

	void Renderer::LoadShaders()
	{
		PROFILE_AUTO("Renderer LoadShaders");

#define USE_SHADER_REFLECTION 0
#if USE_SHADER_REFLECTION
		ParseShaderMetaData();

		if (m_ShaderMetaData.empty())
		{
			// Block to wait for shader compile to complete
			while (!m_ShaderCompiler->bComplete)
			{
				if (!m_ShaderCompiler->TickStatus())
				{
					Platform::Sleep(1);
				}
			}

			ParseShaderMetaData();
		}
#endif

#if COMPILE_SHADER_COMPILER
		while (m_ShaderCompiler != nullptr && !m_ShaderCompiler->bComplete)
		{
			// Wait for shader compilation to complete
			// TODO: Only wait for engine shaders
			if (m_ShaderCompiler->TickStatus())
			{
				break;
			}
			Platform::Sleep(1);
		}
#endif
		std::vector<ShaderInfo> shaderInfos;
		SUPPRESS_WARN_BEGIN;
		shaderInfos = {
			{ "deferred_combine", "vk_deferred_combine_vert.spv", "vk_deferred_combine_frag.spv", "", "" },
			{ "colour", "vk_colour_vert.spv","vk_colour_frag.spv", "", "" },
			{ "ui", "vk_ui_vert.spv","vk_ui_frag.spv", "", "" },
			{ "pbr", "vk_pbr_vert.spv", "vk_pbr_frag.spv", "", "" },
			{ "pbr_ws", "vk_pbr_ws_vert.spv", "vk_pbr_ws_frag.spv", "", "" },
			{ "skybox", "vk_skybox_vert.spv", "vk_skybox_frag.spv", "", "" },
			{ "equirectangular_to_cube", "vk_skybox_vert.spv", "vk_equirectangular_to_cube_frag.spv", "", "" },
			{ "irradiance", "vk_skybox_vert.spv", "vk_irradiance_frag.spv", "", "" },
			{ "prefilter", "vk_skybox_vert.spv", "vk_prefilter_frag.spv", "", "" },
			{ "brdf", "vk_brdf_vert.spv", "vk_brdf_frag.spv", "", "" },
			{ "sprite", "vk_sprite_vert.spv", "vk_sprite_frag.spv", "", "" },
			{ "sprite_arr", "vk_sprite_vert.spv", "vk_sprite_arr_frag.spv", "", "" },
			{ "post_process", "vk_post_process_vert.spv", "vk_post_process_frag.spv", "", "" },
			//{ "post_fxaa", "vk_barebones_pos2_uv_vert.spv", "vk_post_fxaa_frag.spv", "", "" },
			{ "compute_sdf", "vk_compute_sdf_vert.spv", "vk_compute_sdf_frag.spv", "", "" },
			{ "font_ss", "vk_font_ss_vert.spv", "vk_font_frag.spv", "vk_font_ss_geom.spv", "" },
			{ "font_ws", "vk_font_ws_vert.spv", "vk_font_frag.spv", "vk_font_ws_geom.spv", "" },
			{ "shadow", "vk_shadow_vert.spv", "", "", "" },
			{ "ssao", "vk_barebones_pos2_uv_vert.spv", "vk_ssao_frag.spv", "", "" }, // TODO: Why not barebones pos2?
			{ "ssao_blur", "vk_barebones_pos2_uv_vert.spv", "vk_ssao_blur_frag.spv", "", "" },
			{ "taa_resolve", "vk_barebones_pos2_uv_vert.spv", "vk_taa_resolve_frag.spv", "", "" },
			{ "gamma_correct", "vk_barebones_pos2_uv_vert.spv", "vk_gamma_correct_frag.spv", "", "" },
			{ "blit", "vk_barebones_pos2_uv_vert.spv", "vk_blit_frag.spv", "", "" },
			{ "particle_sim", "", "", "", "vk_simulate_particles_comp.spv" },
			{ "particles", "vk_particles_vert.spv", "vk_particles_frag.spv", "vk_particles_geom.spv", "" },
			{ "terrain", "vk_terrain_vert.spv", "vk_terrain_frag.spv", "", "" },
			{ "water", "vk_water_vert.spv", "vk_water_frag.spv", "", "" },
			{ "wireframe", "vk_wireframe_vert.spv", "vk_wireframe_frag.spv", "vk_wireframe_geom.spv", "" },
			{ "emissive", "vk_emissive_vert.spv", "vk_emissive_frag.spv", "", "" },
			{ "battery_charge", "vk_battery_charge_vert.spv", "vk_battery_charge_frag.spv", "", "" },
			{ "cloud", "vk_cloud_vert.spv", "vk_cloud_frag.spv", "", "" },
			{ "terrain_generate_points", "", "", "", "vk_terrain_generate_points_comp.spv" },
			{ "terrain_generate_mesh", "", "", "", "vk_terrain_generate_mesh_comp.spv" },
			{ "hologram", "vk_hologram_vert.spv", "vk_hologram_frag.spv", "", "" },
		};
		SUPPRESS_WARN_END;

		InitializeShaders(shaderInfos);

		ShaderID shaderID = 0;

#if USE_SHADER_REFLECTION
		for (u32 shaderID = 0; shaderID < (u32)m_Shaders.size(); ++shaderID)
		{
			ShaderMetaData& shaderMetaData = m_ShaderMetaData[m_Shaders[shaderID]->name];
			Shader* shader = m_Shaders[shaderID];

			for (const std::string& attributeName : shaderMetaData.vertexAttributes)
			{
				shader->vertexAttributes |= (u32)VertexAttributeFromString(attributeName.c_str());
			}

			for (const std::string& field : shaderMetaData.uboConstantFields)
			{
				Uniform const* uniform = UniformFromStringID(Hash(field.c_str()));
				if (uniform != nullptr)
				{
					shader->constantBufferUniforms.AddUniform(uniform);
				}
				else
				{
					PrintError("Unknown uniform %s in constant uniform buffer in shader %s\n", field.c_str(), shader->name.c_str());
				}
			}

			for (const std::string& field : shaderMetaData.uboDynamicFields)
			{
				Uniform const* uniform = UniformFromStringID(Hash(field.c_str()));
				if (uniform != nullptr)
				{
					shader->dynamicBufferUniforms.AddUniform(UniformFromStringID(Hash(field.c_str())));
				}
				else
				{
					PrintError("Unknown uniform %s in dynamic uniform buffer in shader %s\n", field.c_str(), shader->name.c_str());
				}
			}

			for (const std::string& textureName : shaderMetaData.sampledTextures)
			{
				Uniform const* uniform = UniformFromStringID(Hash(textureName.c_str()));
				if (uniform != nullptr)
				{
					shader->textureUniforms.AddUniform(UniformFromStringID(Hash(textureName.c_str())));
				}
				else
				{
					PrintError("Unknown texture uniform %s in shader %s\n", textureName.c_str(), shader->name.c_str());
				}
			}
		}
#else // USE_SHADER_REFLECTION
		// Deferred combine
		m_Shaders[shaderID]->renderPassType = RenderPassType::DEFERRED_COMBINE;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		// TODO: Specify that this buffer is only used in the frag shader here
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_CAM_POS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_PROJECTION_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_DIR_LIGHT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_POINT_LIGHTS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SPOT_LIGHTS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_AREA_LIGHTS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SKYBOX_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SHADOW_SAMPLING_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SSAO_SAMPLING_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_NEAR_FAR_PLANES);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_BRDF_LUT_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_IRRADIANCE_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_PREFILTER_MAP);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_DEPTH_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_SSAO_FINAL_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_SHADOW_CASCADES_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_FB_0_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_FB_1_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_LTC_MATRICES_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_LTC_AMPLITUDES_SAMPLER);
		++shaderID;

		// Colour
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bDepthWriteEnable = true;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->dynamicVertexBufferSize = 16384 * 4 * 28; // (1835008) TODO: FIXME:
		m_Shaders[shaderID]->maxObjectCount = 32;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_PROJECTION);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_COLOUR_MULTIPLIER);
		++shaderID;

		// UI
		m_Shaders[shaderID]->renderPassType = RenderPassType::UI;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->dynamicVertexBufferSize = 16384 * 4 * 28; // (1835008) TODO: FIXME:
		m_Shaders[shaderID]->maxObjectCount = 64;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_COLOUR_MULTIPLIER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UV_BLEND_AMOUNT);
		++shaderID;

		// PBR
		m_Shaders[shaderID]->renderPassType = RenderPassType::DEFERRED;
		m_Shaders[shaderID]->numAttachments = 2; // TODO: Work out automatically from samplers?
		m_Shaders[shaderID]->dynamicVertexBufferSize = 10 * 1024 * 1024; // 10MB
		m_Shaders[shaderID]->maxObjectCount = 32;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT |
			(u32)VertexAttribute::NORMAL |
			(u32)VertexAttribute::TANGENT;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_PROJECTION);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_ALBEDO);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_METALLIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_METALLIC_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_ROUGHNESS);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_ROUGHNESS_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_NORMAL_SAMPLER);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_METALLIC_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ROUGHNESS_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_NORMAL_SAMPLER);
		++shaderID;

		// PBR - WORLD SPACE
		m_Shaders[shaderID]->renderPassType = RenderPassType::DEFERRED;
		m_Shaders[shaderID]->numAttachments = 2;
		m_Shaders[shaderID]->maxObjectCount = 32;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::NORMAL |
			(u32)VertexAttribute::TANGENT;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_PROJECTION);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_ALBEDO);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_METALLIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_METALLIC_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_ROUGHNESS);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_ROUGHNESS_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_NORMAL_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_TEXTURE_SCALE);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_BLEND_SHARPNESS);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_METALLIC_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ROUGHNESS_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_NORMAL_SAMPLER);
		++shaderID;

		// Skybox
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 128;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SKYBOX_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_TIME);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_CUBEMAP_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		++shaderID;

		// Equirectangular to Cube
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 128;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_HDR_EQUIRECTANGULAR_SAMPLER);
		++shaderID;

		// Irradiance
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 128;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_CUBEMAP_SAMPLER);
		++shaderID;

		// Prefilter
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 128;
		m_Shaders[shaderID]->maxObjectCount = 1;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_ROUGHNESS);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_CUBEMAP_SAMPLER);
		++shaderID;

		// BRDF
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->vertexAttributes = 0;
		++shaderID;

		// Sprite
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 132;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->renderPassType = RenderPassType::UI;
		m_Shaders[shaderID]->maxObjectCount = 64;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_COLOUR_MULTIPLIER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_ALBEDO_SAMPLER);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		++shaderID;

		// Sprite - Texture Array
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 132;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->bTextureArr = true;
		m_Shaders[shaderID]->dynamicVertexBufferSize = 1024 * 1024; // TODO: FIXME:
		m_Shaders[shaderID]->renderPassType = RenderPassType::UI;
		m_Shaders[shaderID]->maxObjectCount = 16;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_COLOUR_MULTIPLIER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_ALBEDO_SAMPLER);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		++shaderID;

		// Post processing
		m_Shaders[shaderID]->renderPassType = RenderPassType::POST_PROCESS;
		m_Shaders[shaderID]->maxObjectCount = 1;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_COLOUR_MULTIPLIER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_POST_PROCESS_MAT);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_SCENE_SAMPLER);
		++shaderID;

		// Post FXAA
		//m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD; // TODO: FIXME:
		//m_Shaders[shaderID]->vertexAttributes =
		//	(u32)VertexAttribute::POSITION2 |
		//	(u32)VertexAttribute::UV;

		//m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);

		//m_Shaders[shaderID]->textureUniforms.AddUniform(&U_SCENE_SAMPLER);
		//++shaderID;

		// Compute SDF
		m_Shaders[shaderID]->renderPassType = RenderPassType::DEFERRED;
		m_Shaders[shaderID]->maxObjectCount = 256;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_SDF_DATA);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_TEX_CHANNEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_HIGH_RES_TEX);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CHAR_RESOLUTION);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_SPREAD);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_HIGH_RES_TEX);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_TEX_CHANNEL);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_SDF_RESOLUTION);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_HIGH_RES);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		++shaderID;

		// Font SS
		m_Shaders[shaderID]->renderPassType = RenderPassType::UI;
		m_Shaders[shaderID]->dynamicVertexBufferSize = 1024 * 1024; // TODO: FIXME:
		m_Shaders[shaderID]->maxObjectCount = 4;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT |
			(u32)VertexAttribute::EXTRA_VEC4 |
			(u32)VertexAttribute::EXTRA_INT;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_FONT_CHAR_DATA);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_TEX_SIZE);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		++shaderID;

		// Font WS
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->dynamicVertexBufferSize = 1024 * 1024; // TODO: FIXME:
		m_Shaders[shaderID]->maxObjectCount = 4;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT |
			(u32)VertexAttribute::TANGENT |
			(u32)VertexAttribute::EXTRA_VEC4 |
			(u32)VertexAttribute::EXTRA_INT;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_FONT_CHAR_DATA);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_TEX_SIZE);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_THRESHOLD);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_SHADOW);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		++shaderID;

		// Shadow
		m_Shaders[shaderID]->renderPassType = RenderPassType::SHADOW;
		m_Shaders[shaderID]->bGenerateVertexBufferForAll = true;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 64;
		m_Shaders[shaderID]->maxObjectCount = -1;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->constantBufferUniforms = {};

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		++shaderID;

		// SSAO
		m_Shaders[shaderID]->renderPassType = RenderPassType::SSAO;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_PROJECTION);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_PROJECTION_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SSAO_GEN_DATA);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_DEPTH_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_SSAO_NORMAL_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_NOISE_SAMPLER);
		++shaderID;

		// SSAO Blur
		m_Shaders[shaderID]->renderPassType = RenderPassType::SSAO_BLUR;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->maxObjectCount = 2;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SSAO_BLUR_DATA_CONSTANT);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_SSAO_BLUR_DATA_DYNAMIC);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_SSAO_RAW_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_SSAO_NORMAL_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_DEPTH_SAMPLER);
		++shaderID;

		// TAA Resolve
		m_Shaders[shaderID]->renderPassType = RenderPassType::TAA_RESOLVE;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 8;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION | // TODO: POS2
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_PROJECTION_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_LAST_FRAME_VIEWPROJ);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_SCENE_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_HISTORY_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_DEPTH_SAMPLER);

		m_Shaders[shaderID]->dynamicBufferUniforms = {};
		++shaderID;

		// Gamma Correct
		m_Shaders[shaderID]->renderPassType = RenderPassType::GAMMA_CORRECT;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION | // TODO: POS2
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_SCENE_SAMPLER);
		++shaderID;

		// Blit
		m_Shaders[shaderID]->renderPassType = RenderPassType::UI;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		++shaderID;

		// Simulate particles
		m_Shaders[shaderID]->renderPassType = RenderPassType::COMPUTE_PARTICLES;
		m_Shaders[shaderID]->bCompute = true;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_PARTICLE_SIM_DATA);

		m_Shaders[shaderID]->additionalBufferUniforms.AddUniform(&U_PARTICLE_BUFFER);
		++shaderID;

		// Particles
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bDepthWriteEnable = true;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::VELOCITY3 |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT |
			(u32)VertexAttribute::EXTRA_VEC4;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_CAM_POS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_PROJECTION);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		++shaderID;

		// Terrain
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bDepthWriteEnable = true;
		m_Shaders[shaderID]->maxObjectCount = 4096 * 8; // TODO: -1
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::NORMAL |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_PROJECTION);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_DIR_LIGHT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_POINT_LIGHTS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SPOT_LIGHTS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_AREA_LIGHTS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SKYBOX_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SHADOW_SAMPLING_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_NEAR_FAR_PLANES);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_SHADOW_CASCADES_SAMPLER);
		++shaderID;

		// Water
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bDepthWriteEnable = true;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::NORMAL |
			(u32)VertexAttribute::TANGENT |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_CAM_POS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_PROJECTION);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_DIR_LIGHT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_OCEAN_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SKYBOX_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_TIME);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		++shaderID;

		// Wireframe
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->maxObjectCount = 2048;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_PROJECTION);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_COLOUR_MULTIPLIER);
		++shaderID;

		// Emissive
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT |
			(u32)VertexAttribute::NORMAL |
			(u32)VertexAttribute::TANGENT;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_PROJECTION);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_ALBEDO);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_EMISSIVE);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_EMISSIVE_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_ROUGHNESS);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_NORMAL_SAMPLER);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_EMISSIVE_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_NORMAL_SAMPLER);
		++shaderID;

		// Battery Charge
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_PROJECTION);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_TIME);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_ALBEDO);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_EMISSIVE);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_ENABLE_EMISSIVE_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CHARGE_AMOUNT);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_EMISSIVE_SAMPLER);
		++shaderID;

		// Cloud
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes = (u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_PROJECTION);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_TIME);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SKYBOX_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_SCREEN_SIZE);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		++shaderID;

		// Terrain generate points
		m_Shaders[shaderID]->renderPassType = RenderPassType::COMPUTE_TERRAIN;
		m_Shaders[shaderID]->bCompute = true;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_TERRAIN_GEN_CONSTANT_DATA);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_TERRAIN_GEN_DYNAMIC_DATA);

		m_Shaders[shaderID]->additionalBufferUniforms.AddUniform(&U_TERRAIN_POINT_BUFFER);

		m_Shaders[shaderID]->textureUniforms.AddUniform(&U_RANDOM_TABLES);
		++shaderID;

		// Terrain generate mesh
		m_Shaders[shaderID]->renderPassType = RenderPassType::COMPUTE_TERRAIN;
		m_Shaders[shaderID]->bCompute = true;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_TERRAIN_GEN_CONSTANT_DATA);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_TERRAIN_GEN_DYNAMIC_DATA);

		m_Shaders[shaderID]->additionalBufferUniforms.AddUniform(&U_TERRAIN_POINT_BUFFER);
		m_Shaders[shaderID]->additionalBufferUniforms.AddUniform(&U_TERRAIN_VERTEX_BUFFER);
		++shaderID;

		// Hologram
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::NORMAL;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_VIEW_PROJECTION);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(&U_TIME);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(&U_CONST_EMISSIVE);
		++shaderID;

		CHECK_EQ(shaderID, m_Shaders.size());
#endif // USE_SHADER_REFLECTION

		for (shaderID = 0; shaderID < (ShaderID)m_Shaders.size(); ++shaderID)
		{
			Shader* shader = m_Shaders[shaderID];

			shader->dynamicVertexIndexBufferIndex = GetDynamicVertexIndexBufferIndex(CalculateVertexStride(shader->vertexAttributes));

			// Sanity checks
			{
				CHECK(!shader->constantBufferUniforms.HasUniform(&U_UNIFORM_BUFFER_DYNAMIC));
				CHECK(!shader->dynamicBufferUniforms.HasUniform(&U_UNIFORM_BUFFER_CONSTANT));

				CHECK((shader->bNeedPushConstantBlock && shader->pushConstantBlockSize != 0) ||
					(!shader->bNeedPushConstantBlock && shader->pushConstantBlockSize == 0));


				if (shader->textureUniforms.HasUniform(&U_HIGH_RES_TEX))
				{
					CHECK(!shader->textureUniforms.HasUniform(&U_ALBEDO_SAMPLER));
				}

				// -1 means allocate max, anything else must be > 0
				CHECK_NE(shader->maxObjectCount, 0);
			}

			if (!LoadShaderCode(shaderID))
			{
				// TODO: Display errors in editor
				PrintError("Couldn't load/compile shader: %s", shader->name.c_str());
				if (!shader->vertexShaderFilePath.empty())
				{
					PrintError(" %s", shader->vertexShaderFilePath.c_str());
				}
				if (!shader->fragmentShaderFilePath.empty())
				{
					PrintError(" %s", shader->fragmentShaderFilePath.c_str());
				}
				if (!shader->geometryShaderFilePath.empty())
				{
					PrintError(" %s", shader->geometryShaderFilePath.c_str());
				}
				if (!shader->computeShaderFilePath.empty())
				{
					PrintError(" %s", shader->computeShaderFilePath.c_str());
				}
				PrintError("\n");
			}
		}

		// Cache shader IDs
		GetShaderID("ui", m_UIShaderID);
	}

	void Renderer::GenerateGBuffer()
	{
		PROFILE_AUTO("GenerateGBuffer");

		CHECK_NE(m_SkyBoxMesh, nullptr);
		CHECK_NE(m_SkyboxShaderID, InvalidShaderID);
		MaterialID skyboxMaterialID = m_SkyBoxMesh->GetSubMesh(0)->GetMaterialID();

		const std::string gBufferMatName = "GBuffer material";
		const std::string gBufferCubeMatName = "GBuffer cubemap material";
		const std::string gBufferQuadName = "GBuffer quad";

		// Remove existing material if present (this will be true when reloading the scene)
		{
			MaterialID existingGBufferQuadMatID = InvalidMaterialID;
			MaterialID existingGBufferCubeMatID = InvalidMaterialID;
			// TODO: Don't rely on material names!
			if (FindOrCreateMaterialByName(gBufferMatName, existingGBufferQuadMatID))
			{
				RemoveMaterial(existingGBufferQuadMatID);
			}
			if (FindOrCreateMaterialByName(gBufferCubeMatName, existingGBufferCubeMatID))
			{
				RemoveMaterial(existingGBufferCubeMatID);
			}

			for (auto iter = m_PersistentObjects.begin(); iter != m_PersistentObjects.end(); ++iter)
			{
				GameObject* gameObject = *iter;
				if (gameObject->GetName().compare(gBufferQuadName) == 0)
				{
					gameObject->Destroy();
					delete gameObject;
					m_PersistentObjects.erase(iter);
					break;
				}
			}

			if (m_GBufferQuadRenderID != InvalidRenderID)
			{
				DestroyRenderObject(m_GBufferQuadRenderID);
				m_GBufferQuadRenderID = InvalidRenderID;
			}
		}

		{
			MaterialCreateInfo gBufferMaterialCreateInfo = {};
			gBufferMaterialCreateInfo.name = gBufferMatName;
			gBufferMaterialCreateInfo.shaderName = "deferred_combine";
			gBufferMaterialCreateInfo.irradianceSamplerMatID = skyboxMaterialID;
			gBufferMaterialCreateInfo.enablePrefilteredMap = true;
			gBufferMaterialCreateInfo.prefilterMapSamplerMatID = skyboxMaterialID;
			gBufferMaterialCreateInfo.enableBRDFLUT = true;
			gBufferMaterialCreateInfo.renderToCubemap = false;
			gBufferMaterialCreateInfo.persistent = true;
			gBufferMaterialCreateInfo.bEditorMaterial = true;
			gBufferMaterialCreateInfo.bSerializable = false;
			FillOutGBufferFrameBufferAttachments(gBufferMaterialCreateInfo.sampledFrameBuffers);

			MaterialID gBufferMatID = InitializeMaterial(&gBufferMaterialCreateInfo);

			GameObject* gBufferQuadGameObject = new GameObject(gBufferQuadName, SID("object"));
			m_PersistentObjects.push_back(gBufferQuadGameObject);
			// NOTE: G-buffer isn't rendered normally, it is handled separately
			gBufferQuadGameObject->SetVisible(false);

			RenderObjectCreateInfo gBufferQuadCreateInfo = {};
			gBufferQuadCreateInfo.materialID = gBufferMatID;
			gBufferQuadCreateInfo.gameObject = gBufferQuadGameObject;
			gBufferQuadCreateInfo.vertexBufferData = &m_FullScreenTriVertexBufferData;
			gBufferQuadCreateInfo.cullFace = CullFace::NONE;
			gBufferQuadCreateInfo.visibleInSceneExplorer = false;
			gBufferQuadCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
			gBufferQuadCreateInfo.bSetDynamicStates = true;

			m_GBufferQuadRenderID = InitializeRenderObject(&gBufferQuadCreateInfo);
		}

		// Initialize GBuffer cubemap material & mesh
		//{
		//	MaterialCreateInfo gBufferCubemapMaterialCreateInfo = {};
		//	gBufferCubemapMaterialCreateInfo.name = gBufferCubeMatName;
		//	gBufferCubemapMaterialCreateInfo.shaderName = "deferred_combine_cubemap";
		//	gBufferCubemapMaterialCreateInfo.irradianceSamplerMatID = skyboxMaterialID;
		//	gBufferCubemapMaterialCreateInfo.enablePrefilteredMap = true;
		//	gBufferCubemapMaterialCreateInfo.prefilterMapSamplerMatID = skyboxMaterialID;
		//	gBufferCubemapMaterialCreateInfo.enableBRDFLUT = true;
		//	gBufferCubemapMaterialCreateInfo.renderToCubemap = false;
		//	gBufferCubemapMaterialCreateInfo.persistent = true;
		//	gBufferCubemapMaterialCreateInfo.bEditorMaterial = true;
		//	FillOutGBufferFrameBufferAttachments(gBufferCubemapMaterialCreateInfo.sampledFrameBuffers);
		//
		//	m_CubemapGBufferMaterialID = InitializeMaterial(&gBufferCubemapMaterialCreateInfo);
		//}
	}

	void Renderer::EnqueueScreenSpaceText()
	{
		real topRightX = -0.03f;
		real topRightOffset = -0.055f;
		real lineHeight = -2.0f * SetFont(SID("editor-02"))->GetMetric('W')->height / (real)g_Window->GetSize().y;

		static const glm::vec4 offWhite(0.95f);
		DrawStringSS("FLEX ENGINE", offWhite, AnchorPoint::TOP_RIGHT, glm::vec2(topRightX, topRightOffset), 1.5f, 0.6f);
		topRightOffset += lineHeight;

		if (g_EngineInstance->IsSimulationPaused())
		{
			DrawStringSS("PAUSED", offWhite, AnchorPoint::TOP_RIGHT, glm::vec2(topRightX, topRightOffset), 0.0f, 0.6f);
			topRightOffset += lineHeight;
		}

		if (AudioManager::IsMuted())
		{
			DrawStringSS("Muted", offWhite, AnchorPoint::TOP_RIGHT, glm::vec2(topRightX, topRightOffset), 0.0f, 0.6f);
			topRightOffset += lineHeight;
		}

#if 0
		std::string str;
		std::string fxaaEnabledStr = std::string("FXAA: ") + (m_PostProcessSettings.bEnableFXAA ? "1" : "0");
		DrawStringSS(fxaaEnabledStr, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP_RIGHT, glm::vec2(-0.03f, -0.15f), 1.0f, 0.35f);
		glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		std::string resolutionStr = "Frame buffer size: " + IntToString(frameBufferSize.x) + " x " + IntToString(frameBufferSize.y);
		DrawStringSS(resolutionStr, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP_RIGHT, glm::vec2(-0.03f, -0.175f), 1.0f, 0.35f);
#endif

		if (m_EditorStrSecRemaining > 0.0f)
		{
			SetFont(SID("editor-01"));
			real alpha = glm::clamp(m_EditorStrSecRemaining / (m_EditorStrSecDuration * m_EditorStrFadeDurationPercent),
				0.0f, 1.0f);
			DrawStringSS(m_EditorMessage, glm::vec4(1.0f, 1.0f, 1.0f, alpha), AnchorPoint::CENTER, VEC2_ZERO, 3);
		}

		SetFont(SID("editor-02"));
		for (u32 i = 0; i < (u32)m_NotificationMessages.size(); ++i)
		{
			DrawStringSS(m_NotificationMessages[i], offWhite, AnchorPoint::TOP_RIGHT, glm::vec2(topRightX, topRightOffset), 1.5f, 0.6f);
			topRightOffset += lineHeight;
		}

		if (previewedFont != InvalidStringID)
		{
			SetFont(previewedFont);
			DrawStringSS("Preview text... 123 -*!~? ", VEC4_ONE, AnchorPoint::CENTER, VEC2_ZERO, 3);
		}
	}

	void Renderer::EnqueueWorldSpaceText()
	{
#if 0
		SetFont(SID("editor-02-ws"));
		real s = g_SecElapsedSinceProgramStart * 3.5f;
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(1.0f), 1.0f), glm::vec3(2.0f, 5.0f, 0.0f), QUAT_IDENTITY, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.95f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 1) * 0.05f, 5.0f + sin(s + 0.3f * 1) * 0.05f, -0.075f * 1), QUAT_IDENTITY, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.90f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 2) * 0.07f, 5.0f + sin(s + 0.3f * 2) * 0.07f, -0.075f * 2), QUAT_IDENTITY, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.85f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 3) * 0.10f, 5.0f + sin(s + 0.3f * 3) * 0.10f, -0.075f * 3), QUAT_IDENTITY, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.80f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 4) * 0.12f, 5.0f + sin(s + 0.3f * 4) * 0.12f, -0.075f * 4), QUAT_IDENTITY, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.75f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 5) * 0.15f, 5.0f + sin(s + 0.3f * 5) * 0.15f, -0.075f * 5), QUAT_IDENTITY, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.70f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 6) * 0.17f, 5.0f + sin(s + 0.3f * 6) * 0.17f, -0.075f * 6), QUAT_IDENTITY, 0.0f, 100.0f);
#endif
	}

	void Renderer::InitializeEngineMaterials()
	{
		PROFILE_AUTO("Renderer InitializeEngineMaterials");

		MaterialCreateInfo spriteMatSSCreateInfo = {};
		spriteMatSSCreateInfo.name = "Sprite SS material";
		spriteMatSSCreateInfo.shaderName = "sprite";
		spriteMatSSCreateInfo.persistent = true;
		spriteMatSSCreateInfo.bEditorMaterial = true;
		spriteMatSSCreateInfo.enableAlbedoSampler = true;
		spriteMatSSCreateInfo.bDynamic = false;
		spriteMatSSCreateInfo.bSerializable = false;
		m_SpriteMatSSID = InitializeMaterial(&spriteMatSSCreateInfo);

		MaterialCreateInfo spriteMatWSCreateInfo = {};
		spriteMatWSCreateInfo.name = "Sprite WS material";
		spriteMatWSCreateInfo.shaderName = "sprite";
		spriteMatWSCreateInfo.persistent = true;
		spriteMatWSCreateInfo.bEditorMaterial = true;
		spriteMatWSCreateInfo.enableAlbedoSampler = true;
		spriteMatWSCreateInfo.bDynamic = false;
		spriteMatWSCreateInfo.bSerializable = false;
		m_SpriteMatWSID = InitializeMaterial(&spriteMatWSCreateInfo);

		MaterialCreateInfo spriteArrMatCreateInfo = {};
		spriteArrMatCreateInfo.name = "Sprite Texture Array material";
		spriteArrMatCreateInfo.shaderName = "sprite_arr";
		spriteArrMatCreateInfo.persistent = true;
		spriteArrMatCreateInfo.bEditorMaterial = true;
		spriteArrMatCreateInfo.enableAlbedoSampler = true;
		spriteArrMatCreateInfo.bDynamic = false;
		spriteArrMatCreateInfo.bSerializable = false;
		m_SpriteArrMatID = InitializeMaterial(&spriteArrMatCreateInfo);

		MaterialCreateInfo fontSSMatCreateInfo = {};
		fontSSMatCreateInfo.name = "font ss";
		fontSSMatCreateInfo.shaderName = "font_ss";
		fontSSMatCreateInfo.persistent = true;
		fontSSMatCreateInfo.bEditorMaterial = true;
		fontSSMatCreateInfo.bDynamic = false;
		fontSSMatCreateInfo.bSerializable = false;
		m_FontMatSSID = InitializeMaterial(&fontSSMatCreateInfo);

		MaterialCreateInfo fontWSMatCreateInfo = {};
		fontWSMatCreateInfo.name = "font ws";
		fontWSMatCreateInfo.shaderName = "font_ws";
		fontWSMatCreateInfo.persistent = true;
		fontWSMatCreateInfo.bEditorMaterial = true;
		fontWSMatCreateInfo.bDynamic = false;
		fontWSMatCreateInfo.bSerializable = false;
		m_FontMatWSID = InitializeMaterial(&fontWSMatCreateInfo);

		MaterialCreateInfo shadowMatCreateInfo = {};
		shadowMatCreateInfo.name = "shadow";
		shadowMatCreateInfo.shaderName = "shadow";
		shadowMatCreateInfo.persistent = true;
		shadowMatCreateInfo.bEditorMaterial = true;
		shadowMatCreateInfo.bSerializable = false;
		m_ShadowMaterialID = InitializeMaterial(&shadowMatCreateInfo);

		MaterialCreateInfo postProcessMatCreateInfo = {};
		postProcessMatCreateInfo.name = "Post process material";
		postProcessMatCreateInfo.shaderName = "post_process";
		postProcessMatCreateInfo.persistent = true;
		postProcessMatCreateInfo.bEditorMaterial = true;
		postProcessMatCreateInfo.bSerializable = false;
		m_PostProcessMatID = InitializeMaterial(&postProcessMatCreateInfo);

		//MaterialCreateInfo postFXAAMatCreateInfo = {};
		//postFXAAMatCreateInfo.name = "fxaa";
		//postFXAAMatCreateInfo.shaderName = "post_fxaa";
		//postFXAAMatCreateInfo.persistent = true;
		//postFXAAMatCreateInfo.bEditorMaterial = true;
		//postFXAAMatCreateInfo.bSerializable = false;
		//m_PostFXAAMatID = InitializeMaterial(&postFXAAMatCreateInfo);

		MaterialCreateInfo selectedObjectMatCreateInfo = {};
		selectedObjectMatCreateInfo.name = "Selected Object";
		selectedObjectMatCreateInfo.shaderName = "colour";
		selectedObjectMatCreateInfo.persistent = true;
		selectedObjectMatCreateInfo.bEditorMaterial = true;
		selectedObjectMatCreateInfo.colourMultiplier = VEC4_ONE;
		selectedObjectMatCreateInfo.bSerializable = false;
		m_SelectedObjectMatID = InitializeMaterial(&selectedObjectMatCreateInfo);

		MaterialCreateInfo taaMatCreateInfo = {};
		taaMatCreateInfo.name = "TAA Resolve";
		taaMatCreateInfo.shaderName = "taa_resolve";
		taaMatCreateInfo.persistent = true;
		taaMatCreateInfo.bEditorMaterial = true;
		taaMatCreateInfo.colourMultiplier = VEC4_ONE;
		taaMatCreateInfo.bSerializable = false;
		m_TAAResolveMaterialID = InitializeMaterial(&taaMatCreateInfo);

		MaterialCreateInfo gammaCorrectMatCreateInfo = {};
		gammaCorrectMatCreateInfo.name = "Gamma Correct";
		gammaCorrectMatCreateInfo.shaderName = "gamma_correct";
		gammaCorrectMatCreateInfo.persistent = true;
		gammaCorrectMatCreateInfo.bEditorMaterial = true;
		gammaCorrectMatCreateInfo.colourMultiplier = VEC4_ONE;
		gammaCorrectMatCreateInfo.bSerializable = false;
		m_GammaCorrectMaterialID = InitializeMaterial(&gammaCorrectMatCreateInfo);

		MaterialCreateInfo fullscreenBlitMatCreateInfo = {};
		fullscreenBlitMatCreateInfo.name = "fullscreen blit";
		fullscreenBlitMatCreateInfo.shaderName = "blit";
		fullscreenBlitMatCreateInfo.persistent = true;
		fullscreenBlitMatCreateInfo.bEditorMaterial = true;
		fullscreenBlitMatCreateInfo.enableAlbedoSampler = true;
		fullscreenBlitMatCreateInfo.bSerializable = false;
		m_FullscreenBlitMatID = InitializeMaterial(&fullscreenBlitMatCreateInfo);

		MaterialCreateInfo computeSDFMatCreateInfo = {};
		computeSDFMatCreateInfo.name = "compute SDF";
		computeSDFMatCreateInfo.shaderName = "compute_sdf";
		computeSDFMatCreateInfo.persistent = true;
		computeSDFMatCreateInfo.bEditorMaterial = true;
		computeSDFMatCreateInfo.bSerializable = false;
		m_ComputeSDFMatID = InitializeMaterial(&computeSDFMatCreateInfo);

		MaterialCreateInfo irradianceCreateInfo = {};
		irradianceCreateInfo.name = "irradiance";
		irradianceCreateInfo.shaderName = "irradiance";
		irradianceCreateInfo.persistent = true;
		irradianceCreateInfo.bEditorMaterial = true;
		irradianceCreateInfo.bSerializable = false;
		m_IrradianceMaterialID = InitializeMaterial(&irradianceCreateInfo);

		MaterialCreateInfo prefilterCreateInfo = {};
		prefilterCreateInfo.name = "prefilter";
		prefilterCreateInfo.shaderName = "prefilter";
		prefilterCreateInfo.persistent = true;
		prefilterCreateInfo.bEditorMaterial = true;
		prefilterCreateInfo.bSerializable = false;
		m_PrefilterMaterialID = InitializeMaterial(&prefilterCreateInfo);

		MaterialCreateInfo brdfCreateInfo = {};
		brdfCreateInfo.name = "brdf";
		brdfCreateInfo.shaderName = "brdf";
		brdfCreateInfo.persistent = true;
		brdfCreateInfo.bEditorMaterial = true;
		brdfCreateInfo.bSerializable = false;
		m_BRDFMaterialID = InitializeMaterial(&brdfCreateInfo);

		MaterialCreateInfo wireframeCreateInfo = {};
		wireframeCreateInfo.name = "wireframe";
		wireframeCreateInfo.shaderName = "wireframe";
		wireframeCreateInfo.persistent = true;
		wireframeCreateInfo.bEditorMaterial = true;
		wireframeCreateInfo.bSerializable = false;
		m_WireframeMatID = InitializeMaterial(&wireframeCreateInfo);

		MaterialCreateInfo placeholderMatCreateInfo = {};
		placeholderMatCreateInfo.name = "placeholder";
		placeholderMatCreateInfo.shaderName = "pbr";
		placeholderMatCreateInfo.persistent = true;
		placeholderMatCreateInfo.bEditorMaterial = true;
		placeholderMatCreateInfo.constAlbedo = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
		placeholderMatCreateInfo.bSerializable = false;
		m_PlaceholderMaterialID = InitializeMaterial(&placeholderMatCreateInfo);
	}

	void Renderer::AddShaderSpecialziationConstant(ShaderID shaderID, StringID specializationConstant)
	{
		auto iter = m_ShaderSpecializationConstants.find(shaderID);
		if (iter != m_ShaderSpecializationConstants.end())
		{
			// Add to existing list
			iter->second.push_back(specializationConstant);
		}
		else
		{
			// Add first entry
			m_ShaderSpecializationConstants[shaderID] = { specializationConstant };
		}
	}

	void Renderer::RemoveShaderSpecialziationConstant(ShaderID shaderID, StringID specializationConstant)
	{
		auto iter = m_ShaderSpecializationConstants.find(shaderID);
		if (iter != m_ShaderSpecializationConstants.end())
		{
			Erase(iter->second, specializationConstant);
		}
	}

	std::vector<StringID>* Renderer::GetShaderSpecializationConstants(ShaderID shaderID)
	{
		auto iter = m_ShaderSpecializationConstants.find(shaderID);
		if (iter != m_ShaderSpecializationConstants.end())
		{
			return &iter->second;
		}

		return nullptr;
	}

	void Renderer::DrawSpecializationConstantInfoImGui()
	{
		if (ImGui::Button(m_bSpecializationConstantsDirty ? "Save*" : "Save"))
		{
			SerializeShaderSpecializationConstants();
			SerializeSpecializationConstantInfo();
			m_bSpecializationConstantsDirty = false;
		}

		ImGui::SameLine();

		if (ImGui::Button("Reload"))
		{
			ParseSpecializationConstantInfo();
			ParseShaderSpecializationConstants();
			m_bSpecializationConstantsDirty = false;
		}

		bool bOpenNewConstantPopup = false;

		if (ImGui::TreeNode("Specialization constants##list-view"))
		{
			for (auto& pair : m_SpecializationConstants)
			{
				ImGui::PushID((i32)pair.first);

				if (ImGui::Button("-##remove-constant"))
				{
					auto iter = m_SpecializationConstants.find(pair.first);
					m_SpecializationConstants.erase(iter);
					m_bSpecializationConstantsDirty = true;
					ImGui::PopID();
					break;
				}

				ImGui::SameLine();

				const std::string& specializationConstantName = m_SpecializationConstantNames[pair.first];
				ImGui::Text("%s (ID: %u)", specializationConstantName.c_str(), pair.second.id);
				ImGui::SameLine();
				ImGui::PushItemWidth(80.0f);
				if (ImGui::InputInt("", &pair.second.value, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					RecreateEverything();
				}
				ImGui::PopItemWidth();

				ImGui::PopID();
			}

			bOpenNewConstantPopup = ImGui::Button("Add new");

			ImGui::TreePop();
		}
		if (ImGui::TreeNode("Shader specialization constants"))
		{
			for (const auto& shaderSpecializationConstantPair : m_ShaderSpecializationConstants)
			{
				ShaderID shaderID = shaderSpecializationConstantPair.first;
				ImGui::PushID(shaderID);

				Shader* shader = GetShader(shaderID);
				if (ImGui::TreeNode(shader->name.c_str()))
				{
					if (DrawShaderSpecializationConstantImGui(shaderID))
					{
						m_bSpecializationConstantsDirty = true;
					}

					ImGui::TreePop();
				}

				ImGui::PopID();
			}

			ImGui::TreePop();
		}

		const char* newConstantPopupName = "New specialization constant";
		static char nameBuff[256];
		static u32 id = 0;
		static i32 value = 0;
		static i32 min = 0;
		static i32 max = 0;

		if (bOpenNewConstantPopup)
		{
			ImGui::OpenPopup(newConstantPopupName);
			memset(nameBuff, 0, 256);
			id = 0;
			value = 0;
			min = 0;
			max = 0;
		}

		if (ImGui::BeginPopupModal(newConstantPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", nameBuff, 256);

			ImGuiExt::InputUInt("ID", &id);

			ImGui::InputInt("Value", &value);

			ImGui::InputInt("Min", &min);
			ImGui::InputInt("Max", &max);

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (strlen(nameBuff) > 0 && id > 0 && max > min &&
				ImGui::Button("Create"))
			{
				std::string name(nameBuff);
				name = Trim(name);

				StringID specializationConstantSID = Hash(name.c_str());
				m_SpecializationConstants[specializationConstantSID] =
					SpecializationConstantMetaData{ (SpecializationConstantID)id, value, min, max };
				m_SpecializationConstantNames[specializationConstantSID] = name;

				m_bSpecializationConstantsDirty = true;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	bool Renderer::DrawShaderSpecializationConstantImGui(ShaderID shaderID)
	{
		bool bChanged = false;

		std::vector<StringID>* specializationConstants = GetShaderSpecializationConstants(shaderID);
		// TODO: Display ordered by ID
		for (const auto& pair : m_SpecializationConstants)
		{
			const std::string& specializationConstantName = m_SpecializationConstantNames[pair.first];
			bool bTicked = specializationConstants != nullptr && Contains(*specializationConstants, pair.first);
			if (ImGui::Checkbox(specializationConstantName.c_str(), &bTicked))
			{
				if (bTicked)
				{
					AddShaderSpecialziationConstant(shaderID, pair.first);
				}
				else
				{
					RemoveShaderSpecialziationConstant(shaderID, pair.first);
				}

				// NOTE: This is a brute force approach, ideally we'd just reload things which need reloading
				RecreateEverything();

				bChanged = true;
			}
		}

		return bChanged;
	}

	void Renderer::SetSpecializationConstantDirty()
	{
		m_bSpecializationConstantsDirty = true;
	}

	void Renderer::DrawImGuiSettings()
	{
		if (ImGui::TreeNode("Renderer settings"))
		{
			if (ImGui::Button("Save"))
			{
				g_Renderer->SaveSettingsToDisk(true);
			}

			ImGui::SameLine();
			if (ImGui::Button("Reload"))
			{
				g_Renderer->LoadSettingsFromDisk();
			}

			bool bShowGrid = g_Editor->IsShowingGrid();
			if (ImGui::Checkbox("Show grid", &bShowGrid))
			{
				g_Editor->SetShowGrid(bShowGrid);
			}

			if (ImGui::Button("Recapture reflection probe"))
			{
				g_Renderer->RecaptureReflectionProbe();
			}

			ImGui::Checkbox("Selection wireframe", &m_bEnableSelectionWireframe);

			bool bVSyncEnabled = g_Window->GetVSyncEnabled();
			if (ImGui::Checkbox("VSync", &bVSyncEnabled))
			{
				g_Window->SetVSyncEnabled(bVSyncEnabled);
			}

			if (ImGui::TreeNode("Camera exposure"))
			{
				BaseCamera* currentCamera = g_CameraManager->CurrentCamera();

				ImGui::Text("Exposure: %.2f", currentCamera->exposure);

				ImGui::PushItemWidth(140.0f);
				{
					if (ImGui::SliderFloat("Aperture (f-stops)", &currentCamera->aperture, 1.0f, 64.0f))
					{
						currentCamera->CalculateExposure();
					}

					real shutterSpeedInv = 1.0f / currentCamera->shutterSpeed;
					if (ImGui::SliderFloat("Shutter speed (1/s)", &shutterSpeedInv, 1.0f, 500.0f))
					{
						currentCamera->shutterSpeed = 1.0f / shutterSpeedInv;
						currentCamera->CalculateExposure();
					}

					if (ImGui::SliderFloat("ISO", &currentCamera->lightSensitivity, 100.0f, 6400.0f))
					{
						// Round to nearest power of 2 * 100
						currentCamera->lightSensitivity = RoundToNearestPowerOfTwo(currentCamera->lightSensitivity / 100.0f) * 100.0f;
						currentCamera->CalculateExposure();
					}
				}
				ImGui::PopItemWidth();

				ImGui::TreePop();
			}

			if (ImGui::SliderInt("Shadow cascade count", &m_ShadowCascadeCount, 1, 4))
			{
				m_ShadowCascadeCount = glm::clamp(m_ShadowCascadeCount, 1, 4);
				// TODO: Recreate less
				RecreateEverything();
			}

			if (ImGuiExt::SliderUInt("Shadow cascade base resolution", &m_ShadowMapBaseResolution, 128u, 4096u))
			{
				m_ShadowMapBaseResolution = NextPowerOfTwo(glm::clamp(m_ShadowMapBaseResolution, 128u, 4096u));
				// TODO: Recreate less
				RecreateEverything();
			}

			ImGui::SliderFloat("Shadow bias", &m_ShadowSamplingData.baseBias, 0.0f, 0.02f);

			SpecializationConstantMetaData& specializationConstantMetaData = m_SpecializationConstants[SID("shader_quality_level")];
			if (ImGui::SliderInt("Shader quality level", &specializationConstantMetaData.value, 0, MAX_SHADER_QUALITY_LEVEL))
			{
				i32 newShaderQualityLevel = glm::clamp(specializationConstantMetaData.value, 0, MAX_SHADER_QUALITY_LEVEL);
				if (newShaderQualityLevel != specializationConstantMetaData.value)
				{
					RecreateEverything();
				}
			}

			SpecializationConstantMetaData& debugOverlayConstant = m_SpecializationConstants[SID("debug_overlay_index")];
			const char* previewValue = "";
			if (debugOverlayConstant.value >= 0 && debugOverlayConstant.value < (i32)g_ResourceManager->debugOverlayNames.size())
			{
				previewValue = g_ResourceManager->debugOverlayNames[debugOverlayConstant.value].c_str();
			}
			if (ImGui::BeginCombo("Debug overlay", previewValue))
			{
				for (i32 i = 0; i < (i32)g_ResourceManager->debugOverlayNames.size(); ++i)
				{
					bool bSelected = (i == debugOverlayConstant.value);
					if (ImGui::Selectable(g_ResourceManager->debugOverlayNames[i].c_str(), &bSelected))
					{
						if (debugOverlayConstant.value != i)
						{
							debugOverlayConstant.value = i;
							RecreateEverything();
						}
					}
				}

				ImGui::EndCombo();
			}

			SpecializationConstantMetaData& enableFogConstant = m_SpecializationConstants[SID("enable_fog")];
			bool bFogEnabled = enableFogConstant.value != 0;
			if (ImGui::Checkbox("Enable fog", &bFogEnabled))
			{
				enableFogConstant.value = bFogEnabled ? 1 : 0;
				RecreateEverything();
			}

			if (ImGui::TreeNode("Debug objects"))
			{
				PhysicsDebuggingSettings& physicsDebuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();

				ImGui::Checkbox("Wireframe overlay", &m_bEnableWireframeOverlay);

				bool bRenderEditorObjs = g_EngineInstance->IsRenderingEditorObjects();
				if (ImGui::Checkbox("Editor objects", &bRenderEditorObjs))
				{
					g_EngineInstance->SetRenderingEditorObjects(bRenderEditorObjs);
				}

				ImGui::Spacing();
				ImGui::Spacing();
				ImGui::Spacing();

				ImGui::Checkbox("Disable All", &physicsDebuggingSettings.bDisableAll);

				if (physicsDebuggingSettings.bDisableAll)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				}

				bool bDisplayBoundingVolumes = g_Renderer->IsDisplayBoundingVolumesEnabled();
				if (ImGui::Checkbox("Bounding volumes", &bDisplayBoundingVolumes))
				{
					g_Renderer->SetDisplayBoundingVolumesEnabled(bDisplayBoundingVolumes);
				}

				ImGui::Checkbox("Wireframe (P)", &physicsDebuggingSettings.bDrawWireframe);

				ImGui::Checkbox("AABB", &physicsDebuggingSettings.bDrawAabb);

				if (physicsDebuggingSettings.bDisableAll)
				{
					ImGui::PopStyleColor();
				}

				ImGui::TreePop();
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Post processing"))
		{
			if (ImGui::Checkbox("TAA", &m_bEnableTAA))
			{
				m_bTAAStateChanged = true;
			}

			ImGui::PushItemWidth(150.0f);
			if (ImGui::SliderInt("Sample Count", &m_TAASampleCount, 1, 16))
			{
				m_bTAAStateChanged = true;
				m_TAASampleCount = (i32)RoundToNearestPowerOfTwo((real)m_TAASampleCount);
			}

			real maxBrightness = 2.5f;
			ImGui::SliderFloat3("Brightness", &m_PostProcessSettings.brightness.r, 0.0f, maxBrightness);
			ImGui::SameLine();
			ImGui::ColorButton("##1", ImVec4(
				m_PostProcessSettings.brightness.r / maxBrightness,
				m_PostProcessSettings.brightness.g / maxBrightness,
				m_PostProcessSettings.brightness.b / maxBrightness, 1));

			real minOffset = -0.065f;
			real maxOffset = 0.065f;
			ImGui::SliderFloat3("Offset", &m_PostProcessSettings.offset.r, minOffset, maxOffset);
			ImGui::SameLine();
			ImGui::ColorButton("##2", ImVec4(
				(m_PostProcessSettings.offset.r - minOffset) / (maxOffset - minOffset),
				(m_PostProcessSettings.offset.g - minOffset) / (maxOffset - minOffset),
				(m_PostProcessSettings.offset.b - minOffset) / (maxOffset - minOffset), 1));

			const real maxSaturation = 1.5f;
			ImGui::SliderFloat("Saturation", &m_PostProcessSettings.saturation, 0.0f, maxSaturation);
			ImGui::SameLine();
			ImGui::ColorButton("##3", ImVec4(
				m_PostProcessSettings.saturation / maxSaturation,
				m_PostProcessSettings.saturation / maxSaturation,
				m_PostProcessSettings.saturation / maxSaturation, 1));

			bool bSSAOEnabled = m_SSAOSamplingData.enabled != 0;
			if (ImGui::Checkbox("SSAO", &bSSAOEnabled))
			{
				m_SSAOSamplingData.enabled = bSSAOEnabled ? 1 : 0;
				if (m_bSSAOBlurEnabled != bSSAOEnabled)
				{
					m_bSSAOBlurEnabled = bSSAOEnabled;
					m_bSSAOStateChanged = true;
				}
			}

			ImGui::SameLine();

			if (ImGui::Checkbox("Blur", &m_bSSAOBlurEnabled))
			{
				m_bSSAOStateChanged = true;
				if (m_bSSAOBlurEnabled)
				{
					m_SSAOSamplingData.enabled = 1;
				}
			}

			SpecializationConstantMetaData& ssaoKernelSizeConstant = m_SpecializationConstants[SID("ssao_kernel_size")];
			if (ImGui::SliderInt("Kernel Size", &ssaoKernelSizeConstant.value, 1, 64))
			{
				m_bSSAOStateChanged = true;
			}
			ImGui::SliderFloat("Radius", &m_SSAOGenData.radius, 0.0001f, 15.0f);
			ImGui::SliderInt("Blur Radius", &m_SSAOBlurDataConstant.radius, 1, 16);
			ImGui::SliderInt("Blur Offset Count", &m_SSAOBlurSamplePixelOffset, 1, 10);
			ImGui::SliderFloat("Pow", &m_SSAOSamplingData.powExp, 0.1f, 10.0f);

			ImGui::PopItemWidth();

			ImGui::TreePop();
		}
	}

	real Renderer::GetStringWidth(const std::string& str, BitmapFont* font, real letterSpacing, bool bNormalized) const
	{
		real strWidth = 0;

		char prevChar = ' ';
		for (char c : str)
		{
			if (BitmapFont::IsCharValid(c))
			{
				FontMetric* metric = font->GetMetric(c);

				if (font->bUseKerning)
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

	real Renderer::GetStringHeight(const std::string& str, BitmapFont* font, bool bNormalized) const
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

	real Renderer::GetStringWidth(const TextCache& textCache, BitmapFont* font) const
	{
		real strWidth = 0;

		char prevChar = ' ';
		for (char c : textCache.str)
		{
			if (BitmapFont::IsCharValid(c))
			{
				FontMetric* metric = font->GetMetric(c);

				if (font->bUseKerning)
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

	real Renderer::GetStringHeight(const TextCache& textCache, BitmapFont* font) const
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

	// TODO: Consolidate with UpdateTextBufferWS
	u32 Renderer::UpdateTextBufferSS(std::vector<TextVertex2D>& outTextVertices)
	{
		PROFILE_AUTO("Update Text Buffer SS");

		glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;

		u32 charCountUpperBound = 0;
		for (BitmapFont* font : g_ResourceManager->fontsScreenSpace)
		{
			const std::vector<TextCache>& caches = font->GetTextCaches();
			for (const TextCache& textCache : caches)
			{
				charCountUpperBound += (u32)textCache.str.length();
			}
		}
		outTextVertices.resize(charCountUpperBound);

		const real frameBufferScale = glm::max(2.0f / (real)frameBufferSize.x, 2.0f / (real)frameBufferSize.y);

		u32 charIndex = 0;
		for (BitmapFont* font : g_ResourceManager->fontsScreenSpace)
		{
			real baseTextScale = frameBufferScale * (font->metaData.size / 12.0f);

			font->bufferStart = (i32)charIndex;

			const std::vector<TextCache>& textCaches = font->GetTextCaches();
			for (const TextCache& textCache : textCaches)
			{
				real textScale = baseTextScale * textCache.scale;
				std::string currentStr = textCache.str;

				real totalAdvanceX = 0;

				glm::vec2 basePos(0.0f);

				real strWidth = GetStringWidth(textCache, font) * textScale;
				real strHeight = GetStringHeight(textCache, font) * textScale;

				switch (textCache.anchor)
				{
				case AnchorPoint::TOP_LEFT:
					basePos = glm::vec3(-aspectRatio, 1.0f - strHeight / 2.0f, 0.0f);
					break;
				case AnchorPoint::TOP:
					basePos = glm::vec3(-strWidth / 2.0f, 1.0f - strHeight / 2.0f, 0.0f);
					break;
				case AnchorPoint::TOP_RIGHT:
					basePos = glm::vec3(aspectRatio - strWidth, 1.0f - strHeight / 2.0f, 0.0f);
					break;
				case AnchorPoint::RIGHT:
					basePos = glm::vec3(aspectRatio - strWidth, 0.0f, 0.0f);
					break;
				case AnchorPoint::BOTTOM_RIGHT:
					basePos = glm::vec3(aspectRatio - strWidth, -1.0f + strHeight / 2.0f, 0.0f);
					break;
				case AnchorPoint::BOTTOM:
					basePos = glm::vec3(-strWidth / 2.0f, -1.0f + strHeight / 2.0f, 0.0f);
					break;
				case AnchorPoint::BOTTOM_LEFT:
					basePos = glm::vec3(-aspectRatio, -1.0f + strHeight / 2.0f, 0.0f);
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

				char prevChar = ' ';
				for (char c : currentStr)
				{
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

							glm::vec2 pos =
								glm::vec2((textCache.pos.x) * aspectRatio, textCache.pos.y) +
								glm::vec2(totalAdvanceX + metric->offsetX, -metric->offsetY) * textScale;

							if (font->bUseKerning)
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

							TextVertex2D vert = {};
							vert.pos = glm::vec2(basePos + pos);
							vert.uv = metric->texCoord;
							vert.colour = textCache.colour;
							vert.charSizePixelsCharSizeNorm = charSizePixelsCharSizeNorm;
							vert.channel = texChannel;

							outTextVertices[charIndex++] = vert;

							totalAdvanceX += metric->advanceX + textCache.xSpacing;
						}
						else
						{
							PrintWarn("Attempted to draw char with invalid metric: %c in font %s\n", c, font->name.c_str());
						}
					}
					else
					{
						PrintWarn("Attempted to draw invalid char: %c in font %s\n", c, font->name.c_str());
					}

					prevChar = c;
				}
			}

			font->bufferSize = (i32)charIndex - font->bufferStart;
			font->ClearCaches();
		}

		return charIndex;
	}

	u32 Renderer::UpdateTextBufferWS(std::vector<TextVertex3D>& outTextVertices)
	{
		// TODO: Consolidate with UpdateTextBufferSS

		PROFILE_AUTO("Update Text Buffer WS");

		const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		const real frameBufferScale = glm::max(1.0f / (real)frameBufferSize.x, 1.0f / (real)frameBufferSize.y);

		u32 charCountUpperBound = 0;
		for (BitmapFont* font : g_ResourceManager->fontsWorldSpace)
		{
			const std::vector<TextCache>& caches = font->GetTextCaches();
			for (const TextCache& textCache : caches)
			{
				charCountUpperBound += (u32)textCache.str.length();
			}
		}
		outTextVertices.resize(charCountUpperBound);

		u32 charIndex = 0;
		for (BitmapFont* font : g_ResourceManager->fontsWorldSpace)
		{
			real textScale = frameBufferScale * font->metaData.size;

			font->bufferStart = (i32)charIndex;

			const std::vector<TextCache>& caches = font->GetTextCaches();
			for (const TextCache& textCache : caches)
			{
				const glm::vec3 tangent = -glm::rotate(textCache.rot, VEC3_RIGHT);

				real totalAdvanceX = 0;

				char prevChar = ' ';
				for (char c : textCache.str)
				{
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

							glm::vec3 pos = textCache.pos +
								tangent * (totalAdvanceX + metric->offsetX) * textScale * textCache.scale +
								VEC3_UP * (real)(-metric->offsetY) * textScale * textCache.scale;

							if (font->bUseKerning)
							{
								std::wstring charKey(std::wstring(1, prevChar) + std::wstring(1, c));

								auto iter = metric->kerning.find(charKey);
								if (iter != metric->kerning.end())
								{
									pos += glm::vec3(iter->second, 0.0f) * textScale * textCache.scale;
								}
							}

							glm::vec4 charSizePixelsCharSizeNorm(
								metric->width, metric->height,
								metric->width * textScale * textCache.scale, metric->height * textScale * textCache.scale);

							i32 texChannel = (i32)metric->channel;

							TextVertex3D vert = {};
							vert.pos = pos;
							vert.uv = metric->texCoord;
							vert.colour = textCache.colour;
							vert.tangent = tangent;
							vert.charSizePixelsCharSizeNorm = charSizePixelsCharSizeNorm;
							vert.channel = texChannel;

							outTextVertices[charIndex++] = vert;

							totalAdvanceX += metric->advanceX + textCache.xSpacing;
						}
						else
						{
							PrintWarn("Attempted to draw char with invalid metric: %c in font %s\n", c, font->name.c_str());
						}
					}
					else
					{
						PrintWarn("Attempted to draw invalid char: %c in font %s\n", c, font->name.c_str());
					}

					prevChar = c;
				}
			}

			font->bufferSize = (i32)charIndex - font->bufferStart;
			font->ClearCaches();
		}

		return charIndex;
	}

	glm::vec4 Renderer::GetSelectedObjectColourMultiplier() const
	{
		static const glm::vec4 colour0 = { 0.95f, 0.95f, 0.95f, 0.4f };
		static const glm::vec4 colour1 = { 0.85f, 0.15f, 0.85f, 0.4f };
		static const real pulseSpeed = 8.0f;
		return Lerp(colour0, colour1, sin(g_SecElapsedSinceProgramStart * pulseSpeed) * 0.5f + 0.5f);
	}

	glm::mat4 Renderer::GetPostProcessingMatrix() const
	{
		// TODO: OPTIMIZATION: Cache result

		glm::mat4 contrastBrightnessSaturation;
		real sat = m_PostProcessSettings.saturation;
		glm::vec3 brightness = m_PostProcessSettings.brightness;
		glm::vec3 offset = m_PostProcessSettings.offset;

		static const glm::vec3 wgt(0.3086f, 0.6094f, 0.0820f);
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
		return contrastBrightnessSaturation;
	}

	void Renderer::GenerateSSAONoise(std::vector<glm::vec4>& noise)
	{
		noise = std::vector<glm::vec4>(SSAO_NOISE_DIM * SSAO_NOISE_DIM);
		for (glm::vec4& noiseSample : noise)
		{
			// Random rotations around z-axis
			noiseSample = glm::vec4(RandomFloat(-1.0f, 1.0f), RandomFloat(-1.0f, 1.0f), 0.0f, 0.0f);
		}
	}

	MaterialID Renderer::CreateParticleSystemSimulationMaterial(const std::string& name)
	{
		MaterialCreateInfo createInfo = {};
		createInfo.name = name;
		createInfo.shaderName = "particle_sim";
		createInfo.persistent = true;
		createInfo.bEditorMaterial = true;
		createInfo.bSerializable = false;
		return InitializeMaterial(&createInfo);
	}

	MaterialID Renderer::CreateParticleSystemRenderingMaterial(const std::string& name)
	{
		MaterialCreateInfo createInfo = {};
		createInfo.name = name;
		createInfo.shaderName = "particles";
		createInfo.persistent = true;
		createInfo.bEditorMaterial = true;
		createInfo.bSerializable = false;
		return InitializeMaterial(&createInfo);
	}

	void Renderer::ParseSpecializationConstantInfo()
	{
		PROFILE_AUTO("ParseSpecializationConstantInfo");

		JSONObject rootObj;
		std::string fileContents;
		if (!ReadFile(SPECIALIZATION_CONSTANTS_LOCATION, fileContents, false))
		{
			PrintError("Failed to read specialization constants!\n");
			return;
		}

		if (!JSONParser::Parse(fileContents, rootObj))
		{
			const char* errorStr = JSONParser::GetErrorString();
			PrintError("Failed to parse specialization constants file! Error: %s\n", errorStr);
			return;
		}

		m_SpecializationConstants.clear();

		for (const JSONField& field : rootObj.fields)
		{
			u64 specializationConstantSID = Hash(field.label.c_str());
			SpecializationConstantID id = field.value.objectValue.GetUInt("id");
			i32 defaultValue = field.value.objectValue.GetInt("default_value");
			std::vector<JSONField> range;
			i32 min = -1;
			i32 max = -1;
			if (field.value.objectValue.TryGetFieldArray("range", range) && range.size() == 2)
			{
				min = range[0].value.AsInt();
				max = range[1].value.AsInt();
			}

			m_SpecializationConstants[specializationConstantSID] = SpecializationConstantMetaData{ id, defaultValue, min, max };

			m_SpecializationConstantNames[specializationConstantSID] = field.label;
		}
	}

	void Renderer::SerializeSpecializationConstantInfo()
	{
		JSONObject rootObj = {};

		rootObj.fields.reserve(m_SpecializationConstants.size());
		for (const auto& pair : m_SpecializationConstants)
		{
			std::string specializationConstantName = m_SpecializationConstantNames[pair.first];
			JSONObject specializationConstantObj = {};
			specializationConstantObj.fields.emplace_back("id", JSONValue(pair.second.id));
			specializationConstantObj.fields.emplace_back("default_value", JSONValue(pair.second.value));
			std::vector<JSONField> rangeFields = { JSONField("", JSONValue(pair.second.min)), JSONField("", JSONValue(pair.second.max)) };
			specializationConstantObj.fields.emplace_back("range", JSONValue(rangeFields));
			rootObj.fields.emplace_back(specializationConstantName, JSONValue(specializationConstantObj));
		}

		std::string fileContents = rootObj.ToString();
		if (!WriteFile(SPECIALIZATION_CONSTANTS_LOCATION, fileContents, false))
		{
			PrintError("Failed to write shader specialization constants\n");
		}
	}

	void Renderer::ParseShaderSpecializationConstants()
	{
		PROFILE_AUTO("ParseShaderSpecializationConstants");

		JSONObject rootObj;
		std::string fileContents;
		if (!ReadFile(SHADER_SPECIALIZATION_CONSTANTS_LOCATION, fileContents, false))
		{
			PrintError("Failed to read shader specialization constants!\n");
			return;
		}

		if (!JSONParser::Parse(fileContents, rootObj))
		{
			const char* errorStr = JSONParser::GetErrorString();
			PrintError("Failed to parse shader specialization constants file! Error: %s\n", errorStr);
			return;
		}

		m_ShaderSpecializationConstants.clear();

		for (const JSONField& field : rootObj.fields)
		{
			const std::string& shaderName = field.label;
			ShaderID shaderID = InvalidShaderID;
			if (GetShaderID(shaderName, shaderID))
			{
				std::vector<StringID>& shaderConstants = m_ShaderSpecializationConstants[shaderID];
				std::vector<JSONField> fields = field.value.fieldArrayValue;
				shaderConstants.reserve(fields.size());
				for (const JSONField& specializationConstantNameField : fields)
				{
					if (specializationConstantNameField.value.strValue.empty())
					{
						PrintWarn("Found empty specialization constant field for shader: %s\n", shaderName.c_str());
						continue;
					}
					StringID specializationConstantID = Hash(specializationConstantNameField.value.strValue.c_str());
					shaderConstants.push_back(specializationConstantID);
				}
			}
		}
	}

	void Renderer::SerializeShaderSpecializationConstants()
	{
		JSONObject rootObj = {};

		rootObj.fields.reserve(m_ShaderSpecializationConstants.size());
		for (const auto& pair : m_ShaderSpecializationConstants)
		{
			Shader* shader = GetShader(pair.first);
			std::vector<JSONField> specializationConstantFields;
			specializationConstantFields.reserve(pair.second.size());
			for (const auto& pair2 : pair.second)
			{
				std::string specializationConstantName = m_SpecializationConstantNames[pair2];
				specializationConstantFields.emplace_back(JSONField("", JSONValue(specializationConstantName)));
			}
			rootObj.fields.emplace_back(shader->name, JSONValue(specializationConstantFields));
		}

		std::string fileContents = rootObj.ToString();
		if (!WriteFile(SHADER_SPECIALIZATION_CONSTANTS_LOCATION, fileContents, false))
		{
			PrintError("Failed to write shader specialization constants\n");
		}
	}

	void PhysicsDebugDrawBase::flushLines()
	{
		Draw();
	}

	void PhysicsDebugDrawBase::DrawAxes(const btVector3& origin, const btQuaternion& orientation, real scale)
	{
		drawLine(origin, origin + quatRotate(orientation, btVector3(scale, 0.0f, 0.0f)), btVector3(0.9f, 0.1f, 0.1f));
		drawLine(origin, origin + quatRotate(orientation, btVector3(0.0f, scale, 0.0f)), btVector3(0.1f, 0.9f, 0.1f));
		drawLine(origin, origin + quatRotate(orientation, btVector3(0.0f, 0.0f, scale)), btVector3(0.1f, 0.1f, 0.9f));
	}

	void PhysicsDebugDrawBase::UpdateDebugMode()
	{
		const PhysicsDebuggingSettings& settings = g_Renderer->GetPhysicsDebuggingSettings();

		m_DebugMode =
			(settings.bDisableAll ? DBG_NoDebug : 0) |
			(settings.bDrawWireframe ? DBG_DrawWireframe : 0) |
			(settings.bDrawAabb ? DBG_DrawAabb : 0) |
			(settings.bDrawFeaturesText ? DBG_DrawFeaturesText : 0) |
			(settings.bDrawContactPoints ? DBG_DrawContactPoints : 0) |
			(settings.bNoDeactivation ? DBG_NoDeactivation : 0) |
			(settings.bNoHelpText ? DBG_NoHelpText : 0) |
			(settings.bDrawText ? DBG_DrawText : 0) |
			(settings.bProfileTimings ? DBG_ProfileTimings : 0) |
			(settings.bEnableSatComparison ? DBG_EnableSatComparison : 0) |
			(settings.bDisableBulletLCP ? DBG_DisableBulletLCP : 0) |
			(settings.bEnableCCD ? DBG_EnableCCD : 0) |
			(settings.bDrawConstraints ? DBG_DrawConstraints : 0) |
			(settings.bDrawConstraintLimits ? DBG_DrawConstraintLimits : 0) |
			(settings.bFastWireframe ? DBG_FastWireframe : 0) |
			(settings.bDrawNormals ? DBG_DrawNormals : 0) |
			(settings.bDrawFrames ? DBG_DrawFrames : 0);
	}

	void PhysicsDebugDrawBase::ClearLines()
	{
		m_LineSegmentIndex = 0;
	}

	bool Renderer::DrawImGuiShadersDropdown(i32* selectedShaderID, Shader** outSelectedShader /* = nullptr */)
	{
		bool bValueChanged = false;

		ImGui::PushItemWidth(240.0f);
		if (ImGui::BeginCombo("Shaders", m_Shaders[*selectedShaderID]->name.c_str()))
		{
			for (i32 i = 0; i < (i32)m_Shaders.size(); ++i)
			{
				ImGui::PushID(i);

				bool bSelected = (i == *selectedShaderID);
				if (ImGui::Selectable(m_Shaders[i]->name.c_str(), &bSelected))
				{
					*selectedShaderID = i;
					bValueChanged = true;
				}

				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		if (outSelectedShader != nullptr)
		{
			*outSelectedShader = m_Shaders[*selectedShaderID];
		}

		return bValueChanged;
	}

	bool Renderer::DrawImGuiTextureSelector(const char* label, const std::vector<std::string>& textureFilePaths, i32* selectedIndex)
	{
		bool bValueChanged = false;

		std::string currentTexName = (*selectedIndex == 0 ? "NONE" : StripLeadingDirectories(textureFilePaths[*selectedIndex]));
		if (ImGui::BeginCombo(label, currentTexName.c_str()))
		{
			for (i32 i = 0; i < (i32)textureFilePaths.size(); i++)
			{
				bool bTextureSelected = (*selectedIndex == i);

				if (i == 0)
				{
					if (ImGui::Selectable("NONE", bTextureSelected))
					{
						*selectedIndex = 0;
						bValueChanged = true;
					}
				}
				else
				{
					std::string texName = StripLeadingDirectories(textureFilePaths[i]);

					if (ImGui::Selectable(texName.c_str(), bTextureSelected))
					{
						*selectedIndex = i;
						bValueChanged = true;
					}

					if (ImGui::IsItemHovered())
					{
						Texture* texture = g_ResourceManager->FindLoadedTextureWithName(texName);
						if (texture != nullptr)
						{
							DrawImGuiTexturePreviewTooltip(texture);
						}
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

	bool Renderer::DrawImGuiShadersList(i32* selectedShaderID, bool bShowFilter, Shader** outSelectedShader /* = nullptr */)
	{
		bool bValueChanged = false;

		static ImGuiTextFilter shaderFilter;
		if (bShowFilter)
		{
			shaderFilter.Draw("##shader-filter");

			ImGui::SameLine();
			if (ImGui::Button("x"))
			{
				shaderFilter.Clear();
			}
		}

		if (ImGui::BeginChild("Shader list", ImVec2(0, 120), true))
		{
			// TODO: Add option to generate & view shader dissasembly here

			for (i32 i = 0; i < (i32)m_Shaders.size(); ++i)
			{
				Shader* shader = m_Shaders[i];
				if (!bShowFilter || shaderFilter.PassFilter(shader->name.c_str()))
				{
					bool bSelected = (i == *selectedShaderID);
					if (ImGui::Selectable(shader->name.c_str(), &bSelected))
					{
						if (bSelected)
						{
							*selectedShaderID = i;
							bValueChanged = true;
						}
					}
				}
			}
		}
		ImGui::EndChild(); // Shader list

		if (outSelectedShader != nullptr)
		{
			*outSelectedShader = m_Shaders[*selectedShaderID];
		}

		return bValueChanged;
	}

	void Renderer::DrawImGuiShaderErrors()
	{
#if COMPILE_SHADER_COMPILER
		ShaderCompiler::DrawImGuiShaderErrors();
#endif
	}
	void Renderer::ClearShaderHash(const std::string& shaderName)
	{
#if COMPILE_SHADER_COMPILER
		ShaderCompiler::ClearShaderHash(shaderName);
#else
		FLEX_UNUSED(shaderName);
#endif
	}
	void Renderer::RecompileShaders(bool bForceCompileAll)
	{
#if COMPILE_SHADER_COMPILER
		m_ShaderCompiler->StartCompilation(bForceCompileAll);
#else
		FLEX_UNUSED(bForceCompileAll);
#endif
	}
} // namespace flex
