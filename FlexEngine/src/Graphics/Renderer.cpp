	#include "stdafx.hpp"

#include "Graphics/Renderer.hpp"

IGNORE_WARNINGS_PUSH
#if COMPILE_IMGUI
#include "imgui.h"
#include "imgui_internal.h"
#endif

#include <glm/gtx/transform.hpp> // for scale
#include <glm/gtx/quaternion.hpp> // for rotate

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/fttypes.h>
#include <freetype/fterrors.h>
IGNORE_WARNINGS_POP

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
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Monitor.hpp"
#include "Window/Window.hpp"

namespace flex
{
	Renderer::Renderer() :
		m_DefaultSettingsFilePathAbs(RelativePathToAbsolute(ROOT_LOCATION "config/default-renderer-settings.ini")),
		m_SettingsFilePathAbs(RelativePathToAbsolute(ROOT_LOCATION "config/renderer-settings.ini")),
		m_FontsFilePathAbs(RelativePathToAbsolute(ROOT_LOCATION "config/fonts.ini"))
	{
	}

	Renderer::~Renderer()
	{
	}

	void Renderer::Initialize()
	{
		if (!FileExists(m_FontsFilePathAbs))
		{
			PrintError("Fonts file missing!\n");
		}
		else
		{
			JSONObject fontSettings;
			if (JSONParser::Parse(m_FontsFilePathAbs, fontSettings))
			{
				std::vector<JSONObject> fontObjs;
				if (fontSettings.SetObjectArrayChecked("fonts", fontObjs))
				{
					static const std::string DPIStr = FloatToString(g_Monitor->DPI.x, 0) + "DPI";

					for (const JSONObject& fontObj : fontObjs)
					{
						FontMetaData fontMetaData = {};

						fontObj.SetStringChecked("file path", fontMetaData.filePath);
						fontMetaData.size = (i16)fontObj.GetInt("size");
						fontObj.SetBoolChecked("screen space", fontMetaData.bScreenSpace);
						fontObj.SetFloatChecked("threshold", fontMetaData.threshold);
						fontObj.SetFloatChecked("shadow opacity", fontMetaData.shadowOpacity);
						fontObj.SetVec2Checked("shadow offset", fontMetaData.shadowOffset);
						fontObj.SetFloatChecked("soften", fontMetaData.soften);

						if (fontMetaData.filePath.empty())
						{
							PrintError("Font doesn't contain file path!\n");
							continue;
						}

						fontMetaData.renderedTextureFilePath = StripFileType(fontMetaData.filePath);

						fontMetaData.renderedTextureFilePath += "-" + IntToString(fontMetaData.size, 2) + "-" + DPIStr + m_FontImageExtension;
						fontMetaData.renderedTextureFilePath = RESOURCE_LOCATION "fonts/" + fontMetaData.renderedTextureFilePath;
						fontMetaData.filePath = RESOURCE_LOCATION "fonts/" + fontMetaData.filePath;

						std::string fontName = fontObj.GetString("name");
						m_Fonts[fontName] = fontMetaData;
					}
				}
			}
		}

		std::string hdriPath = RESOURCE("textures\\hdri\\");
		if (!FindFilesInDirectory(hdriPath, m_AvailableHDRIs, "hdr"))
		{
			PrintWarn("Unable to find hdri directory at %s\n", hdriPath.c_str());
		}

		m_PointLights = (PointLightData*)malloc_hooked(MAX_NUM_POINT_LIGHTS * sizeof(PointLightData));
		for (i32 i = 0; i < MAX_NUM_POINT_LIGHTS; ++i)
		{
			m_PointLights[i].color = VEC3_NEG_ONE;
			m_PointLights[i].enabled = 0;
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
		glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();

		m_SSAOSamplingData.ssaoEnabled = 1;
		m_SSAOSamplingData.ssaoPowExp = 2.0f;

		m_ShadowSamplingData.cascadeDepthSplits = glm::vec4(0.1f, 0.25f, 0.5f, 0.8f);
	}

	void Renderer::PostInitialize()
	{
		// TODO: Use MeshComponent for these objects?

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
				(u32)VertexAttribute::POSITION_2D |
				(u32)VertexAttribute::UV;

			m_FullScreenTriVertexBufferData = {};
			m_FullScreenTriVertexBufferData.Initialize(&triVertexBufferDataCreateInfo);


			GameObject* fullScreenTriGameObject = new GameObject("Full screen triangle", GameObjectType::_NONE);
			m_PersistentObjects.push_back(fullScreenTriGameObject);
			fullScreenTriGameObject->SetVisible(false);
			fullScreenTriGameObject->SetCastsShadow(false);

			RenderObjectCreateInfo fullScreenTriCreateInfo = {};
			fullScreenTriCreateInfo.vertexBufferData = &m_FullScreenTriVertexBufferData;
			fullScreenTriCreateInfo.materialID = m_PostProcessMatID;
			fullScreenTriCreateInfo.bDepthWriteEnable = false;
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
			m_Quad3DVertexBufferData.Initialize(&quad3DVertexBufferDataCreateInfo);


			GameObject* quad3DGameObject = new GameObject("Sprite Quad 3D", GameObjectType::_NONE);
			m_PersistentObjects.push_back(quad3DGameObject);
			quad3DGameObject->SetVisible(false);
			quad3DGameObject->SetCastsShadow(false);

			RenderObjectCreateInfo quad3DCreateInfo = {};
			quad3DCreateInfo.vertexBufferData = &m_Quad3DVertexBufferData;
			quad3DCreateInfo.materialID = m_SpriteMatWSID;
			quad3DCreateInfo.bDepthWriteEnable = false;
			quad3DCreateInfo.gameObject = quad3DGameObject;
			quad3DCreateInfo.cullFace = CullFace::NONE;
			quad3DCreateInfo.visibleInSceneExplorer = false;
			quad3DCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
			quad3DCreateInfo.bEditorObject = true; // TODO: Create other quad which is identical but is not an editor object for gameplay objects?
			quad3DCreateInfo.renderPassOverride = RenderPassType::FORWARD;
			m_Quad3DRenderID = InitializeRenderObject(&quad3DCreateInfo);

			m_Quad3DVertexBufferData.DescribeShaderVariables(this, m_Quad3DRenderID);

			quad3DCreateInfo.materialID = m_SpriteMatSSID;
			quad3DCreateInfo.renderPassOverride = RenderPassType::UI;
			m_Quad3DSSRenderID = InitializeRenderObject(&quad3DCreateInfo);
		}
	}

	void Renderer::Destroy()
	{
		free_hooked(m_PointLights);

		m_Quad3DVertexBufferData.Destroy();
		m_FullScreenTriVertexBufferData.Destroy();

		DestroyRenderObject(m_FullScreenTriRenderID);
		DestroyRenderObject(m_Quad3DRenderID);
		DestroyRenderObject(m_Quad3DSSRenderID);
		DestroyRenderObject(m_GBufferQuadRenderID);
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

	void Renderer::SaveSettingsToDisk(bool bSaveOverDefaults /* = false */, bool bAddEditorStr /* = true */)
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

	void Renderer::LoadSettingsFromDisk(bool bLoadDefaults /* = false */)
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
		const glm::vec4& color)
	{
		SpriteQuadDrawInfo drawInfo = {};

		drawInfo.materialID = m_SpriteMatSSID;
		drawInfo.scale = glm::vec3(size.x, size.y, 1.0f);
		drawInfo.bScreenSpace = true;
		drawInfo.bReadDepth = false;
		drawInfo.bWriteDepth = false;
		drawInfo.anchor = anchor;
		drawInfo.color = color;
		drawInfo.pos = glm::vec3(pos.x, pos.y, 1.0f);
		drawInfo.bEnableAlbedoSampler = false;

		EnqueueSprite(drawInfo);
	}

	void Renderer::EnqueueUntexturedQuadRaw(const glm::vec2& pos,
		const glm::vec2& size,
		const glm::vec4& color)
	{
		SpriteQuadDrawInfo drawInfo = {};

		drawInfo.materialID = m_SpriteMatSSID;
		drawInfo.scale = glm::vec3(size.x, size.y, 1.0f);
		drawInfo.bScreenSpace = true;
		drawInfo.bReadDepth = false;
		drawInfo.bWriteDepth = false;
		drawInfo.bRaw = true;
		drawInfo.color = color;
		drawInfo.pos = glm::vec3(pos.x, pos.y, 1.0f);
		drawInfo.bEnableAlbedoSampler = false;

		EnqueueSprite(drawInfo);
	}

	void Renderer::EnqueueSprite(const SpriteQuadDrawInfo& drawInfo)
	{
		if (drawInfo.bScreenSpace)
		{
			if (drawInfo.materialID != InvalidMaterialID && GetShader(GetMaterial(drawInfo.materialID).shaderID).bTextureArr)
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
		if (m_NumPointLightsEnabled < MAX_NUM_POINT_LIGHTS)
		{
			PointLightID newPointLightID = (PointLightID)m_NumPointLightsEnabled;
			memcpy(m_PointLights + newPointLightID, pointLightData, sizeof(PointLightData));
			m_NumPointLightsEnabled++;
			return newPointLightID;
		}
		return InvalidPointLightID;
	}

	void Renderer::UpdatePointLightData(PointLightID ID, PointLightData* data)
	{
		assert(ID < MAX_NUM_POINT_LIGHTS);
		assert(data != nullptr);

		memcpy(m_PointLights + ID, data, sizeof(PointLightData));
	}

	void Renderer::RemoveDirectionalLight()
	{
		m_DirectionalLight = nullptr;
	}

	void Renderer::RemovePointLight(PointLightID ID)
	{
		if (m_PointLights[ID].color.x != -1.0f)
		{
			m_PointLights[ID].color = VEC4_NEG_ONE;
			m_PointLights[ID].enabled = 0;
			m_NumPointLightsEnabled--;
			assert(m_NumPointLightsEnabled >= 0);
		}
	}

	void Renderer::RemoveAllPointLights()
	{
		for (i32 i = 0; i < m_NumPointLightsEnabled; ++i)
		{
			m_PointLights[i].color = VEC4_NEG_ONE;
			m_PointLights[i].enabled = 0;
		}
		m_NumPointLightsEnabled = 0;
	}

	DirLightData* Renderer::GetDirectionalLight()
	{
		if (m_DirectionalLight)
		{
			return &m_DirectionalLight->data;
		}
		return nullptr;
	}

	PointLightData* Renderer::GetPointLight(PointLightID ID)
	{
		return &m_PointLights[ID];
	}

	i32 Renderer::GetNumPointLights()
	{
		return m_NumPointLightsEnabled;
	}

	i32 Renderer::GetFramesRenderedCount() const
	{
		return m_FramesRendered;
	}

	BitmapFont* Renderer::SetFont(StringID fontID)
	{
		m_CurrentFont = m_Fonts[fontID].bitmapFont;
		return m_CurrentFont;
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

	bool Renderer::IsTAAEnabled() const
	{
		return m_bEnableTAA;
	}

	i32 Renderer::GetTAASampleCount() const
	{
		return m_TAASampleCount;
	}

	void Renderer::EnqueueScreenSpaceSprites()
	{
		if (m_bDisplayShadowCascadePreview)
		{
			SpriteQuadDrawInfo drawInfo = {};
			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = true;
			drawInfo.bWriteDepth = true;
			drawInfo.materialID = m_SpriteArrMatID;
			drawInfo.anchor = AnchorPoint::BOTTOM_RIGHT;
			drawInfo.scale = glm::vec3(0.2f);
			for (u32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
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

			real lastSplitDist = 0.0;
			for (u32 c = 0; c < NUM_SHADOW_CASCADES; ++c)
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

	void Renderer::DrawImGuiMisc()
	{

	}

	void Renderer::DrawImGuiWindows()
	{
		if (bFontWindowShowing)
		{
			if (ImGui::Begin("Fonts", &bFontWindowShowing))
			{
				for (auto& fontPair : m_Fonts)
				{
					FontMetaData& fontMeta = fontPair.second;
					BitmapFont* font = fontMeta.bitmapFont;

					ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollWithMouse;
					if (ImGui::BeginChild(fontMeta.renderedTextureFilePath.c_str(), ImVec2(0, 240), true, flags))
					{
						ImGui::Text("%s", fontPair.first.c_str());
						ImGui::Text("%s", font->name.c_str());

						ImGui::Columns(2);
						ImGui::SetColumnWidth(0, 350.0f);

						ImGui::DragFloat("Threshold", &font->metaData.threshold, 0.001f, 0.0f, 1.0f);
						ImGui::DragFloat2("Shadow Offset", &font->metaData.shadowOffset.x, 0.0007f);
						ImGui::DragFloat("Shadow Opacity", &font->metaData.shadowOpacity, 0.005f, 0.0f, 0.999f);
						ImGui::DragFloat("Soften", &font->metaData.soften, 0.001f, 0.0f, 1.0f);

						ImGui::Text("Size: %i", font->metaData.size);
						ImGui::SameLine();
						ImGui::Text("%s space", fontMeta.bScreenSpace ? "Screen" : "World");
						glm::vec2u texSize(font->GetTextureSize());
						ImGui::Text("Resolution: %ux%u", texSize.x, texSize.y);
						ImGui::Text("Char count: %i", font->characterCount);
						ImGui::Text("Byte count: %i", font->bufferSize);
						ImGui::Text("Use kerning: %s", font->bUseKerning ? "true" : "false");
						// TODO: Add support to ImGui vulkan renderer for images
						//VulkanTexture* tex = font->GetTexture();
						//ImVec2 texSize((real)tex->width, (real)tex->height);
						//ImVec2 uv0(0.0f, 0.0f);
						//ImVec2 uv1(1.0f, 1.0f);
						//ImGui::Image((void*)&tex->image, texSize, uv0, uv1);

						ImGui::NextColumn();
						if (ImGui::Button("Re-bake"))
						{
							if (fontMeta.bScreenSpace)
							{
								auto vecIterSS = std::find(m_FontsSS.begin(), m_FontsSS.end(), fontMeta.bitmapFont);
								assert(vecIterSS != m_FontsSS.end());

								m_FontsSS.erase(vecIterSS);
							}
							else
							{
								auto vecIterWS = std::find(m_FontsWS.begin(), m_FontsWS.end(), fontMeta.bitmapFont);
								assert(vecIterWS != m_FontsWS.end());

								m_FontsWS.erase(vecIterWS);
							}

							delete fontMeta.bitmapFont;
							fontMeta.bitmapFont = nullptr;
							font = nullptr;

							LoadFont(fontMeta, true);

						}
						if (ImGui::Button("View SDF"))
						{
							std::string absDir = RelativePathToAbsolute(fontMeta.renderedTextureFilePath);
							OpenExplorer(absDir);
						}
						if (ImGui::Button("Open in explorer"))
						{
							const std::string absDir = ExtractDirectoryString(RelativePathToAbsolute(fontMeta.renderedTextureFilePath));
							OpenExplorer(absDir);
						}
						ImGui::EndColumns();
					}
					ImGui::EndChild();
				}

				if (ImGui::Button("Re-bake all"))
				{
					LoadFonts(true);
				}
			}
			ImGui::End();
		}
	}

	void Renderer::DrawImGuiRenderObjects()
	{
		ImGui::NewLine();

		ImGui::BeginChild("SelectedObject", ImVec2(0.0f, 500.0f), true);

		const std::vector<GameObject*>& selectedObjects = g_Editor->GetSelectedObjects();
		if (!selectedObjects.empty())
		{
			// TODO: Draw common fields for all selected objects?
			GameObject* selectedObject = selectedObjects[0];
			if (selectedObject)
			{
				selectedObject->DrawImGuiObjects();
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
			if (DrawImGuiGameObjectNameAndChildren(rootObject))
			{
				break;
			}
		}

		DoCreateGameObjectButton("Add object...", "Add object");

		const bool bShowAddPointLightBtn = m_NumPointLightsEnabled < MAX_NUM_POINT_LIGHTS;
		if (bShowAddPointLightBtn)
		{
			if (ImGui::Button("Add point light"))
			{
				BaseScene* scene = g_SceneManager->CurrentScene();
				PointLight* newPointLight = new PointLight(scene);
				scene->AddRootObject(newPointLight);
				newPointLight->Initialize();
				newPointLight->PostInitialize();

				g_Editor->SetSelectedObject(newPointLight);
			}
		}

		const bool bShowAddDirLightBtn = m_DirectionalLight == nullptr;
		if (bShowAddDirLightBtn)
		{
			if (bShowAddPointLightBtn)
			{
				ImGui::SameLine();
			}

			if (ImGui::Button("Add directional light"))
			{
				BaseScene* scene = g_SceneManager->CurrentScene();
				DirectionalLight* newDiright = new DirectionalLight();
				scene->AddRootObject(newDiright);
				newDiright->Initialize();
				newDiright->PostInitialize();

				g_Editor->SetSelectedObject(newDiright);
			}
		}
	}

	void Renderer::DrawImGuiSettings()
	{
		if (ImGui::TreeNode("Renderer settings"))
		{
			if (ImGui::Button(" Save "))
			{
				g_Renderer->SaveSettingsToDisk(false, true);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);
			{
				ImGui::SameLine();
				if (ImGui::Button("Save defaults"))
				{
					g_Renderer->SaveSettingsToDisk(true, true);
				}

				ImGui::SameLine();
				if (ImGui::Button("Reload defaults"))
				{
					g_Renderer->LoadSettingsFromDisk(true);
				}
			}
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();

			if (ImGui::Button("Recapture reflection probe"))
			{
				g_Renderer->RecaptureReflectionProbe();
			}

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

			if (ImGui::TreeNode("Debug objects"))
			{
				PhysicsDebuggingSettings& physicsDebuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();

				ImGui::Checkbox("Disable All", &physicsDebuggingSettings.DisableAll);

				ImGui::Spacing();
				ImGui::Spacing();
				ImGui::Spacing();

				bool bRenderEditorObjs = g_EngineInstance->IsRenderingEditorObjects();
				if (ImGui::Checkbox("Editor objects", &bRenderEditorObjs))
				{
					g_EngineInstance->SetRenderingEditorObjects(bRenderEditorObjs);
				}

				if (physicsDebuggingSettings.DisableAll)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				}

				bool bDisplayBoundingVolumes = g_Renderer->IsDisplayBoundingVolumesEnabled();
				if (ImGui::Checkbox("Bounding volumes", &bDisplayBoundingVolumes))
				{
					g_Renderer->SetDisplayBoundingVolumesEnabled(bDisplayBoundingVolumes);
				}

				ImGui::Checkbox("Wireframe (P)", &physicsDebuggingSettings.DrawWireframe);

				ImGui::Checkbox("AABB", &physicsDebuggingSettings.DrawAabb);

				if (physicsDebuggingSettings.DisableAll)
				{
					ImGui::PopStyleColor();
				}

				ImGui::TreePop();
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Post processing"))
		{
			ImGui::Checkbox("TAA", &m_bEnableTAA);

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

			bool bSSAOEnabled = m_SSAOSamplingData.ssaoEnabled != 0;
			if (ImGui::Checkbox("SSAO", &bSSAOEnabled))
			{
				m_SSAOSamplingData.ssaoEnabled = bSSAOEnabled ? 1 : 0;
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
					m_SSAOSamplingData.ssaoEnabled = 1;
				}
			}

			if (ImGui::SliderInt("Kernel Size", &m_SSAOKernelSize, 1, 64))
			{
				m_bSSAOStateChanged = true;
			}
			ImGui::SliderFloat("Radius", &m_SSAOGenData.radius, 0.0001f, 15.0f);
			ImGui::SliderInt("Blur Radius", &m_SSAOBlurDataConstant.radius, 1, 16);
			ImGui::SliderInt("Blur Offset Count", &m_SSAOBlurSamplePixelOffset, 1, 10);
			ImGui::SliderFloat("Pow", &m_SSAOSamplingData.ssaoPowExp, 0.1f, 10.0f);
			
			ImGui::PopItemWidth();

			ImGui::TreePop();
		}
	}

	void Renderer::DrawImGuiForGameObjectWithValidRenderID(GameObject* gameObject)
	{
		RenderID renderID = gameObject->GetRenderID();
		assert(renderID != InvalidRenderID);

		MaterialID matID = g_Renderer->GetMaterialID(renderID);

		g_Renderer->DrawImGuiForRenderObject(renderID);

		MeshComponent* mesh = gameObject->GetMeshComponent();

		std::vector<Pair<std::string, MaterialID>> validMaterialNames = g_Renderer->GetValidMaterialNames();

		MaterialID selectedMaterialID = 0;
		i32 selectedMaterialShortIndex = 0;
		std::string currentMaterialName = "NONE";
		i32 matShortIndex = 0;
		for (const Pair<std::string, MaterialID>& matPair : validMaterialNames)
		{
			if (matPair.second == (i32)matID)
			{
				selectedMaterialID = matPair.second;
				selectedMaterialShortIndex = matShortIndex;
				currentMaterialName = matPair.first;
				break;
			}

			++matShortIndex;
		}
		if (ImGui::BeginCombo("Material", currentMaterialName.c_str()))
		{
			matShortIndex = 0;
			for (const Pair<std::string, MaterialID>& matPair : validMaterialNames)
			{
				bool bSelected = (matShortIndex == selectedMaterialShortIndex);
				std::string materialName = matPair.first;
				if (ImGui::Selectable(materialName.c_str(), &bSelected))
				{
					if (mesh)
					{
						mesh->SetMaterialID(matPair.second);
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
					if (mesh)
					{
						mesh->SetMaterialID(*draggedMaterialID);
					}
				}
			}

			ImGui::EndDragDropTarget();
		}

		if (mesh)
		{
			MeshComponent::Type meshType = mesh->GetType();
			switch (meshType)
			{
			case MeshComponent::Type::FILE:
			{
				i32 selectedMeshIndex = 0;
				std::string currentMeshFileName = "NONE";
				i32 i = 0;
				for (auto iter : MeshComponent::m_LoadedMeshes)
				{
					const std::string meshFileName = StripLeadingDirectories(iter.first);
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

					for (auto meshPair : MeshComponent::m_LoadedMeshes)
					{
						bool bSelected = (i == selectedMeshIndex);
						const std::string meshFileName = StripLeadingDirectories(meshPair.first);
						if (ImGui::Selectable(meshFileName.c_str(), &bSelected))
						{
							if (selectedMeshIndex != i)
							{
								selectedMeshIndex = i;
								std::string relativeFilePath = meshPair.first;
								MaterialID meshMatID = mesh->GetMaterialID();
								DestroyRenderObject(renderID);
								gameObject->SetRenderID(InvalidRenderID);
								mesh->Destroy();
								mesh->SetOwner(gameObject);
								mesh->SetRequiredAttributesFromMaterialID(meshMatID);
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
								MaterialID meshMatID = mesh->GetMaterialID();
								DestroyRenderObject(gameObject->GetRenderID());
								gameObject->SetRenderID(InvalidRenderID);
								mesh->Destroy();
								mesh->SetOwner(gameObject);
								mesh->SetRequiredAttributesFromMaterialID(meshMatID);
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
					for (i32 i = 0; i < (i32)MeshComponent::PrefabShape::_NONE; ++i)
					{
						std::string shapeStr = MeshComponent::PrefabShapeToString((MeshComponent::PrefabShape)i);
						bool bSelected = (selectedMeshIndex == i);
						if (ImGui::Selectable(shapeStr.c_str(), &bSelected))
						{
							if (selectedMeshIndex != i)
							{
								selectedMeshIndex = i;
								MaterialID meshMatID = mesh->GetMaterialID();
								DestroyRenderObject(gameObject->GetRenderID());
								gameObject->SetRenderID(InvalidRenderID);
								mesh->Destroy();
								mesh->SetOwner(gameObject);
								mesh->SetRequiredAttributesFromMaterialID(meshMatID);
								mesh->LoadPrefabShape((MeshComponent::PrefabShape)i);
							}
						}
					}

					ImGui::EndCombo();
				}
			} break;
			case MeshComponent::Type::PROCEDURAL:
			{
				ImGui::Text("Procedural mesh");
				ImGui::Text("Vertex count: %u", mesh->GetVertexBufferData()->VertexCount);
			} break;
			default:
			{
				PrintError("Unhandled MeshComponent::Type in Renderer::DrawImGuiForGameObject: %d\n", (i32)meshType);
				assert(false);
			} break;
			}
		}
	}

	void Renderer::OnPostSceneChange()
	{
		m_PhysicsDebugDrawer->OnPostSceneChange();
	}

	void Renderer::LoadShaders()
	{
		if (m_BaseShaders.empty())
		{
#if COMPILE_OPEN_GL
			m_BaseShaders = {
				{ "deferred_combine", "deferred_combine.vert", "deferred_combine.frag" },
				//{ "deferred_combine_cubemap", "deferred_combine_cubemap.vert", "deferred_combine_cubemap.frag" },
				{ "color", "color.vert", "color.frag" },
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
			};
#elif COMPILE_VULKAN
			m_BaseShaders = {
				{ "deferred_combine", "vk_deferred_combine_vert.spv", "vk_deferred_combine_frag.spv" },
				//{ "deferred_combine_cubemap", "vk_deferred_combine_cubemap_vert.spv", "vk_deferred_combine_cubemap_frag.spv" },
				{ "color", "vk_color_vert.spv","vk_color_frag.spv" },
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
				{ "post_fxaa", "vk_post_fxaa_vert.spv", "vk_post_fxaa_frag.spv" },
				{ "compute_sdf", "vk_compute_sdf_vert.spv", "vk_compute_sdf_frag.spv" },
				{ "font_ss", "vk_font_ss_vert.spv", "vk_font_frag.spv", "vk_font_ss_geom.spv" },
				{ "font_ws", "vk_font_ws_vert.spv", "vk_font_frag.spv", "vk_font_ws_geom.spv" },
				{ "shadow", "vk_shadow_vert.spv" },
				{ "ssao", "vk_ssao_vert.spv", "vk_ssao_frag.spv" },
				{ "ssao_blur", "vk_post_fxaa_vert.spv", "vk_ssao_blur_frag.spv" },
				{ "taa_resolve", "vk_post_fxaa_vert.spv", "vk_taa_resolve_frag.spv" },
				{ "gamma_correct", "vk_post_fxaa_vert.spv", "vk_gamma_correct_frag.spv" },
				{ "spherical_harmonic_visualization", "vk_spherical_harmonic_visualization_vert.spv", "vk_spherical_harmonic_visualization_frag.spv" },
			};
#endif

			ShaderID shaderID = 0;

			// Deferred combine
			m_BaseShaders[shaderID].renderPassType = RenderPassType::DEFERRED_COMBINE;
			m_BaseShaders[shaderID].bDeferred = false;
			m_BaseShaders[shaderID].bDepthWriteEnable = false;
			m_BaseShaders[shaderID].bNeedBRDFLUT = true;
			m_BaseShaders[shaderID].bNeedShadowMap = true;
			m_BaseShaders[shaderID].bNeedIrradianceSampler = true;
			m_BaseShaders[shaderID].bNeedPrefilteredMap = true;
			m_BaseShaders[shaderID].bNeedDepthSampler = true;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION_2D |
				(u32)VertexAttribute::UV;

			// TODO: Specify that this buffer is only used in the frag shader here
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_CAM_POS);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_VIEW_INV);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_PROJECTION_INV);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_DIR_LIGHT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_POINT_LIGHTS);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SHADOW_SAMPLING_DATA);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SSAO_SAMPLING_DATA);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_NEAR_FAR_PLANES);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_IRRADIANCE_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_FB_0_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_FB_1_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_DEPTH_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SSAO_FINAL_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_BRDF_LUT_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SHADOW_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_PREFILTER_MAP);

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_IRRADIANCE_SAMPLER);
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_IRRADIANCE_SAMPLER); // TODO
			++shaderID;

			//// Deferred combine cubemap
			//m_BaseShaders[shaderID].renderPassType = RenderPassType::DEFERRED_COMBINE;
			//m_BaseShaders[shaderID].bDeferred = false;
			//m_BaseShaders[shaderID].bDepthWriteEnable = false;
			//m_BaseShaders[shaderID].bNeedBRDFLUT = true;
			//m_BaseShaders[shaderID].bNeedDepthSampler = true;
			////m_BaseShaders[shaderID].bNeedShadowMap = true;
			//m_BaseShaders[shaderID].bNeedIrradianceSampler = true;
			//m_BaseShaders[shaderID].bNeedPrefilteredMap = true;
			//m_BaseShaders[shaderID].vertexAttributes =
			//	(u32)VertexAttribute::POSITION; // Used as 3D texture coord into cubemap

			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_VIEW);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_PROJECTION);
			////m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_LIGHT_VIEW_PROJS);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_CAM_POS);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_POINT_LIGHTS);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_DIR_LIGHT);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_IRRADIANCE_SAMPLER);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_CUBEMAP_SAMPLER);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_PREFILTER_MAP);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_BRDF_LUT_SAMPLER);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_FB_0_SAMPLER);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_FB_1_SAMPLER);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_DEPTH_SAMPLER);

			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_IRRADIANCE_SAMPLER);
			//++shaderID;

			// Color
			m_BaseShaders[shaderID].renderPassType = RenderPassType::FORWARD;
			m_BaseShaders[shaderID].bTranslucent = true;
			m_BaseShaders[shaderID].bDynamic = true;
			m_BaseShaders[shaderID].dynamicVertexBufferSize = 16384 *4*28; // TODO: FIXME:
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_COLOR_MULTIPLIER);
			++shaderID;

			// PBR
			m_BaseShaders[shaderID].renderPassType = RenderPassType::DEFERRED;
			m_BaseShaders[shaderID].numAttachments = 2; // TODO: Work out automatically from samplers?
			m_BaseShaders[shaderID].bDeferred = true;
			m_BaseShaders[shaderID].bNeedAlbedoSampler = true;
			m_BaseShaders[shaderID].bNeedMetallicSampler = true;
			m_BaseShaders[shaderID].bNeedRoughnessSampler = true;
			m_BaseShaders[shaderID].bNeedNormalSampler = true;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT |
				(u32)VertexAttribute::NORMAL |
				(u32)VertexAttribute::TANGENT;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_VIEW);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_CONST_ALBEDO);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ALBEDO_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_CONST_METALLIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_METALLIC_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_METALLIC_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_CONST_ROUGHNESS);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_ROUGHNESS_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ROUGHNESS_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_NORMAL_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_NORMAL_SAMPLER);
			++shaderID;

			// PBR - WORLD SPACE
			m_BaseShaders[shaderID].renderPassType = RenderPassType::DEFERRED;
			m_BaseShaders[shaderID].numAttachments = 2;
			m_BaseShaders[shaderID].bDeferred = true;
			m_BaseShaders[shaderID].bNeedMetallicSampler = true;
			m_BaseShaders[shaderID].bNeedRoughnessSampler = true;
			m_BaseShaders[shaderID].bNeedAlbedoSampler = true;
			m_BaseShaders[shaderID].bNeedNormalSampler = true;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::NORMAL |
				(u32)VertexAttribute::TANGENT;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_VIEW);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_CONST_ALBEDO);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ALBEDO_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_CONST_METALLIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_METALLIC_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_METALLIC_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_CONST_ROUGHNESS);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_ROUGHNESS_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ROUGHNESS_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_NORMAL_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_NORMAL_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_TEXTURE_SCALE);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_BLEND_SHARPNESS);
			++shaderID;

			// Skybox
			m_BaseShaders[shaderID].renderPassType = RenderPassType::FORWARD;
			m_BaseShaders[shaderID].bNeedCubemapSampler = true;
			m_BaseShaders[shaderID].bNeedPushConstantBlock = true;
			m_BaseShaders[shaderID].pushConstantBlockSize = 128;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_VIEW);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_PROJECTION);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_EXPOSURE);
			//m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_TIME);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_CUBEMAP_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms = {};
			++shaderID;

			// Equirectangular to Cube
			m_BaseShaders[shaderID].renderPassType = RenderPassType::FORWARD;
			m_BaseShaders[shaderID].bNeedHDREquirectangularSampler = true;
			m_BaseShaders[shaderID].bNeedPushConstantBlock = true;
			m_BaseShaders[shaderID].pushConstantBlockSize = 128;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_HDR_EQUIRECTANGULAR_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms = {};
			++shaderID;

			// Irradiance
			m_BaseShaders[shaderID].renderPassType = RenderPassType::FORWARD;
			m_BaseShaders[shaderID].bNeedCubemapSampler = true;
			m_BaseShaders[shaderID].bNeedPushConstantBlock = true;
			m_BaseShaders[shaderID].pushConstantBlockSize = 128;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_CUBEMAP_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms = {};
			++shaderID;

			// Prefilter
			m_BaseShaders[shaderID].renderPassType = RenderPassType::FORWARD;
			m_BaseShaders[shaderID].bNeedCubemapSampler = true;
			m_BaseShaders[shaderID].bNeedPushConstantBlock = true;
			m_BaseShaders[shaderID].pushConstantBlockSize = 128;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_CUBEMAP_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_CONST_ROUGHNESS);
			++shaderID;

			// BRDF
			m_BaseShaders[shaderID].renderPassType = RenderPassType::FORWARD;
			m_BaseShaders[shaderID].vertexAttributes = 0;

			m_BaseShaders[shaderID].constantBufferUniforms = {};

			m_BaseShaders[shaderID].dynamicBufferUniforms = {};
			++shaderID;

			// Sprite
			m_BaseShaders[shaderID].bNeedPushConstantBlock = true;
			m_BaseShaders[shaderID].pushConstantBlockSize = 132;
			m_BaseShaders[shaderID].bTranslucent = true;
			m_BaseShaders[shaderID].renderPassType = RenderPassType::UI;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_BaseShaders[shaderID].constantBufferUniforms = {};

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_COLOR_MULTIPLIER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ALBEDO_SAMPLER);
			++shaderID;

			// Sprite - Texture Array
			m_BaseShaders[shaderID].bNeedPushConstantBlock = true;
			m_BaseShaders[shaderID].pushConstantBlockSize = 132;
			m_BaseShaders[shaderID].bTranslucent = true;
			m_BaseShaders[shaderID].bTextureArr = true;
			m_BaseShaders[shaderID].bDynamic = true;
			m_BaseShaders[shaderID].dynamicVertexBufferSize = 1024 * 1024; // TODO: FIXME:
			m_BaseShaders[shaderID].renderPassType = RenderPassType::UI;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_BaseShaders[shaderID].constantBufferUniforms = {};

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_COLOR_MULTIPLIER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ALBEDO_SAMPLER);
			++shaderID;

			// Post processing
			m_BaseShaders[shaderID].renderPassType = RenderPassType::POST_PROCESS;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION_2D |
				(u32)VertexAttribute::UV;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SCENE_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_COLOR_MULTIPLIER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_POST_PROCESS_MAT);
			++shaderID;

			// Post FXAA
			m_BaseShaders[shaderID].renderPassType = RenderPassType::FORWARD; // TODO: FIXME:
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION_2D |
				(u32)VertexAttribute::UV;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_FXAA_DATA);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SCENE_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms = {};
			++shaderID;

			// Compute SDF
			m_BaseShaders[shaderID].renderPassType = RenderPassType::DEFERRED;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_BaseShaders[shaderID].constantBufferUniforms = {};

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_SDF_DATA);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_TEX_CHANNEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_HIGH_RES_TEX); // highResTex
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_CHAR_RESOLUTION);
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_SPREAD);
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_HIGH_RES_TEX);
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_TEX_CHANNEL);
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_SDF_RESOLUTION);
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_HIGH_RES);
			++shaderID;

			// Font SS
			m_BaseShaders[shaderID].renderPassType = RenderPassType::UI;
			m_BaseShaders[shaderID].bDynamic = true;
			m_BaseShaders[shaderID].dynamicVertexBufferSize = 1024 * 1024; // TODO: FIXME:
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION_2D |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT |
				(u32)VertexAttribute::EXTRA_VEC4 |
				(u32)VertexAttribute::EXTRA_INT;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_ALBEDO_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_FONT_CHAR_DATA);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_TEX_SIZE);
			++shaderID;

			// Font WS
			m_BaseShaders[shaderID].renderPassType = RenderPassType::FORWARD;
			m_BaseShaders[shaderID].bDynamic = true;
			m_BaseShaders[shaderID].dynamicVertexBufferSize = 1024 * 1024; // TODO: FIXME:
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT |
				(u32)VertexAttribute::TANGENT |
				(u32)VertexAttribute::EXTRA_VEC4 |
				(u32)VertexAttribute::EXTRA_INT;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_ALBEDO_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_FONT_CHAR_DATA);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_TEX_SIZE);
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_THRESHOLD);
			//m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_SHADOW);
			++shaderID;

			// Shadow
			m_BaseShaders[shaderID].renderPassType = RenderPassType::SHADOW;
			m_BaseShaders[shaderID].bGenerateVertexBufferForAll = true;
			m_BaseShaders[shaderID].bNeedPushConstantBlock = true;
			m_BaseShaders[shaderID].pushConstantBlockSize = 64;
			m_BaseShaders[shaderID].pushConstantsNeededInFragStage = true;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_BaseShaders[shaderID].constantBufferUniforms = {};

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			++shaderID;

			// SSAO
			m_BaseShaders[shaderID].renderPassType = RenderPassType::SSAO;
			m_BaseShaders[shaderID].bNeedDepthSampler = true;
			m_BaseShaders[shaderID].bNeedNoiseSampler = true;
			m_BaseShaders[shaderID].bDepthWriteEnable = false;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_PROJECTION);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_PROJECTION_INV);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SSAO_GEN_DATA);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_DEPTH_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SSAO_NORMAL_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_NOISE_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms = {};
			++shaderID;

			// SSAO Blur
			m_BaseShaders[shaderID].renderPassType = RenderPassType::SSAO_BLUR;
			m_BaseShaders[shaderID].bNeedDepthSampler = true;
			m_BaseShaders[shaderID].bDepthWriteEnable = false;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SSAO_BLUR_DATA_CONSTANT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SSAO_RAW_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SSAO_NORMAL_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_DEPTH_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_SSAO_BLUR_DATA_DYNAMIC);
			++shaderID;

			// TAA Resolve
			m_BaseShaders[shaderID].renderPassType = RenderPassType::TAA_RESOLVE;
			m_BaseShaders[shaderID].bDepthWriteEnable = false;
			m_BaseShaders[shaderID].bNeedPushConstantBlock = true;
			m_BaseShaders[shaderID].pushConstantsNeededInFragStage = true;
			m_BaseShaders[shaderID].pushConstantBlockSize = 8;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_VIEW_INV);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_PROJECTION_INV);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_LAST_FRAME_VIEWPROJ);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SCENE_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_HISTORY_SAMPLER);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_DEPTH_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms = {};
			++shaderID;

			// Gamma Correct
			m_BaseShaders[shaderID].renderPassType = RenderPassType::GAMMA_CORRECT;
			m_BaseShaders[shaderID].bDepthWriteEnable = false;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_SCENE_SAMPLER);

			m_BaseShaders[shaderID].dynamicBufferUniforms = {};
			++shaderID;

			// Spherical Harmonic Visualizer
			m_BaseShaders[shaderID].renderPassType = RenderPassType::FORWARD;
			m_BaseShaders[shaderID].vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::NORMAL |
				(u32)VertexAttribute::TANGENT;

			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_VIEW);
			m_BaseShaders[shaderID].constantBufferUniforms.AddUniform(U_PROJECTION);

			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_MODEL);
			m_BaseShaders[shaderID].dynamicBufferUniforms.AddUniform(U_SH_COEFFS);
			++shaderID;

			assert(shaderID == m_BaseShaders.size());
		}

		SetShaderCount(m_BaseShaders.size());

		for (u32 shaderID = 0; shaderID < m_BaseShaders.size(); ++shaderID)
		{
			Shader& shader = m_BaseShaders[shaderID];

			// Sanity checks
			{
				assert(!shader.constantBufferUniforms.HasUniform(U_UNIFORM_BUFFER_DYNAMIC));
				assert(!shader.dynamicBufferUniforms.HasUniform(U_UNIFORM_BUFFER_CONSTANT));

				assert((shader.bNeedPushConstantBlock && shader.pushConstantBlockSize != 0) ||
					  (!shader.bNeedPushConstantBlock && shader.pushConstantBlockSize == 0));


				if (shader.constantBufferUniforms.HasUniform(U_HIGH_RES_TEX))
				{
					assert(!shader.constantBufferUniforms.HasUniform(U_ALBEDO_SAMPLER));
				}
			}

			if (!LoadShaderCode(shaderID))
			{
				PrintError("Couldn't load/compile shaders: %s", shader.vertexShaderFilePath.c_str());
				if (!shader.fragmentShaderFilePath.empty())
				{
					PrintError(", %s", shader.fragmentShaderFilePath.c_str());
				}
				if (!shader.geometryShaderFilePath.empty())
				{
					PrintError(", %s", shader.geometryShaderFilePath.c_str());
				}
				PrintError("\n");
			}

			// TODO: Clear out at some point?
			//shader.vertexShaderCode.clear();
			//shader.vertexShaderCode.shrink_to_fit();
			//shader.fragmentShaderCode.clear();
			//shader.fragmentShaderCode.shrink_to_fit();
			//shader.geometryShaderCode.clear();
			//shader.geometryShaderCode.shrink_to_fit();
		}
	}

	void Renderer::DoCreateGameObjectButton(const char* buttonName, const char* popupName)
	{
		static const char* defaultNewNameBase = "New_Object_";

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


			bool bCreate = ImGui::InputText("##new-object-name",
				(char*)newObjectName.data(),
				maxStrLen,
				ImGuiInputTextFlags_EnterReturnsTrue);

			bCreate |= ImGui::Button("Create");

			bool bInvalidName = std::string(newObjectName.c_str()).empty();

			if (bCreate && !bInvalidName)
			{
				// Remove excess trailing \0 chars
				newObjectName = std::string(newObjectName.c_str());

				if (!newObjectName.empty())
				{
					GameObject* newGameObject = new GameObject(newObjectName, GameObjectType::OBJECT);

					newGameObject->SetMeshComponent(new MeshComponent(newGameObject));
					newGameObject->GetMeshComponent()->LoadFromFile(RESOURCE_LOCATION  "meshes/cube.glb");

					g_SceneManager->CurrentScene()->AddRootObject(newGameObject);

					newGameObject->Initialize();
					newGameObject->PostInitialize();

					g_Editor->SetSelectedObject(newGameObject);

					ImGui::CloseCurrentPopup();
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	bool Renderer::DrawImGuiGameObjectNameAndChildren(GameObject* gameObject)
	{
		bool bParentChildTreeDirty = false;

		RenderID renderID = gameObject->GetRenderID();
		std::string objectName = gameObject->GetName();
		std::string objectID;
		if (renderID == InvalidRenderID)
		{
			objectID = "##" + objectName;
		}
		else
		{
			objectID = "##" + std::to_string(renderID);

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
				if (child->IsVisibleInSceneExplorer(true))
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
		bool bSelected = g_Editor->IsObjectSelected(gameObject);

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

		gameObject->DoImGuiContextMenu(false);

		if (gameObject == nullptr)
		{
			bParentChildTreeDirty = true;
		}
		else
		{
			// TODO: Remove from renderer class
			if (ImGui::IsMouseReleased(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_None))
			{
				if (g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_CONTROL))
				{
					g_Editor->ToggleSelectedObject(gameObject);
				}
				else if (g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT))
				{
					const std::vector<GameObject*>& selectedObjects = g_Editor->GetSelectedObjects();
					if (selectedObjects.empty() ||
						(selectedObjects.size() == 1 && selectedObjects[0] == gameObject))
					{
						g_Editor->ToggleSelectedObject(gameObject);
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
							g_Editor->AddSelectedObject(objectToSelect);
						}
					}
				}
				else
				{
					g_Editor->SetSelectedObject(gameObject);
				}
			}

			if (ImGui::IsItemActive())
			{
				if (ImGui::BeginDragDropSource())
				{
					const void* data = nullptr;
					size_t size = 0;

					const std::vector<GameObject*>& selectedObjects = g_Editor->GetSelectedObjects();
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
							g_Editor->AddSelectedObject(draggedGameObject);
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
						g_Editor->SetSelectedObject(gameObject);

						data = (void*)(&gameObject);
						size = sizeof(GameObject*);
						dragDropText = gameObject->GetName();
					}

					ImGui::SetDragDropPayload(m_GameObjectPayloadCStr, data, size);

					ImGui::Text("%s", dragDropText.c_str());

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
										bParentChildTreeDirty = true;
									}
								}
								else
								{
									g_SceneManager->CurrentScene()->RemoveRootObject(draggedGameObject, false);
									gameObject->AddChild(draggedGameObject);
									bParentChildTreeDirty = true;
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
			if (!bParentChildTreeDirty && gameObject)
			{
				ImGui::Indent();
				// Don't cache results since children can change during this recursive call
				for (GameObject* child : gameObject->GetChildren())
				{
					if (DrawImGuiGameObjectNameAndChildren(child))
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

		return bParentChildTreeDirty;
	}

	void Renderer::GenerateGBuffer()
	{
		if (m_gBufferQuadVertexBufferData.vertexData == nullptr)
		{
			GenerateGBufferVertexBuffer(g_bVulkanEnabled);
		}

		assert(m_SkyBoxMesh != nullptr);
		MaterialID skyboxMaterialID = m_SkyBoxMesh->GetMeshComponent()->GetMaterialID();

		const std::string gBufferMatName = "GBuffer material";
		const std::string gBufferCubeMatName = "GBuffer cubemap material";
		const std::string gBufferQuadName = "GBuffer quad";

		// Remove existing material if present (this will be true when reloading the scene)
		{
			MaterialID existingGBufferQuadMatID = InvalidMaterialID;
			MaterialID existingGBufferCubeMatID = InvalidMaterialID;
			// TODO: Don't rely on material names!
			if (GetMaterialID(gBufferMatName, existingGBufferQuadMatID))
			{
				RemoveMaterial(existingGBufferQuadMatID);
			}
			if (GetMaterialID(gBufferCubeMatName, existingGBufferCubeMatID))
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
			}
		}

		{
			MaterialCreateInfo gBufferMaterialCreateInfo = {};
			gBufferMaterialCreateInfo.name = gBufferMatName;
			gBufferMaterialCreateInfo.shaderName = "deferred_combine";
			gBufferMaterialCreateInfo.enableIrradianceSampler = true;
			gBufferMaterialCreateInfo.irradianceSamplerMatID = skyboxMaterialID;
			gBufferMaterialCreateInfo.enablePrefilteredMap = true;
			gBufferMaterialCreateInfo.prefilterMapSamplerMatID = skyboxMaterialID;
			gBufferMaterialCreateInfo.enableBRDFLUT = true;
			gBufferMaterialCreateInfo.renderToCubemap = false;
			gBufferMaterialCreateInfo.persistent = true;
			gBufferMaterialCreateInfo.visibleInEditor = false;
			FillOutGBufferFrameBufferAttachments(gBufferMaterialCreateInfo.sampledFrameBuffers);

			MaterialID gBufferMatID = InitializeMaterial(&gBufferMaterialCreateInfo);

			GameObject* gBufferQuadGameObject = new GameObject(gBufferQuadName, GameObjectType::_NONE);
			m_PersistentObjects.push_back(gBufferQuadGameObject);
			// NOTE: G-buffer isn't rendered normally, it is handled separately
			gBufferQuadGameObject->SetVisible(false);

			RenderObjectCreateInfo gBufferQuadCreateInfo = {};
			gBufferQuadCreateInfo.materialID = gBufferMatID;
			gBufferQuadCreateInfo.gameObject = gBufferQuadGameObject;
			gBufferQuadCreateInfo.vertexBufferData = &m_gBufferQuadVertexBufferData;
			gBufferQuadCreateInfo.cullFace = CullFace::NONE;
			gBufferQuadCreateInfo.visibleInSceneExplorer = false;
			gBufferQuadCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
			gBufferQuadCreateInfo.bDepthWriteEnable = false;
			gBufferQuadCreateInfo.bSetDynamicStates = true;

			m_GBufferQuadRenderID = InitializeRenderObject(&gBufferQuadCreateInfo);

			m_gBufferQuadVertexBufferData.DescribeShaderVariables(this, m_GBufferQuadRenderID);
		}

		// Initialize GBuffer cubemap material & mesh
		//{
		//	MaterialCreateInfo gBufferCubemapMaterialCreateInfo = {};
		//	gBufferCubemapMaterialCreateInfo.name = gBufferCubeMatName;
		//	gBufferCubemapMaterialCreateInfo.shaderName = "deferred_combine_cubemap";
		//	gBufferCubemapMaterialCreateInfo.enableIrradianceSampler = true;
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
		static const glm::vec4 color(0.95f);
		DrawStringSS("FLEX ENGINE", color, AnchorPoint::TOP_RIGHT, glm::vec2(-0.03f, -0.055f), 1.0f, 0.6f);
		if (g_EngineInstance->IsSimulationPaused())
		{
			DrawStringSS("PAUSED", color, AnchorPoint::TOP_RIGHT, glm::vec2(-0.03f, -0.09f), 0.0f);
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
			real alpha = glm::clamp(m_EditorStrSecRemaining / (m_EditorStrSecDuration*m_EditorStrFadeDurationPercent),
				0.0f, 1.0f);
			DrawStringSS(m_EditorMessage, glm::vec4(1.0f, 1.0f, 1.0f, alpha), AnchorPoint::CENTER, VEC2_ZERO, 3);
		}
	}

	void Renderer::EnqueueWorldSpaceText()
	{
		SetFont(SID("editor-02-ws"));
		real s = g_SecElapsedSinceProgramStart * 3.5f;
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(1.0f), 1.0f), glm::vec3(2.0f, 5.0f, 0.0f), QUAT_UNIT, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.95f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 1) * 0.05f, 5.0f + sin(s + 0.3f * 1) * 0.05f, -0.075f * 1), QUAT_UNIT, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.90f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 2) * 0.07f, 5.0f + sin(s + 0.3f * 2) * 0.07f, -0.075f * 2), QUAT_UNIT, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.85f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 3) * 0.10f, 5.0f + sin(s + 0.3f * 3) * 0.10f, -0.075f * 3), QUAT_UNIT, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.80f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 4) * 0.12f, 5.0f + sin(s + 0.3f * 4) * 0.12f, -0.075f * 4), QUAT_UNIT, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.75f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 5) * 0.15f, 5.0f + sin(s + 0.3f * 5) * 0.15f, -0.075f * 5), QUAT_UNIT, 0.0f, 100.0f);
		DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.70f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 6) * 0.17f, 5.0f + sin(s + 0.3f * 6) * 0.17f, -0.075f * 6), QUAT_UNIT, 0.0f, 100.0f);
	}

	bool Renderer::LoadFontMetrics(const std::vector<char>& fileMemory,
		FT_Library& ft,
		FontMetaData& metaData,
		std::map<i32, FontMetric*>* outCharacters,
		std::array<glm::vec2i, 4>* outMaxPositions,
		FT_Face* outFace)
	{
		assert(metaData.bitmapFont == nullptr);

		// TODO: Save in common place
		u32 sampleDensity = 32;

		FT_Error error = FT_New_Memory_Face(ft, (FT_Byte*)fileMemory.data(), (FT_Long)fileMemory.size(), 0, outFace);
		FT_Face& face = *outFace;
		if (error == FT_Err_Unknown_File_Format)
		{
			PrintError("Unhandled font file format: %s\n", metaData.filePath.c_str());
			return false;
		}
		else if (error != FT_Err_Ok || !face)
		{
			PrintError("Failed to create new font face: %s\n", metaData.filePath.c_str());
			return false;
		}

		error = FT_Set_Char_Size(face,
			0, metaData.size * sampleDensity,
			(FT_UInt)g_Monitor->DPI.x,
			(FT_UInt)g_Monitor->DPI.y);

		if (g_bEnableLogging_Loading)
		{
			const std::string fileName = StripLeadingDirectories(metaData.filePath);
			Print("Loaded font file %s\n", fileName.c_str());
		}

		std::string fontName = std::string(face->family_name) + " - " + face->style_name;
		metaData.bitmapFont = new BitmapFont(metaData, fontName, face->num_glyphs);
		BitmapFont* newFont = metaData.bitmapFont;

		if (metaData.bScreenSpace)
		{
			m_FontsSS.push_back(newFont);
		}
		else
		{
			m_FontsWS.push_back(newFont);
		}

		//newFont->SetUseKerning(FT_HAS_KERNING(face) != 0);

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

			if (newFont->bUseKerning && glyphIndex)
			{
				for (i32 previous = 0; previous < BitmapFont::CHAR_COUNT - 1; ++previous)
				{
					FT_Vector delta;

					u32 prevIdx = FT_Get_Char_Index(face, previous);
					FT_Get_Kerning(face, prevIdx, glyphIndex, FT_KERNING_DEFAULT, &delta);

					if (delta.x != 0 || delta.y != 0)
					{
						std::wstring charKey(std::wstring(1, (wchar_t)previous) + std::wstring(1, (wchar_t)c));
						metric->kerning[charKey] =
							glm::vec2((real)delta.x / 64.0f, (real)delta.y / 64.0f);
					}
				}
			}

			if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER))
			{
				PrintError("Failed to load glyph with index %u\n", glyphIndex);
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

			(*outCharacters)[c] = metric;
		}

		(*outMaxPositions)[0] = maxPos[0];
		(*outMaxPositions)[1] = maxPos[1];
		(*outMaxPositions)[2] = maxPos[2];
		(*outMaxPositions)[3] = maxPos[3];
		*outFace = face;

		return true;
	}

	void Renderer::InitializeMaterials()
	{
		MaterialCreateInfo spriteMatSSCreateInfo = {};
		spriteMatSSCreateInfo.name = "Sprite material";
		spriteMatSSCreateInfo.shaderName = "sprite";
		spriteMatSSCreateInfo.persistent = true;
		spriteMatSSCreateInfo.visibleInEditor = true;
		spriteMatSSCreateInfo.enableAlbedoSampler = true;
		m_SpriteMatSSID = InitializeMaterial(&spriteMatSSCreateInfo);

		MaterialCreateInfo spriteMatWSCreateInfo = {};
		spriteMatWSCreateInfo.name = "Sprite material";
		spriteMatWSCreateInfo.shaderName = "sprite";
		spriteMatWSCreateInfo.persistent = true;
		spriteMatWSCreateInfo.visibleInEditor = true;
		spriteMatWSCreateInfo.enableAlbedoSampler = true;
		m_SpriteMatWSID = InitializeMaterial(&spriteMatWSCreateInfo);

		MaterialCreateInfo spriteArrMatCreateInfo = {};
		spriteArrMatCreateInfo.name = "Sprite Texture Array material";
		spriteArrMatCreateInfo.shaderName = "sprite_arr";
		spriteArrMatCreateInfo.persistent = true;
		spriteArrMatCreateInfo.visibleInEditor = true;
		spriteArrMatCreateInfo.enableAlbedoSampler = true;
		m_SpriteArrMatID = InitializeMaterial(&spriteArrMatCreateInfo);

		MaterialCreateInfo fontSSMatCreateInfo = {};
		fontSSMatCreateInfo.name = "font ss";
		fontSSMatCreateInfo.shaderName = "font_ss";
		fontSSMatCreateInfo.persistent = true;
		fontSSMatCreateInfo.visibleInEditor = false;
		m_FontMatSSID = InitializeMaterial(&fontSSMatCreateInfo);

		MaterialCreateInfo fontWSMatCreateInfo = {};
		fontWSMatCreateInfo.name = "font ws";
		fontWSMatCreateInfo.shaderName = "font_ws";
		fontWSMatCreateInfo.persistent = true;
		fontWSMatCreateInfo.visibleInEditor = false;
		m_FontMatWSID = InitializeMaterial(&fontWSMatCreateInfo);

		MaterialCreateInfo shadowMatCreateInfo = {};
		shadowMatCreateInfo.name = "shadow";
		shadowMatCreateInfo.shaderName = "shadow";
		shadowMatCreateInfo.persistent = true;
		shadowMatCreateInfo.visibleInEditor = false;
		m_ShadowMaterialID = InitializeMaterial(&shadowMatCreateInfo);

		MaterialCreateInfo postProcessMatCreateInfo = {};
		postProcessMatCreateInfo.name = "Post process material";
		postProcessMatCreateInfo.shaderName = "post_process";
		postProcessMatCreateInfo.persistent = true;
		postProcessMatCreateInfo.visibleInEditor = false;
		m_PostProcessMatID = InitializeMaterial(&postProcessMatCreateInfo);

		MaterialCreateInfo postFXAAMatCreateInfo = {};
		postFXAAMatCreateInfo.name = "fxaa";
		postFXAAMatCreateInfo.shaderName = "post_fxaa";
		postFXAAMatCreateInfo.persistent = true;
		postFXAAMatCreateInfo.visibleInEditor = false;
		m_PostFXAAMatID = InitializeMaterial(&postFXAAMatCreateInfo);

		MaterialCreateInfo selectedObjectMatCreateInfo = {};
		selectedObjectMatCreateInfo.name = "Selected Object";
		selectedObjectMatCreateInfo.shaderName = "color";
		selectedObjectMatCreateInfo.persistent = true;
		selectedObjectMatCreateInfo.visibleInEditor = false;
		selectedObjectMatCreateInfo.colorMultiplier = VEC4_ONE;
		m_SelectedObjectMatID = InitializeMaterial(&selectedObjectMatCreateInfo);

		MaterialCreateInfo taaMatCreateInfo = {};
		taaMatCreateInfo.name = "TAA Resolve";
		taaMatCreateInfo.shaderName = "taa_resolve";
		taaMatCreateInfo.persistent = true;
		taaMatCreateInfo.visibleInEditor = false;
		taaMatCreateInfo.colorMultiplier = VEC4_ONE;
		m_TAAResolveMaterialID = InitializeMaterial(&taaMatCreateInfo);
		
		MaterialCreateInfo gammaCorrectMatCreateInfo = {};
		gammaCorrectMatCreateInfo.name = "Gamma Correct";
		gammaCorrectMatCreateInfo.shaderName = "gamma_correct";
		gammaCorrectMatCreateInfo.persistent = true;
		gammaCorrectMatCreateInfo.visibleInEditor = false;
		gammaCorrectMatCreateInfo.colorMultiplier = VEC4_ONE;
		m_GammaCorrectMaterialID = InitializeMaterial(&gammaCorrectMatCreateInfo);

		MaterialCreateInfo placeholderMatCreateInfo = {};
		placeholderMatCreateInfo.name = "placeholder";
		placeholderMatCreateInfo.shaderName = "pbr";
		placeholderMatCreateInfo.persistent = true;
		placeholderMatCreateInfo.visibleInEditor = true;
		placeholderMatCreateInfo.constAlbedo = glm::vec3(1.0f, 0.0f, 1.0f);
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
	void Renderer::UpdateTextBufferSS(std::vector<TextVertex2D>& outTextVertices)
	{
		PROFILE_AUTO("Update Text Buffer SS");

		glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;

		u32 charCountUpperBound = 0;
		for (BitmapFont* font : m_FontsWS)
		{
			const std::vector<TextCache>& caches = font->GetTextCaches();
			for (const TextCache& textCache : caches)
			{
				charCountUpperBound += textCache.str.length();
			}
		}
		outTextVertices.reserve(charCountUpperBound);

		const real frameBufferScale = glm::max(2.0f / (real)frameBufferSize.x, 2.0f / (real)frameBufferSize.y);

		for (BitmapFont* font : m_FontsSS)
		{
			real baseTextScale = frameBufferScale * (font->metaData.size / 12.0f);

			font->bufferStart = (i32)(outTextVertices.size());

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
							vert.color = textCache.color;
							vert.charSizePixelsCharSizeNorm = charSizePixelsCharSizeNorm;
							vert.channel = texChannel;

							outTextVertices.push_back(vert);

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

			font->bufferSize = (i32)outTextVertices.size() - font->bufferStart;
			font->ClearCaches();
		}
	}

	void Renderer::UpdateTextBufferWS(std::vector<TextVertex3D>& outTextVertices)
	{
		// TODO: Consolidate with UpdateTextBufferSS

		PROFILE_AUTO("Update Text Buffer WS");

		const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		const real frameBufferScale = glm::max(2.0f / (real)frameBufferSize.x, 2.0f / (real)frameBufferSize.y);

		u32 charCountUpperBound = 0;
		for (BitmapFont* font : m_FontsWS)
		{
			const std::vector<TextCache>& caches = font->GetTextCaches();
			for (const TextCache& textCache : caches)
			{
				charCountUpperBound += textCache.str.length();
			}
		}
		outTextVertices.reserve(charCountUpperBound);

		for (BitmapFont* font : m_FontsWS)
		{
			real textScale = frameBufferScale * (font->metaData.size / 12.0f);

			font->bufferStart = (i32)(outTextVertices.size());

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
							vert.color = textCache.color;
							vert.tangent = tangent;
							vert.charSizePixelsCharSizeNorm = charSizePixelsCharSizeNorm;
							vert.channel = texChannel;

							outTextVertices.push_back(vert);

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

			font->bufferSize = (i32)outTextVertices.size() - font->bufferStart;
			font->ClearCaches();
		}
	}

	glm::vec4 Renderer::GetSelectedObjectColorMultiplier() const
	{
		static const glm::vec4 color0 = { 0.95f, 0.95f, 0.95f, 0.4f };
		static const glm::vec4 color1 = { 0.85f, 0.15f, 0.85f, 0.4f };
		static const real pulseSpeed = 8.0f;
		return Lerp(color0, color1, sin(g_SecElapsedSinceProgramStart * pulseSpeed) * 0.5f + 0.5f);
	}

	glm::mat4 Renderer::GetPostProcessingMatrix() const
	{
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

	void Renderer::GenerateGBufferVertexBuffer(bool bFlipV)
	{
		if (m_gBufferQuadVertexBufferData.vertexData == nullptr)
		{
			VertexBufferDataCreateInfo gBufferQuadVertexBufferDataCreateInfo = {};

			gBufferQuadVertexBufferDataCreateInfo.positions_3D = {
				glm::vec3(-1.0f, -1.0f, 0.0f),
				glm::vec3(-1.0f, 3.0f,  0.0f),
				glm::vec3(3.0f,  -1.0f, 0.0f),
			};

			gBufferQuadVertexBufferDataCreateInfo.texCoords_UV = {
				glm::vec2(0.0f, bFlipV ? 1.0f  : 0.0f),
				glm::vec2(0.0f, bFlipV ? -1.0f : 2.0f),
				glm::vec2(2.0f, bFlipV ? 1.0f  : 0.0f),
			};

			gBufferQuadVertexBufferDataCreateInfo.attributes = (u32)VertexAttribute::POSITION | (u32)VertexAttribute::UV;
			m_gBufferQuadVertexBufferData.Initialize(&gBufferQuadVertexBufferDataCreateInfo);
		}
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

	void PhysicsDebugDrawBase::UpdateDebugMode()
	{
		const PhysicsDebuggingSettings& settings = g_Renderer->GetPhysicsDebuggingSettings();

		m_DebugMode =
			(settings.DisableAll ? DBG_NoDebug : 0) |
			(settings.DrawWireframe ? DBG_DrawWireframe : 0) |
			(settings.DrawAabb ? DBG_DrawAabb : 0) |
			(settings.DrawFeaturesText ? DBG_DrawFeaturesText : 0) |
			(settings.DrawContactPoints ? DBG_DrawContactPoints : 0) |
			(settings.NoDeactivation ? DBG_NoDeactivation : 0) |
			(settings.NoHelpText ? DBG_NoHelpText : 0) |
			(settings.DrawText ? DBG_DrawText : 0) |
			(settings.ProfileTimings ? DBG_ProfileTimings : 0) |
			(settings.EnableSatComparison ? DBG_EnableSatComparison : 0) |
			(settings.DisableBulletLCP ? DBG_DisableBulletLCP : 0) |
			(settings.EnableCCD ? DBG_EnableCCD : 0) |
			(settings.DrawConstraints ? DBG_DrawConstraints : 0) |
			(settings.DrawConstraintLimits ? DBG_DrawConstraintLimits : 0) |
			(settings.FastWireframe ? DBG_FastWireframe : 0) |
			(settings.DrawNormals ? DBG_DrawNormals : 0) |
			(settings.DrawFrames ? DBG_DrawFrames : 0);
	}

	void PhysicsDebugDrawBase::ClearLines()
	{
		m_LineSegments.clear();
	}

	void PhysicsDebugDrawBase::flushLines()
	{
		Draw();
	}

} // namespace flex