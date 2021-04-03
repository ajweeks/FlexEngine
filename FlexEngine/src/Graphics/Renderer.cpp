#include "stdafx.hpp"

#include "Graphics/Renderer.hpp"

IGNORE_WARNINGS_PUSH
#if COMPILE_IMGUI
#include "imgui.h"
#endif

#include <glm/gtx/transform.hpp> // for scale
#include <glm/gtx/quaternion.hpp> // for rotate
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Editor.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Physics/RigidBody.hpp"
#include "Profiler.hpp"
#include "Platform/Platform.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Monitor.hpp"
#include "Window/Window.hpp"

namespace flex
{
	std::array<glm::mat4, 6> Renderer::s_CaptureViews;

	Renderer::Renderer() :
		m_RendererSettingsFilePathAbs(RelativePathToAbsolute(RENDERER_SETTINGS_LOCATION))
	{
	}

	Renderer::~Renderer()
	{
	}

	void Renderer::Initialize()
	{
		std::string hdriPath = TEXTURE_DIRECTORY "hdri/";
		if (!Platform::FindFilesInDirectory(hdriPath, m_AvailableHDRIs, "hdr"))
		{
			PrintWarn("Unable to find hdri directory at %s\n", hdriPath.c_str());
		}

		m_LightData = (u8*)malloc(MAX_POINT_LIGHT_COUNT * sizeof(PointLightData) + MAX_SPOT_LIGHT_COUNT * sizeof(SpotLightData));
		m_PointLightData = (PointLightData*)m_LightData;
		m_SpotLightData = (SpotLightData*)(m_LightData + MAX_POINT_LIGHT_COUNT * sizeof(PointLightData));
		assert(m_LightData != nullptr);
		for (i32 i = 0; i < MAX_POINT_LIGHT_COUNT; ++i)
		{
			m_PointLightData[i].colour = VEC3_NEG_ONE;
			m_PointLightData[i].enabled = 0;
		}
		for (i32 i = 0; i < MAX_SPOT_LIGHT_COUNT; ++i)
		{
			m_SpotLightData[i].colour = VEC3_NEG_ONE;
			m_SpotLightData[i].enabled = 0;
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
	}

	void Renderer::PostInitialize()
	{
		// TODO: Use MeshComponent for these objects?

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

			m_FullScreenTriVertexBufferData.DescribeShaderVariables(this, m_FullScreenTriRenderID);
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

			m_Quad3DVertexBufferData.DescribeShaderVariables(this, m_Quad3DRenderID);

			quad3DCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
			quad3DCreateInfo.materialID = m_SpriteMatSSID;
			quad3DCreateInfo.renderPassOverride = RenderPassType::UI;
			m_Quad3DSSRenderID = InitializeRenderObject(&quad3DCreateInfo);
		}
	}

	void Renderer::DestroyPersistentObjects()
	{
		for (GameObject* obj : m_PersistentObjects)
		{
			obj->Destroy();
			delete obj;
		}
		m_PersistentObjects.clear();
	}

	void Renderer::Destroy()
	{
		free(m_LightData);
		m_LightData = nullptr;
		m_PointLightData = nullptr;
		m_SpotLightData = nullptr;

		delete m_ShaderDirectoryWatcher;

		m_Quad3DVertexBufferData.Destroy();
		m_FullScreenTriVertexBufferData.Destroy();

		DestroyRenderObject(m_FullScreenTriRenderID);
		DestroyRenderObject(m_Quad3DRenderID);
		DestroyRenderObject(m_Quad3DSSRenderID);
		DestroyRenderObject(m_GBufferQuadRenderID);

		if (m_PhysicsDebugDrawer)
		{
			m_PhysicsDebugDrawer->Destroy();
			delete m_PhysicsDebugDrawer;
			m_PhysicsDebugDrawer = nullptr;
		}
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
		if (FileExists(m_RendererSettingsFilePathAbs))
		{
			Platform::DeleteFile(m_RendererSettingsFilePathAbs);
		}

		JSONObject rootObject = {};
		rootObject.fields.emplace_back("version", JSONValue(m_RendererSettingsFileVersion));
		rootObject.fields.emplace_back("enable v-sync", JSONValue(m_bVSyncEnabled));
		rootObject.fields.emplace_back("enable fxaa", JSONValue(m_PostProcessSettings.bEnableFXAA));
		rootObject.fields.emplace_back("brightness", JSONValue(VecToString(m_PostProcessSettings.brightness, 3)));
		rootObject.fields.emplace_back("offset", JSONValue(VecToString(m_PostProcessSettings.offset, 3)));
		rootObject.fields.emplace_back("saturation", JSONValue(m_PostProcessSettings.saturation));

		rootObject.fields.emplace_back("shadow cascade count", JSONValue(m_ShadowCascadeCount));
		rootObject.fields.emplace_back("shadow cascade base resolution", JSONValue(m_ShadowMapBaseResolution));

		BaseCamera* cam = g_CameraManager->CurrentCamera();
		rootObject.fields.emplace_back("aperture", JSONValue(cam->aperture));
		rootObject.fields.emplace_back("shutter speed", JSONValue(cam->shutterSpeed));
		rootObject.fields.emplace_back("light sensitivity", JSONValue(cam->lightSensitivity));
		std::string fileContents = rootObject.Print(0);

		if (WriteFile(m_RendererSettingsFilePathAbs, fileContents, false))
		{
			if (bAddEditorStr)
			{
				AddEditorString("Saved renderer settings");
			}
		}
		else
		{
			PrintError("Failed to write render settings to %s\n", m_RendererSettingsFilePathAbs.c_str());
		}
	}

	void Renderer::LoadSettingsFromDisk()
	{
		JSONObject rootObject;
		if (JSONParser::ParseFromFile(m_RendererSettingsFilePathAbs, rootObject))
		{
			if (rootObject.HasField("version"))
			{
				m_RendererSettingsFileVersion = rootObject.GetInt("version");
			}

			SetVSyncEnabled(rootObject.GetBool("enable v-sync"));
			m_PostProcessSettings.bEnableFXAA = rootObject.GetBool("enable fxaa");
			m_PostProcessSettings.brightness = ParseVec3(rootObject.GetString("brightness"));
			m_PostProcessSettings.offset = ParseVec3(rootObject.GetString("offset"));
			m_PostProcessSettings.saturation = rootObject.GetFloat("saturation");

			rootObject.SetIntChecked("shadow cascade count", m_ShadowCascadeCount);
			rootObject.SetUIntChecked("shadow cascade base resolution", m_ShadowMapBaseResolution);

			// Done loading
			m_RendererSettingsFileVersion = LATEST_RENDERER_SETTINGS_FILE_VERSION;
		}
		else
		{
			PrintError("Failed to parse renderer settings file %s\n\terror: %s\n", m_RendererSettingsFilePathAbs.c_str(), JSONParser::GetErrorString());
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
				m_QueuedSSSprites.push_back(drawInfo);
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
			assert(newPointLightID != InvalidPointLightID);

			memcpy(m_PointLightData + newPointLightID, pointLightData, sizeof(PointLightData));
			m_NumPointLightsEnabled++;
			return newPointLightID;
		}
		return InvalidPointLightID;
	}

	void Renderer::UpdatePointLightData(SpotLightID ID, PointLightData* data)
	{
		assert(ID < MAX_POINT_LIGHT_COUNT);
		assert(data != nullptr);

		memcpy(m_PointLightData + ID, data, sizeof(PointLightData));
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
			assert(newSpotLightID != InvalidSpotLightID);

			memcpy(m_SpotLightData + newSpotLightID, spotLightData, sizeof(SpotLightData));
			m_NumSpotLightsEnabled++;
			return newSpotLightID;
		}
		return InvalidSpotLightID;
	}

	void Renderer::UpdateSpotLightData(SpotLightID ID, SpotLightData* data)
	{
		assert(ID < MAX_SPOT_LIGHT_COUNT);
		assert(data != nullptr);

		memcpy(m_SpotLightData + ID, data, sizeof(SpotLightData));
	}

	void Renderer::RemoveDirectionalLight()
	{
		m_DirectionalLight = nullptr;
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
				assert(m_NumPointLightsEnabled >= 0);
			}
		}
	}

	void Renderer::RemoveAllPointLights()
	{
		for (i32 i = 0; i < MAX_POINT_LIGHT_COUNT; ++i)
		{
			m_PointLightData[i].colour = VEC4_NEG_ONE;
			m_PointLightData[i].enabled = 0;
		}
		m_NumPointLightsEnabled = 0;
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
				assert(m_NumSpotLightsEnabled >= 0);
			}
		}
	}

	void Renderer::RemoveAllSpotLights()
	{
		for (i32 i = 0; i < MAX_SPOT_LIGHT_COUNT; ++i)
		{
			m_SpotLightData[i].colour = VEC4_NEG_ONE;
			m_SpotLightData[i].enabled = 0;
		}
		m_NumSpotLightsEnabled = 0;
	}

	DirLightData* Renderer::GetDirectionalLight()
	{
		if (m_DirectionalLight)
		{
			return &m_DirectionalLight->data;
		}
		return nullptr;
	}

	i32 Renderer::GetNumPointLights()
	{
		return m_NumPointLightsEnabled;
	}

	i32 Renderer::GetNumSpotLights()
	{
		return m_NumSpotLightsEnabled;
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
		return m_Shaders[shaderID];
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
		for (const auto& matPair : m_Materials)
		{
			if (matPair.second->name.compare(matName) == 0)
			{
				return true;
			}
		}

		return false;
	}

	bool Renderer::FindOrCreateMaterialByName(const std::string& materialName, MaterialID& materialID)
	{
		for (u32 i = 0; i < (u32)m_Materials.size(); ++i)
		{
			auto matIter = m_Materials.find(i);
			if (matIter != m_Materials.end() && matIter->second->name.compare(materialName) == 0)
			{
				materialID = i;
				return true;
			}
		}

		MaterialCreateInfo* matCreateInfo = g_ResourceManager->GetMaterialInfo(materialName);
		if (matCreateInfo != nullptr)
		{
			materialID = InitializeMaterial(matCreateInfo);
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

	std::vector<JSONObject> Renderer::SerializeAllMaterialsToJSON()
	{
		std::vector<JSONObject> result;

		result.reserve(m_Materials.size());
		for (auto& matPair : m_Materials)
		{
			Material* material = matPair.second;
			if (material->bSerializable)
			{
				result.emplace_back(material->Serialize());
			}
		}

		return result;
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
		if (m_EditorStrSecRemaining > 0.0f)
		{
			m_EditorStrSecRemaining -= g_DeltaTime;
			if (m_EditorStrSecRemaining <= 0.0f)
			{
				m_EditorStrSecRemaining = 0.0f;
			}
		}

		if (m_ShaderDirectoryWatcher && m_ShaderDirectoryWatcher->Update())
		{
			RecompileShaders(false);
		}

		glm::vec4 depthSplits(0.04f, 0.15f, 0.4f, 1.0f);

		BaseCamera* cam = g_CameraManager->CurrentCamera();
		DirLightData* dirLight = g_Renderer->GetDirectionalLight();
		if (dirLight)
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

				m_ShadowLightViewMats[c] = glm::lookAt(frustumCenter - (-m_DirectionalLight->data.dir) * -minExtents.z, frustumCenter, VEC3_UP);
				m_ShadowLightProjMats[c] = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z - minExtents.z, 0.0f);

				m_ShadowSamplingData.cascadeViewProjMats[c] = m_ShadowLightProjMats[c] * m_ShadowLightViewMats[c];
				m_ShadowSamplingData.cascadeDepthSplits[c] = depthSplits[c];

				lastSplitDist = depthSplits[c];
			}
		}
	}

	static ImGuiTextFilter materialFilter;

	i32 Renderer::GetShortMaterialIndex(MaterialID materialID, bool bShowEditorMaterials)
	{
		i32 matShortIndex = 0;
		for (i32 i = 0; i < (i32)m_Materials.size(); ++i)
		{
			auto matIter = m_Materials.find(i);
			if (matIter == m_Materials.end() || (!bShowEditorMaterials && !matIter->second->visibleInEditor))
			{
				continue;
			}

			Material* material = matIter->second;

			if (!materialFilter.PassFilter(material->name.c_str()))
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

	bool Renderer::DrawImGuiMaterialList(i32* selectedMaterialIndexShort, MaterialID* selectedMaterialID, bool bShowEditorMaterials, bool bScrollToSelected)
	{
		bool bMaterialSelectionChanged = false;

		materialFilter.Draw("##material-filter");

		ImGui::SameLine();
		if (ImGui::Button("x"))
		{
			materialFilter.Clear();
		}

		if (ImGui::BeginChild("material list", ImVec2(0.0f, 120.0f), true))
		{
			i32 matShortIndex = 0;
			for (i32 i = 0; i < (i32)m_Materials.size(); ++i)
			{
				auto matIter = m_Materials.find(i);
				if (matIter == m_Materials.end() || (!bShowEditorMaterials && !matIter->second->visibleInEditor))
				{
					continue;
				}

				Material* material = matIter->second;

				if (!materialFilter.PassFilter(material->name.c_str()))
				{
					continue;
				}

				ImGui::PushID(i);

				bool bSelected = (matShortIndex == *selectedMaterialIndexShort);
				const bool bWasMatVisibleInEditor = material->visibleInEditor;
				if (!bWasMatVisibleInEditor)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				}

				if (ImGui::Selectable(material->name.c_str(), &bSelected))
				{
					if (*selectedMaterialIndexShort != matShortIndex)
					{
						*selectedMaterialIndexShort = matShortIndex;
						*selectedMaterialID = (MaterialID)i;
						bMaterialSelectionChanged = true;
					}
				}

				if (bScrollToSelected && bSelected)
				{
					ImGui::SetScrollHereY();
				}

				if (!bWasMatVisibleInEditor)
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
					strcpy(newNameBuf, material->name.c_str());
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

	bool Renderer::DrawImGuiForGameObject(GameObject* gameObject)
	{
		Mesh* mesh = gameObject->GetMesh();
		bool bAnyPropertyChanged = false;

		if (mesh != nullptr)
		{
			ImGui::Text("Materials");

			std::vector<MeshComponent*> subMeshes = mesh->GetSubMeshes();

			real windowWidth = ImGui::GetContentRegionAvailWidth();
			real maxWindowHeight = 170.0f;
			real maxItemCount = 5.0f;
			real verticalPad = 8.0f;
			real windowHeight = glm::min(subMeshes.size() * maxWindowHeight / maxItemCount + verticalPad, maxWindowHeight);
			if (ImGui::BeginChild("materials", ImVec2(windowWidth - 4.0f, windowHeight), true))
			{
				// TODO: Obliterate!
				std::vector<Pair<std::string, MaterialID>> validMaterialNames = GetValidMaterialNames();

				bool bMatChanged = false;
				for (u32 slotIndex = 0; !bMatChanged && slotIndex < subMeshes.size(); ++slotIndex)
				{
					MeshComponent* meshComponent = subMeshes[slotIndex];

					if (meshComponent == nullptr || meshComponent->renderID == InvalidRenderID)
					{
						continue;
					}

					MaterialID matID = GetRenderObjectMaterialID(meshComponent->renderID);

					i32 selectedMaterialShortIndex = 0;
					std::string currentMaterialName = "NONE";
					i32 matShortIndex = 0;
					for (const Pair<std::string, MaterialID>& matPair : validMaterialNames)
					{
						if (matPair.second == matID)
						{
							selectedMaterialShortIndex = matShortIndex;
							currentMaterialName = matPair.first;
							break;
						}

						++matShortIndex;
					}

					std::string comboStrID = std::to_string(slotIndex);
					if (ImGui::BeginCombo(comboStrID.c_str(), currentMaterialName.c_str()))
					{
						matShortIndex = 0;
						for (const Pair<std::string, MaterialID>& matPair : validMaterialNames)
						{
							bool bSelected = (matShortIndex == selectedMaterialShortIndex);
							std::string materialName = matPair.first;
							if (ImGui::Selectable(materialName.c_str(), &bSelected))
							{
								bAnyPropertyChanged = true;
								meshComponent->SetMaterialID(matPair.second);
								selectedMaterialShortIndex = matShortIndex;
								bMatChanged = true;
							}

							++matShortIndex;
						}

						ImGui::EndCombo();
					}

					if (ImGui::BeginDragDropTarget())
					{
						const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Editor::MaterialPayloadCStr);

						if (payload && payload->Data)
						{
							MaterialID* draggedMaterialID = (MaterialID*)payload->Data;
							if (draggedMaterialID)
							{
								bAnyPropertyChanged = true;
								meshComponent->SetMaterialID(*draggedMaterialID);
								bMatChanged = true;
							}
						}

						ImGui::EndDragDropTarget();
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

	void Renderer::OnPostSceneChange()
	{
		if (m_PhysicsDebugDrawer != nullptr)
		{
			m_PhysicsDebugDrawer->OnPostSceneChange();
		}
	}

	void Renderer::LoadShaders()
	{
		std::vector<ShaderInfo> shaderInfos;
		SUPPRESS_WARN_BEGIN("-Wmissing-field-initializers");
#if COMPILE_OPEN_GL
		shaderInfos = {
			{ "deferred_combine", "deferred_combine.vert", "deferred_combine.frag" },
			{ "colour", "colour.vert", "colour.frag" },
			{ "pbr", "pbr.vert", "pbr.frag" },
			{ "pbr_ws", "pbr_ws.vert", "pbr_ws.frag" },
			{ "skybox", "skybox.vert", "skybox.frag" },
			{ "equirectangular_to_cube", "skybox.vert", "equirectangular_to_cube.frag" },
			{ "irradiance", "skybox.vert", "irradiance.frag" },
			{ "prefilter", "skybox.vert", "prefilter.frag" },
			{ "brdf", "brdf.vert", "brdf.frag" },
			{ "sprite", "sprite.vert", "sprite.frag" },
			{ "sprite_arr", "sprite.vert", "sprite_arr.frag" },
			{ "post_process", "post_process.vert", "post_process.frag" },
			{ "post_fxaa", "post_fxaa.vert", "post_fxaa.frag" },
			{ "compute_sdf", "compute_sdf.vert", "compute_sdf.frag" },
			{ "font_ss", "font_ss.vert", "font_ss.frag", "font_ss.geom" },
			{ "font_ws", "font_ws.vert", "font_ws.frag", "font_ws.geom" },
			{ "shadow", "shadow.vert" },
			{ "ssao", "ssao.vert", "ssao.frag" },
			{ "ssao_blur", "ssao_blur.vert", "ssao_blur.frag" },
			{ "taa_resolve", "post_process.vert", "taa_resolve.frag" },
			{ "gamma_correct", "post_process.vert", "gamma_correct.frag" },
			{ "blit", "blit.vert", "blit.frag" },
		};
#elif COMPILE_VULKAN
		shaderInfos = {
			{ "deferred_combine", "vk_deferred_combine_vert.spv", "vk_deferred_combine_frag.spv" },
			{ "colour", "vk_colour_vert.spv","vk_colour_frag.spv" },
			{ "pbr", "vk_pbr_vert.spv", "vk_pbr_frag.spv" },
			{ "pbr_ws", "vk_pbr_ws_vert.spv", "vk_pbr_ws_frag.spv" },
			{ "skybox", "vk_skybox_vert.spv", "vk_skybox_frag.spv" },
			{ "equirectangular_to_cube", "vk_skybox_vert.spv", "vk_equirectangular_to_cube_frag.spv" },
			{ "irradiance", "vk_skybox_vert.spv", "vk_irradiance_frag.spv" },
			{ "prefilter", "vk_skybox_vert.spv", "vk_prefilter_frag.spv" },
			{ "brdf", "vk_brdf_vert.spv", "vk_brdf_frag.spv" },
			{ "sprite", "vk_sprite_vert.spv", "vk_sprite_frag.spv" },
			{ "sprite_arr", "vk_sprite_vert.spv", "vk_sprite_arr_frag.spv" },
			{ "post_process", "vk_post_process_vert.spv", "vk_post_process_frag.spv" },
			{ "post_fxaa", "vk_barebones_pos2_uv_vert.spv", "vk_post_fxaa_frag.spv" },
			{ "compute_sdf", "vk_compute_sdf_vert.spv", "vk_compute_sdf_frag.spv" },
			{ "font_ss", "vk_font_ss_vert.spv", "vk_font_frag.spv", "vk_font_ss_geom.spv" },
			{ "font_ws", "vk_font_ws_vert.spv", "vk_font_frag.spv", "vk_font_ws_geom.spv" },
			{ "shadow", "vk_shadow_vert.spv" },
			{ "ssao", "vk_barebones_pos2_uv_vert.spv", "vk_ssao_frag.spv" }, // TODO: Why not barebones pos2?
			{ "ssao_blur", "vk_barebones_pos2_uv_vert.spv", "vk_ssao_blur_frag.spv" },
			{ "taa_resolve", "vk_barebones_pos2_uv_vert.spv", "vk_taa_resolve_frag.spv" },
			{ "gamma_correct", "vk_barebones_pos2_uv_vert.spv", "vk_gamma_correct_frag.spv" },
			{ "blit", "vk_barebones_pos2_uv_vert.spv", "vk_blit_frag.spv" },
			{ "particle_sim", "", "", "", "vk_simulate_particles_comp.spv" },
			{ "particles", "vk_particles_vert.spv", "vk_particles_frag.spv", "vk_particles_geom.spv" },
			{ "terrain", "vk_terrain_vert.spv", "vk_terrain_frag.spv" },
			{ "water", "vk_water_vert.spv", "vk_water_frag.spv" },
			{ "wireframe", "vk_wireframe_vert.spv", "vk_wireframe_frag.spv", "vk_wireframe_geom.spv" },
			{ "emissive", "vk_emissive_vert.spv", "vk_emissive_frag.spv" },
			{ "raymarched", "vk_raymarched_vert.spv", "vk_raymarched_frag.spv" },
		};
#endif
		SUPPRESS_WARN_END();

		InitializeShaders(shaderInfos);

		ShaderID shaderID = 0;

		// Deferred combine
		m_Shaders[shaderID]->renderPassType = RenderPassType::DEFERRED_COMBINE;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		// TODO: Specify that this buffer is only used in the frag shader here
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_CAM_POS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_PROJECTION_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_DIR_LIGHT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_LIGHTS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_SKYBOX_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_SHADOW_SAMPLING_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_SSAO_SAMPLING_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_NEAR_FAR_PLANES);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_BRDF_LUT_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_IRRADIANCE_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_PREFILTER_MAP);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_DEPTH_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_SSAO_FINAL_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_SHADOW_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_FB_0_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_FB_1_SAMPLER);
		++shaderID;

		// Colour
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->dynamicVertexBufferSize = 16384 * 4 * 28; // (1835008) TODO: FIXME:
		m_Shaders[shaderID]->maxObjectCount = 32;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_COLOUR_MULTIPLIER);
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

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CONST_ALBEDO);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CONST_METALLIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_METALLIC_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CONST_ROUGHNESS);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_ROUGHNESS_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_NORMAL_SAMPLER);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_METALLIC_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ROUGHNESS_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_NORMAL_SAMPLER);
		++shaderID;

		// PBR - WORLD SPACE
		m_Shaders[shaderID]->renderPassType = RenderPassType::DEFERRED;
		m_Shaders[shaderID]->numAttachments = 2;
		m_Shaders[shaderID]->maxObjectCount = 8;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::NORMAL |
			(u32)VertexAttribute::TANGENT;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CONST_ALBEDO);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CONST_METALLIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_METALLIC_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CONST_ROUGHNESS);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_ROUGHNESS_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_NORMAL_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_TEXTURE_SCALE);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_BLEND_SHARPNESS);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_METALLIC_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ROUGHNESS_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_NORMAL_SAMPLER);
		++shaderID;

		// Skybox
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 128;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_SKYBOX_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_TIME);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_CUBEMAP_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
		++shaderID;

		// Equirectangular to Cube
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 128;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_HDR_EQUIRECTANGULAR_SAMPLER);
		++shaderID;

		// Irradiance
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 128;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_CUBEMAP_SAMPLER);
		++shaderID;

		// Prefilter
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 128;
		m_Shaders[shaderID]->maxObjectCount = 1;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CONST_ROUGHNESS);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_CUBEMAP_SAMPLER);
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

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_COLOUR_MULTIPLIER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
		++shaderID;

		// Sprite - Texture Array
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 132;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->bTextureArr = true;
		m_Shaders[shaderID]->dynamicVertexBufferSize = 1024 * 1024; // TODO: FIXME:
		m_Shaders[shaderID]->renderPassType = RenderPassType::UI;
		m_Shaders[shaderID]->maxObjectCount = 1;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_COLOUR_MULTIPLIER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
		++shaderID;

		// Post processing
		m_Shaders[shaderID]->renderPassType = RenderPassType::POST_PROCESS;
		m_Shaders[shaderID]->maxObjectCount = 1;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_COLOUR_MULTIPLIER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_POST_PROCESS_MAT);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_SCENE_SAMPLER);
		++shaderID;

		// Post FXAA
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD; // TODO: FIXME:
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_FXAA_DATA);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_SCENE_SAMPLER);
		++shaderID;

		// Compute SDF
		m_Shaders[shaderID]->renderPassType = RenderPassType::DEFERRED;
		m_Shaders[shaderID]->maxObjectCount = 1;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_SDF_DATA);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_TEX_CHANNEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_HIGH_RES_TEX);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CHAR_RESOLUTION);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_SPREAD);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_HIGH_RES_TEX);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_TEX_CHANNEL);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_SDF_RESOLUTION);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_HIGH_RES);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
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

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_FONT_CHAR_DATA);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_TEX_SIZE);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
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

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_FONT_CHAR_DATA);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_TEX_SIZE);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_THRESHOLD);
		//m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_SHADOW);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
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

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		++shaderID;

		// SSAO
		m_Shaders[shaderID]->renderPassType = RenderPassType::SSAO;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_PROJECTION);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_PROJECTION_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_SSAO_GEN_DATA);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_DEPTH_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_SSAO_NORMAL_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_NOISE_SAMPLER);
		++shaderID;

		// SSAO Blur
		m_Shaders[shaderID]->renderPassType = RenderPassType::SSAO_BLUR;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->maxObjectCount = 1;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_SSAO_BLUR_DATA_CONSTANT);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_SSAO_BLUR_DATA_DYNAMIC);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_SSAO_RAW_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_SSAO_NORMAL_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_DEPTH_SAMPLER);
		++shaderID;

		// TAA Resolve
		m_Shaders[shaderID]->renderPassType = RenderPassType::TAA_RESOLVE;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->bNeedPushConstantBlock = true;
		m_Shaders[shaderID]->pushConstantBlockSize = 8;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION | // TODO: POS2
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_PROJECTION_INV);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_LAST_FRAME_VIEWPROJ);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_SCENE_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_HISTORY_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_DEPTH_SAMPLER);

		m_Shaders[shaderID]->dynamicBufferUniforms = {};
		++shaderID;

		// Gamma Correct
		m_Shaders[shaderID]->renderPassType = RenderPassType::GAMMA_CORRECT;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION | // TODO: POS2
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_SCENE_SAMPLER);
		++shaderID;

		// Blit
		m_Shaders[shaderID]->renderPassType = RenderPassType::UI;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION2 |
			(u32)VertexAttribute::UV;

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
		++shaderID;

		// Simulate particles
		m_Shaders[shaderID]->renderPassType = RenderPassType::COMPUTE_PARTICLES;
		m_Shaders[shaderID]->bCompute = true;

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_PARTICLE_SIM_DATA);

		m_Shaders[shaderID]->additionalBufferUniforms.AddUniform(U_PARTICLE_BUFFER);
		++shaderID;

		// Particles
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bDepthWriteEnable = true;
		m_Shaders[shaderID]->bTranslucent = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::VELOCITY3 |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT |
			(u32)VertexAttribute::EXTRA_VEC4;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_CAM_POS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
		++shaderID;

		// Terrain
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bDepthWriteEnable = true;
		m_Shaders[shaderID]->bTranslucent = false;
		m_Shaders[shaderID]->maxObjectCount = 1024;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT |
			(u32)VertexAttribute::NORMAL;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_DIR_LIGHT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_SKYBOX_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_SHADOW_SAMPLING_DATA);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_SHADOW_SAMPLER);
		++shaderID;

		// Water
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bDepthWriteEnable = true;
		m_Shaders[shaderID]->bTranslucent = false;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::NORMAL |
			(u32)VertexAttribute::TANGENT |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_CAM_POS);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_PROJECTION);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_DIR_LIGHT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_OCEAN_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_SKYBOX_DATA);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_TIME);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
		++shaderID;

		// Wireframe
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->maxObjectCount = 2048;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_COLOUR_MULTIPLIER);
		++shaderID;

		// Emissive
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->vertexAttributes =
			(u32)VertexAttribute::POSITION |
			(u32)VertexAttribute::UV |
			(u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT |
			(u32)VertexAttribute::NORMAL |
			(u32)VertexAttribute::TANGENT;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CONST_ALBEDO);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CONST_EMISSIVE);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_EMISSIVE_SAMPLER);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_CONST_ROUGHNESS);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_ENABLE_NORMAL_SAMPLER);

		m_Shaders[shaderID]->textureUniforms.AddUniform(U_ALBEDO_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_EMISSIVE_SAMPLER);
		m_Shaders[shaderID]->textureUniforms.AddUniform(U_NORMAL_SAMPLER);
		++shaderID;

		// Raymarched
		m_Shaders[shaderID]->renderPassType = RenderPassType::FORWARD;
		m_Shaders[shaderID]->bTranslucent = true;
		m_Shaders[shaderID]->bDepthWriteEnable = false;
		m_Shaders[shaderID]->vertexAttributes = (u32)VertexAttribute::POSITION;

		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);
		m_Shaders[shaderID]->constantBufferUniforms.AddUniform(U_TIME);

		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
		m_Shaders[shaderID]->dynamicBufferUniforms.AddUniform(U_MODEL);
		++shaderID;

		assert(shaderID == m_Shaders.size());

		for (shaderID = 0; shaderID < (ShaderID)m_Shaders.size(); ++shaderID)
		{
			Shader* shader = m_Shaders[shaderID];

			// Sanity checks
			{
				assert(!shader->constantBufferUniforms.HasUniform(U_UNIFORM_BUFFER_DYNAMIC));
				assert(!shader->dynamicBufferUniforms.HasUniform(U_UNIFORM_BUFFER_CONSTANT));

				assert((shader->bNeedPushConstantBlock && shader->pushConstantBlockSize != 0) ||
					(!shader->bNeedPushConstantBlock && shader->pushConstantBlockSize == 0));


				if (shader->textureUniforms.HasUniform(U_HIGH_RES_TEX))
				{
					assert(!shader->textureUniforms.HasUniform(U_ALBEDO_SAMPLER));
				}

				// -1 means allocate max, anything else must be > 0
				assert(shader->maxObjectCount != 0);
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
	}

	void Renderer::GenerateGBuffer()
	{
		assert(m_SkyBoxMesh != nullptr);
		assert(m_SkyboxShaderID != InvalidShaderID);
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
			gBufferMaterialCreateInfo.visibleInEditor = false;
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
		//	gBufferCubemapMaterialCreateInfo.visibleInEditor = false;
		//	FillOutGBufferFrameBufferAttachments(gBufferCubemapMaterialCreateInfo.sampledFrameBuffers);
		//
		//	m_CubemapGBufferMaterialID = InitializeMaterial(&gBufferCubemapMaterialCreateInfo);
		//}
	}

	void Renderer::EnqueueScreenSpaceText()
	{
		SetFont(SID("editor-02"));
		static const glm::vec4 colour(0.95f);
		DrawStringSS("FLEX ENGINE", colour, AnchorPoint::TOP_RIGHT, glm::vec2(-0.03f, -0.055f), 1.5f, 0.6f);
		if (g_EngineInstance->IsSimulationPaused())
		{
			const std::vector<TextCache>& textCaches = m_CurrentFont->GetTextCaches();
			real height = GetStringHeight(textCaches[textCaches.size() - 1], m_CurrentFont) / (real)g_Window->GetSize().y;
			// TODO: Allow specifying text pos in different units (absolute, relative, ...)
			DrawStringSS("PAUSED", colour, AnchorPoint::TOP_RIGHT, glm::vec2(-0.03f, -(height + 0.09f)), 0.0f, 0.6f);
		}

		if (AudioManager::IsMuted())
		{
			const std::vector<TextCache>& textCaches = m_CurrentFont->GetTextCaches();
			real height = GetStringHeight(textCaches[textCaches.size() - 1], m_CurrentFont) / (real)g_Window->GetSize().y;
			DrawStringSS("Muted", colour, AnchorPoint::TOP_RIGHT, glm::vec2(-0.03f, -(height + 0.09f)), 0.0f, 0.6f);
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

	void Renderer::InitializeMaterials()
	{
		MaterialCreateInfo spriteMatSSCreateInfo = {};
		spriteMatSSCreateInfo.name = "Sprite SS material";
		spriteMatSSCreateInfo.shaderName = "sprite";
		spriteMatSSCreateInfo.persistent = true;
		spriteMatSSCreateInfo.visibleInEditor = false;
		spriteMatSSCreateInfo.enableAlbedoSampler = true;
		spriteMatSSCreateInfo.bDynamic = false;
		spriteMatSSCreateInfo.bSerializable = false;
		m_SpriteMatSSID = InitializeMaterial(&spriteMatSSCreateInfo);

		MaterialCreateInfo spriteMatWSCreateInfo = {};
		spriteMatWSCreateInfo.name = "Sprite WS material";
		spriteMatWSCreateInfo.shaderName = "sprite";
		spriteMatWSCreateInfo.persistent = true;
		spriteMatWSCreateInfo.visibleInEditor = false;
		spriteMatWSCreateInfo.enableAlbedoSampler = true;
		spriteMatWSCreateInfo.bDynamic = false;
		spriteMatWSCreateInfo.bSerializable = false;
		m_SpriteMatWSID = InitializeMaterial(&spriteMatWSCreateInfo);

		MaterialCreateInfo spriteArrMatCreateInfo = {};
		spriteArrMatCreateInfo.name = "Sprite Texture Array material";
		spriteArrMatCreateInfo.shaderName = "sprite_arr";
		spriteArrMatCreateInfo.persistent = true;
		spriteArrMatCreateInfo.visibleInEditor = false;
		spriteArrMatCreateInfo.enableAlbedoSampler = true;
		spriteArrMatCreateInfo.bDynamic = false;
		spriteArrMatCreateInfo.bSerializable = false;
		m_SpriteArrMatID = InitializeMaterial(&spriteArrMatCreateInfo);

		MaterialCreateInfo fontSSMatCreateInfo = {};
		fontSSMatCreateInfo.name = "font ss";
		fontSSMatCreateInfo.shaderName = "font_ss";
		fontSSMatCreateInfo.persistent = true;
		fontSSMatCreateInfo.visibleInEditor = false;
		fontSSMatCreateInfo.bDynamic = false;
		fontSSMatCreateInfo.bSerializable = false;
		m_FontMatSSID = InitializeMaterial(&fontSSMatCreateInfo);

		MaterialCreateInfo fontWSMatCreateInfo = {};
		fontWSMatCreateInfo.name = "font ws";
		fontWSMatCreateInfo.shaderName = "font_ws";
		fontWSMatCreateInfo.persistent = true;
		fontWSMatCreateInfo.visibleInEditor = false;
		fontWSMatCreateInfo.bDynamic = false;
		fontWSMatCreateInfo.bSerializable = false;
		m_FontMatWSID = InitializeMaterial(&fontWSMatCreateInfo);

		MaterialCreateInfo shadowMatCreateInfo = {};
		shadowMatCreateInfo.name = "shadow";
		shadowMatCreateInfo.shaderName = "shadow";
		shadowMatCreateInfo.persistent = true;
		shadowMatCreateInfo.visibleInEditor = false;
		shadowMatCreateInfo.bSerializable = false;
		m_ShadowMaterialID = InitializeMaterial(&shadowMatCreateInfo);

		MaterialCreateInfo postProcessMatCreateInfo = {};
		postProcessMatCreateInfo.name = "Post process material";
		postProcessMatCreateInfo.shaderName = "post_process";
		postProcessMatCreateInfo.persistent = true;
		postProcessMatCreateInfo.visibleInEditor = false;
		postProcessMatCreateInfo.bSerializable = false;
		m_PostProcessMatID = InitializeMaterial(&postProcessMatCreateInfo);

		MaterialCreateInfo postFXAAMatCreateInfo = {};
		postFXAAMatCreateInfo.name = "fxaa";
		postFXAAMatCreateInfo.shaderName = "post_fxaa";
		postFXAAMatCreateInfo.persistent = true;
		postFXAAMatCreateInfo.visibleInEditor = false;
		postFXAAMatCreateInfo.bSerializable = false;
		m_PostFXAAMatID = InitializeMaterial(&postFXAAMatCreateInfo);

		MaterialCreateInfo selectedObjectMatCreateInfo = {};
		selectedObjectMatCreateInfo.name = "Selected Object";
		selectedObjectMatCreateInfo.shaderName = "colour";
		selectedObjectMatCreateInfo.persistent = true;
		selectedObjectMatCreateInfo.visibleInEditor = false;
		selectedObjectMatCreateInfo.colourMultiplier = VEC4_ONE;
		selectedObjectMatCreateInfo.bSerializable = false;
		m_SelectedObjectMatID = InitializeMaterial(&selectedObjectMatCreateInfo);

		MaterialCreateInfo taaMatCreateInfo = {};
		taaMatCreateInfo.name = "TAA Resolve";
		taaMatCreateInfo.shaderName = "taa_resolve";
		taaMatCreateInfo.persistent = true;
		taaMatCreateInfo.visibleInEditor = false;
		taaMatCreateInfo.colourMultiplier = VEC4_ONE;
		taaMatCreateInfo.bSerializable = false;
		m_TAAResolveMaterialID = InitializeMaterial(&taaMatCreateInfo);

		MaterialCreateInfo gammaCorrectMatCreateInfo = {};
		gammaCorrectMatCreateInfo.name = "Gamma Correct";
		gammaCorrectMatCreateInfo.shaderName = "gamma_correct";
		gammaCorrectMatCreateInfo.persistent = true;
		gammaCorrectMatCreateInfo.visibleInEditor = false;
		gammaCorrectMatCreateInfo.colourMultiplier = VEC4_ONE;
		gammaCorrectMatCreateInfo.bSerializable = false;
		m_GammaCorrectMaterialID = InitializeMaterial(&gammaCorrectMatCreateInfo);

		MaterialCreateInfo fullscreenBlitMatCreateInfo = {};
		fullscreenBlitMatCreateInfo.name = "fullscreen blit";
		fullscreenBlitMatCreateInfo.shaderName = "blit";
		fullscreenBlitMatCreateInfo.persistent = true;
		fullscreenBlitMatCreateInfo.visibleInEditor = false;
		fullscreenBlitMatCreateInfo.enableAlbedoSampler = true;
		fullscreenBlitMatCreateInfo.bSerializable = false;
		m_FullscreenBlitMatID = InitializeMaterial(&fullscreenBlitMatCreateInfo);

		MaterialCreateInfo computeSDFMatCreateInfo = {};
		computeSDFMatCreateInfo.name = "compute SDF";
		computeSDFMatCreateInfo.shaderName = "compute_sdf";
		computeSDFMatCreateInfo.persistent = true;
		computeSDFMatCreateInfo.visibleInEditor = false;
		computeSDFMatCreateInfo.bSerializable = false;
		m_ComputeSDFMatID = InitializeMaterial(&computeSDFMatCreateInfo);

		MaterialCreateInfo irradianceCreateInfo = {};
		irradianceCreateInfo.name = "irradiance";
		irradianceCreateInfo.shaderName = "irradiance";
		irradianceCreateInfo.persistent = true;
		irradianceCreateInfo.visibleInEditor = false;
		irradianceCreateInfo.bSerializable = false;
		m_IrradianceMaterialID = InitializeMaterial(&irradianceCreateInfo);

		MaterialCreateInfo prefilterCreateInfo = {};
		prefilterCreateInfo.name = "prefilter";
		prefilterCreateInfo.shaderName = "prefilter";
		prefilterCreateInfo.persistent = true;
		prefilterCreateInfo.visibleInEditor = false;
		prefilterCreateInfo.bSerializable = false;
		m_PrefilterMaterialID = InitializeMaterial(&prefilterCreateInfo);

		MaterialCreateInfo brdfCreateInfo = {};
		brdfCreateInfo.name = "brdf";
		brdfCreateInfo.shaderName = "brdf";
		brdfCreateInfo.persistent = true;
		brdfCreateInfo.visibleInEditor = false;
		brdfCreateInfo.bSerializable = false;
		m_BRDFMaterialID = InitializeMaterial(&brdfCreateInfo);

		MaterialCreateInfo wireframeCreateInfo = {};
		wireframeCreateInfo.name = "wireframe";
		wireframeCreateInfo.shaderName = "wireframe";
		wireframeCreateInfo.persistent = true;
		wireframeCreateInfo.visibleInEditor = false;
		wireframeCreateInfo.bSerializable = false;
		m_WireframeMatID = InitializeMaterial(&wireframeCreateInfo);

		MaterialCreateInfo placeholderMatCreateInfo = {};
		placeholderMatCreateInfo.name = "placeholder";
		placeholderMatCreateInfo.shaderName = "pbr";
		placeholderMatCreateInfo.persistent = true;
		placeholderMatCreateInfo.visibleInEditor = false;
		placeholderMatCreateInfo.constAlbedo = glm::vec3(1.0f, 0.0f, 1.0f);
		placeholderMatCreateInfo.bSerializable = false;
		m_PlaceholderMaterialID = InitializeMaterial(&placeholderMatCreateInfo);
	}

	std::string Renderer::PickRandomSkyboxTexture()
	{
		i32 matIdx = -1;
		i32 attemptCount = 0;
		do
		{
			matIdx = RandomInt(0, (i32)m_AvailableHDRIs.size());
			++attemptCount;
		} while (!FileExists(m_AvailableHDRIs[matIdx]) && attemptCount < 15);

		if (matIdx == -1)
		{
			PrintWarn("Unable to open any available HDRIs!\n");
			return EMPTY_STRING;
		}

		return m_AvailableHDRIs[matIdx];
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
				RecreateShadowFrameBuffers();
			}

			if (ImGuiExt::SliderUInt("Shadow cascade base resolution", &m_ShadowMapBaseResolution, 128u, 4096u))
			{
				m_ShadowMapBaseResolution = NextPowerOfTwo(glm::clamp(m_ShadowMapBaseResolution, 128u, 4096u));
				RecreateShadowFrameBuffers();
			}

			if (ImGui::SliderInt("Shader quality level", &m_ShaderQualityLevel, 0, 3))
			{
				m_ShaderQualityLevel = glm::clamp(m_ShaderQualityLevel, 0, 3);
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

			ImGui::Checkbox("FXAA", &m_PostProcessSettings.bEnableFXAA);

			if (m_PostProcessSettings.bEnableFXAA)
			{
				ImGui::Indent();
				ImGui::Checkbox("Show edges", &m_PostProcessSettings.bEnableFXAADEBUGShowEdges);
				ImGui::Unindent();
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

			if (ImGui::SliderInt("Kernel Size", &m_SSAOKernelSize, 1, 64))
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
		MaterialCreateInfo particleSimMatCreateInfo = {};
		particleSimMatCreateInfo.name = name;
		particleSimMatCreateInfo.shaderName = "particle_sim";
		particleSimMatCreateInfo.persistent = true;
		particleSimMatCreateInfo.visibleInEditor = false;
		particleSimMatCreateInfo.bSerializable = false;
		return InitializeMaterial(&particleSimMatCreateInfo);
	}

	MaterialID Renderer::CreateParticleSystemRenderingMaterial(const std::string& name)
	{
		MaterialCreateInfo particleMatCreateInfo = {};
		particleMatCreateInfo.name = name;
		particleMatCreateInfo.shaderName = "particles";
		particleMatCreateInfo.persistent = true;
		particleMatCreateInfo.visibleInEditor = false;
		particleMatCreateInfo.bSerializable = false;
		return InitializeMaterial(&particleMatCreateInfo);
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
} // namespace flex
