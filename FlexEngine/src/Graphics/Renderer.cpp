#include "stdafx.hpp"

#include "Graphics/Renderer.hpp"

IGNORE_WARNINGS_PUSH
#if COMPILE_IMGUI
#include "imgui.h"
#include "imgui_internal.h"
#endif

#include <glm/gtx/quaternion.hpp> // for rotate

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/fttypes.h>
#include <freetype/fterrors.h>
IGNORE_WARNINGS_POP

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
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
	{
	}

	Renderer::~Renderer()
	{
	}

	void Renderer::Initialize()
	{
		// TODO: Handle changing DPI after this point
		// TODO: Save these strings in a config file?
		m_FontMetaDatas[0] = {
			RESOURCE_LOCATION  "fonts\\UbuntuCondensed-Regular.ttf",
			RESOURCE_LOCATION  "fonts\\UbuntuCondensed-Regular-24",
			24,
			true,
			&m_FntUbuntuCondensedSS,
		};

		m_FontMetaDatas[1] = {
			RESOURCE_LOCATION  "fonts\\SourceCodePro-regular.ttf",
			RESOURCE_LOCATION  "fonts\\SourceCodePro-regular-16",
			16,
			false,
			&m_FntSourceCodeProWS,
		};

		m_FontMetaDatas[2] = {
			RESOURCE_LOCATION  "fonts\\SourceCodePro-regular.ttf",
			RESOURCE_LOCATION  "fonts\\SourceCodePro-regular-14",
			14,
			true,
			&m_FntSourceCodeProSS,
		};

		//m_FontMetaDatas[3] = {
		//	RESOURCE_LOCATION  "fonts\\gant.ttf",
		//	RESOURCE_LOCATION  "fonts\\gant-regular-10",
		//	10,
		//	true,
		//	&m_FntGantSS,
		//};

		std::string DPIStr = FloatToString(g_Monitor->DPI.x, 0) + "DPI";
		for (i32 i = 0; i < FONT_COUNT; ++i)
		{
			m_FontMetaDatas[i].renderedTextureFilePath += "-" + DPIStr + m_FontImageExtension;
		}

		std::string hdriPath = RESOURCE("textures\\hdri\\");
		if (!FindFilesInDirectory(hdriPath, m_AvailableHDRIs, "hdr"))
		{
			PrintWarn("Unable to find hdri directory at %s\n", hdriPath.c_str());
		}

		m_PointLights = (PointLightData*)malloc(MAX_NUM_POINT_LIGHTS * sizeof(PointLightData));
		for (i32 i = 0; i < MAX_NUM_POINT_LIGHTS; ++i)
		{
			m_PointLights[i].color = VEC3_NEG_ONE;
			m_PointLights[i].enabled = 0;
		}
	}

	void Renderer::Destroy()
	{
		free(m_PointLights);
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

		m_Grid->SetVisible(bRenderGrid);
		m_WorldOrigin->SetVisible(bRenderGrid);
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

	void Renderer::SetPostProcessingEnabled(bool bEnabled)
	{
		m_bPostProcessingEnabled = bEnabled;
	}

	bool Renderer::IsPostProcessingEnabled() const
	{
		return m_bPostProcessingEnabled;
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
		return &m_DirectionalLight->data;
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

	void Renderer::SetFont(BitmapFont* font)
	{
		m_CurrentFont = font;
	}

	Renderer::PostProcessSettings& Renderer::GetPostProcessSettings()
	{
		return m_PostProcessSettings;
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

	void Renderer::DrawImGuiSettings()
	{
		static const char* rendererSettingsStr = "Renderer settings";
		if (ImGui::TreeNode(rendererSettingsStr))
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

			static const char* recaptureReflectionProbeStr = "Recapture reflection probe";
			if (ImGui::Button(recaptureReflectionProbeStr))
			{
				g_Renderer->RecaptureReflectionProbe();
			}

			bool bVSyncEnabled = g_Window->GetVSyncEnabled();
			static const char* vSyncEnabledStr = "VSync";
			if (ImGui::Checkbox(vSyncEnabledStr, &bVSyncEnabled))
			{
				g_Window->SetVSyncEnabled(bVSyncEnabled);
			}

			static const char* exposureControlStr = "Camera exposure";
			if (ImGui::TreeNode(exposureControlStr))
			{
				BaseCamera* currentCamera = g_CameraManager->CurrentCamera();

				ImGui::Text("Exposure: %.2f", currentCamera->exposure);

				ImGui::PushItemWidth(140.0f);
				{
					static const char* apertureStr = "Aperture (f-stops)";
					if (ImGui::SliderFloat(apertureStr, &currentCamera->aperture, 1.0f, 64.0f))
					{
						currentCamera->CalculateExposure();
					}

					static const char* shutterSpeedStr = "Shutter speed (1/s)";
					real shutterSpeedInv = 1.0f / currentCamera->shutterSpeed;
					if (ImGui::SliderFloat(shutterSpeedStr, &shutterSpeedInv, 1.0f, 500.0f))
					{
						currentCamera->shutterSpeed = 1.0f / shutterSpeedInv;
						currentCamera->CalculateExposure();
					}

					static const char* isoStr = "ISO";
					if (ImGui::SliderFloat(isoStr, &currentCamera->lightSensitivity, 100.0f, 6400.0f))
					{
						// Round to nearest power of 2 * 100
						currentCamera->lightSensitivity = pow(2.0f, ceil(log(currentCamera->lightSensitivity / 100.0f) / log(2.0f) - 0.5f)) * 100.0f;
						currentCamera->CalculateExposure();
					}
				}
				ImGui::PopItemWidth();

				ImGui::TreePop();
			}

			static const char* physicsDebuggingStr = "Debug objects";
			if (ImGui::TreeNode(physicsDebuggingStr))
			{
				PhysicsDebuggingSettings& physicsDebuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();

				static const char* disableAllStr = "Disable All";
				ImGui::Checkbox(disableAllStr, &physicsDebuggingSettings.DisableAll);

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

				static const char* displayBoundingVolumesStr = "Bounding volumes";
				bool bDisplayBoundingVolumes = g_Renderer->IsDisplayBoundingVolumesEnabled();
				if (ImGui::Checkbox(displayBoundingVolumesStr, &bDisplayBoundingVolumes))
				{
					g_Renderer->SetDisplayBoundingVolumesEnabled(bDisplayBoundingVolumes);
				}

				static const char* wireframeStr = "Wireframe (P)";
				ImGui::Checkbox(wireframeStr, &physicsDebuggingSettings.DrawWireframe);

				static const char* aabbStr = "AABB";
				ImGui::Checkbox(aabbStr, &physicsDebuggingSettings.DrawAabb);

				// Unused (for now):
				//static const char* drawFeaturesTextStr = "Draw Features Text";
				//ImGui::Checkbox(drawFeaturesTextStr, &physicsDebuggingSettings.DrawFeaturesText);

				//static const char* drawContactPointsStr = "Draw Contact Points";
				//ImGui::Checkbox(drawContactPointsStr, &physicsDebuggingSettings.DrawContactPoints);

				//static const char* noDeactivationStr = "No Deactivation";
				//ImGui::Checkbox(noDeactivationStr, &physicsDebuggingSettings.NoDeactivation);

				//static const char* noHelpTextStr = "No Help Text";
				//ImGui::Checkbox(noHelpTextStr, &physicsDebuggingSettings.NoHelpText);

				//static const char* drawTextStr = "Draw Text";
				//ImGui::Checkbox(drawTextStr, &physicsDebuggingSettings.DrawText);

				//static const char* profileTimingsStr = "Profile Timings";
				//ImGui::Checkbox(profileTimingsStr, &physicsDebuggingSettings.ProfileTimings);

				//static const char* satComparisonStr = "Sat Comparison";
				//ImGui::Checkbox(satComparisonStr, &physicsDebuggingSettings.EnableSatComparison);

				//static const char* disableBulletLCPStr = "Disable Bullet LCP";
				//ImGui::Checkbox(disableBulletLCPStr, &physicsDebuggingSettings.DisableBulletLCP);

				//static const char* ccdStr = "CCD";
				//ImGui::Checkbox(ccdStr, &physicsDebuggingSettings.EnableCCD);

				//static const char* drawConstraintsStr = "Draw Constraints";
				//ImGui::Checkbox(drawConstraintsStr, &physicsDebuggingSettings.DrawConstraints);

				//static const char* drawConstraintLimitsStr = "Draw Constraint Limits";
				//ImGui::Checkbox(drawConstraintLimitsStr, &physicsDebuggingSettings.DrawConstraintLimits);

				//static const char* fastWireframeStr = "Fast Wireframe";
				//ImGui::Checkbox(fastWireframeStr, &physicsDebuggingSettings.FastWireframe);

				//static const char* drawNormalsStr = "Draw Normals";
				//ImGui::Checkbox(drawNormalsStr, &physicsDebuggingSettings.DrawNormals);

				//static const char* drawFramesStr = "Draw Frames";
				//ImGui::Checkbox(drawFramesStr, &physicsDebuggingSettings.DrawFrames);

				if (physicsDebuggingSettings.DisableAll)
				{
					ImGui::PopStyleColor();
				}

				ImGui::TreePop();
			}

			ImGui::TreePop();
		}

		static const char* postProcessStr = "Post processing";
		if (ImGui::TreeNode(postProcessStr))
		{
			ImGui::Checkbox("Enabled", &m_bPostProcessingEnabled);

			static const char* fxaaEnabledStr = "FXAA";
			ImGui::Checkbox(fxaaEnabledStr, &m_PostProcessSettings.bEnableFXAA);

			if (m_PostProcessSettings.bEnableFXAA)
			{
				ImGui::Indent();
				static const char* fxaaShowEdgesEnabledStr = "Show edges";
				ImGui::Checkbox(fxaaShowEdgesEnabledStr, &m_PostProcessSettings.bEnableFXAADEBUGShowEdges);
				ImGui::Unindent();
			}

			static const char* brightnessStr = "Brightness (RGB)";
			real maxBrightness = 2.5f;
			ImGui::SliderFloat3(brightnessStr, &m_PostProcessSettings.brightness.r, 0.0f, maxBrightness);
			ImGui::SameLine();
			ImGui::ColorButton("##1", ImVec4(
				m_PostProcessSettings.brightness.r / maxBrightness,
				m_PostProcessSettings.brightness.g / maxBrightness,
				m_PostProcessSettings.brightness.b / maxBrightness, 1));

			static const char* offsetStr = "Offset (RGB)";
			real minOffset = -0.065f;
			real maxOffset = 0.065f;
			ImGui::SliderFloat3(offsetStr, &m_PostProcessSettings.offset.r, minOffset, maxOffset);
			ImGui::SameLine();
			ImGui::ColorButton("##2", ImVec4(
				(m_PostProcessSettings.offset.r - minOffset) / (maxOffset - minOffset),
				(m_PostProcessSettings.offset.g - minOffset) / (maxOffset - minOffset),
				(m_PostProcessSettings.offset.b - minOffset) / (maxOffset - minOffset), 1));

			static const char* saturationStr = "Saturation";
			const real maxSaturation = 1.5f;
			ImGui::SliderFloat(saturationStr, &m_PostProcessSettings.saturation, 0.0f, maxSaturation);
			ImGui::SameLine();
			ImGui::ColorButton("##3", ImVec4(
				m_PostProcessSettings.saturation / maxSaturation,
				m_PostProcessSettings.saturation / maxSaturation,
				m_PostProcessSettings.saturation / maxSaturation, 1));

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

					for (auto meshPair : MeshComponent::m_LoadedMeshes)
					{
						bool bSelected = (i == selectedMeshIndex);
						std::string meshFileName = meshPair.first;
						StripLeadingDirectories(meshFileName);
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
			default:
			{
				PrintError("Unhandled MeshComponent::Type in Renderer::DrawImGuiForGameObject: %d\n", (i32)meshType);
				assert(false);
			} break;
			}
		}
	}

	void Renderer::DoCreateGameObjectButton(const char* buttonName, const char* popupName)
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

					MaterialID matID = 0;

					newGameObject->SetMeshComponent(new MeshComponent(matID, newGameObject));
					newGameObject->GetMeshComponent()->LoadFromFile(RESOURCE_LOCATION  "meshes/cube.glb");

					g_SceneManager->CurrentScene()->AddRootObject(newGameObject);

					newGameObject->Initialize();
					newGameObject->PostInitialize();

					g_EngineInstance->SetSelectedObject(newGameObject);

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
					g_EngineInstance->ToggleSelectedObject(gameObject);
				}
				else if (g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT))
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

	bool Renderer::LoadFontMetrics(const std::vector<char>& fileMemory, const std::string& fontFilePath, FT_Library& ft, BitmapFont** font,
		i16 size, bool bScreenSpace, std::map<i32, FontMetric*>* outCharacters,
	std::array<glm::vec2i, 4>* outMaxPositions, FT_Face* outFace)
	{
		// TODO: Save in common place
		u32 sampleDensity = 32;

		FT_Error error = FT_New_Memory_Face(ft, (FT_Byte*)fileMemory.data(), (FT_Long)fileMemory.size(), 0, outFace);
		FT_Face& face = *outFace;
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
			0, size * sampleDensity,
			(FT_UInt)g_Monitor->DPI.x,
			(FT_UInt)g_Monitor->DPI.y);

		if (g_bEnableLogging_Loading)
		{
			std::string fileName = fontFilePath;
			StripLeadingDirectories(fileName);
			Print("Loaded font file %s\n", fileName.c_str());
		}

		std::string fontName = std::string(face->family_name) + " - " + face->style_name;
		*font = new BitmapFont(size, fontName, face->num_glyphs);
		BitmapFont* newFont = *font;

		if (bScreenSpace)
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

			if (newFont->UseKerning() && glyphIndex)
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

	void Renderer::UpdateTextBufferSS(std::vector<TextVertex2D>& outTextVertices)
	{
		PROFILE_AUTO("Update Text Buffer SS");

		glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;

		for (BitmapFont* font : m_FontsSS)
		{
			real textScale = glm::max(2.0f / (real)frameBufferSize.x, 2.0f / (real)frameBufferSize.y) *
				(font->GetSize() / 12.0f);

			font->SetBufferStart((i32)(outTextVertices.size()));

			const std::vector<TextCache>& textCaches = font->GetTextCaches();
			for (const TextCache& textCache : textCaches)
			{
				std::string currentStr = textCache.str;

				real totalAdvanceX = 0;

				glm::vec2 basePos(0.0f);

				if (!textCache.bRaw)
				{
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

							glm::vec2 pos =
								glm::vec2((textCache.pos.x) * (textCache.bRaw ? 1.0f : aspectRatio), textCache.pos.y) +
								glm::vec2(totalAdvanceX + metric->offsetX, -metric->offsetY) * textScale;

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

							TextVertex2D vert = {};
							vert.pos = basePos + pos;
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

			font->SetBufferSize((i32)(outTextVertices.size()) - font->GetBufferStart());
			font->ClearCaches();
		}
	}

	void Renderer::UpdateTextBufferWS(std::vector<TextVertex3D>& outTextVertices)
	{
		// TODO: Consolidate with UpdateTextBufferSS

		PROFILE_AUTO("Update Text Buffer WS");

		glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		//real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;

		for (BitmapFont* font : m_FontsWS)
		{
			real textScale = glm::max(2.0f / (real)frameBufferSize.x, 2.0f / (real)frameBufferSize.y) *
				(font->GetSize() / 12.0f);

			font->SetBufferStart((i32)(outTextVertices.size()));

			const std::vector<TextCache>& caches = font->GetTextCaches();
			for (const TextCache& textCache : caches)
			{
				std::string currentStr = textCache.str;

				const glm::vec3 tangent = glm::rotate(textCache.rot, VEC3_RIGHT);

				real totalAdvanceX = 0;

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

							glm::vec3 pos = textCache.pos +
								tangent * (totalAdvanceX + metric->offsetX) * textScale +
								VEC3_UP * (real)(-metric->offsetY) * textScale;

							if (font->UseKerning())
							{
								std::wstring charKey(std::wstring(1, prevChar) + std::wstring(1, c));

								auto iter = metric->kerning.find(charKey);
								if (iter != metric->kerning.end())
								{
									pos += glm::vec3(iter->second, 0.0f) * textScale;
								}
							}

							glm::vec4 charSizePixelsCharSizeNorm(
								metric->width, metric->height,
								metric->width * textScale, metric->height * textScale);

							i32 texChannel = (i32)metric->channel;

							TextVertex3D vert = {};
							vert.pos = pos;
							vert.color = textCache.color;
							vert.tangent = tangent;
							vert.uv = metric->texCoord;
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

			font->SetBufferSize((i32)(outTextVertices.size()) - font->GetBufferStart());
			font->ClearCaches();
		}
	}

} // namespace flex