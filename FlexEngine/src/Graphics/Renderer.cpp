#include "stdafx.hpp"

#include "Graphics/Renderer.hpp"

#pragma warning(push, 0)

#if COMPILE_IMGUI
#include "imgui.h"
#include "imgui_internal.h"
#endif

#pragma warning(pop)

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Physics/RigidBody.hpp"
#include "Scene/GameObject.hpp"
#include "Window/Window.hpp"

namespace flex
{
	Renderer::Renderer()
	{
	}

	Renderer::~Renderer()
	{
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
	}

	bool Renderer::IsRenderingGrid() const
	{
		return m_bRenderGrid;
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

	void Renderer::DrawImGuiForRenderObjectCommon(GameObject* gameObject)
	{
		if (!gameObject)
		{
			return;
		}

		bool bStatic = gameObject->IsStatic();
		if (ImGui::Checkbox("Static", &bStatic))
		{
			gameObject->SetStatic(bStatic);
		}

		ImGui::Text("Transform");

		Transform* transform = gameObject->GetTransform();
		if (ImGui::BeginPopupContextItem("transform context menu"))
		{
			if (ImGui::Button("Copy"))
			{
				CopyTransformToClipboard(transform);
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Paste"))
			{
				PasteTransformFromClipboard(transform);
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		static int transformSpace = 0;

		static glm::vec3 sRot = glm::degrees((glm::eulerAngles(transform->GetLocalRotation())));

		if (!ImGui::IsMouseDown(0))
		{
			sRot = glm::degrees((glm::eulerAngles(transform->GetLocalRotation())));
		}

		glm::vec3 translation = transform->GetLocalPosition();
		glm::vec3 rotation = sRot;
		glm::vec3 pScale = transform->GetLocalScale();
		glm::vec3 scale = pScale;

		bool valueChanged = false;

		valueChanged = ImGui::DragFloat3("Position", &translation[0], 0.1f) || valueChanged;
		if (ImGui::IsItemClicked(1))
		{
			translation = VEC3_ZERO;
			valueChanged = true;
		}

		glm::vec3 cleanedRot;
		valueChanged = DoImGuiRotationDragFloat3("Rotation", rotation, cleanedRot) || valueChanged;

		valueChanged = ImGui::DragFloat3("Scale", &scale[0], 0.01f) || valueChanged;
		if (ImGui::IsItemClicked(1))
		{
			scale = VEC3_ONE;
			valueChanged = true;
		}

		ImGui::SameLine();

		bool bUniformScale = gameObject->HasUniformScale();
		if (ImGui::Checkbox("u", &bUniformScale))
		{
			valueChanged = true;
		}
		if (bUniformScale)
		{
			float newScale = scale.x;
			if (scale.y != pScale.y)
			{
				newScale = scale.y;
			}
			else if (scale.z != pScale.z)
			{
				newScale = scale.z;
			}
			scale = glm::vec3(newScale);
		}

		if (valueChanged)
		{
			transform->SetLocalPosition(translation, false);

			sRot = rotation;

			glm::quat rotQuat(glm::quat(glm::radians(cleanedRot)));

			transform->SetLocalRotation(rotQuat, false);
			transform->SetLocalScale(scale, true);
			gameObject->SetUseUniformScale(bUniformScale, false);

			if (gameObject->GetRigidBody())
			{
				gameObject->GetRigidBody()->MatchParentTransform();
			}

			if (g_EngineInstance->IsObjectSelected(gameObject))
			{
				g_EngineInstance->CalculateSelectedObjectsCenter();
			}
		}
	}

	void Renderer::DrawImGuiLights()
	{
		ImGui::Text("Lights");
		ImGuiColorEditFlags colorEditFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_RGB | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_HDR;

		bool bDirLightEnabled = (m_DirectionalLight.enabled == 1);
		if (ImGui::Checkbox("##dir-light-enabled", &bDirLightEnabled))
		{
			m_DirectionalLight.enabled = bDirLightEnabled ? 1 : 0;
		}

		ImGui::SameLine();

		if (ImGui::TreeNode("Directional Light"))
		{
			ImGui::DragFloat3("Position", &m_DirectionalLight.position.x, 0.1f);
			glm::vec3 dirtyRot = glm::degrees(glm::eulerAngles(m_DirectionalLight.rotation));
			glm::vec3 cleanedRot;
			if (DoImGuiRotationDragFloat3("Rotation", dirtyRot, cleanedRot))
			{
				m_DirectionalLight.rotation = glm::quat(glm::radians(cleanedRot));
			}
			ImGui::SliderFloat("Brightness", &m_DirectionalLight.brightness, 0.0f, 15.0f);
			ImGui::ColorEdit4("Color ", &m_DirectionalLight.color.r, colorEditFlags);

			ImGui::Spacing();
			ImGui::Text("Shadow");

			ImGui::Checkbox("Cast shadow", &m_DirectionalLight.bCastShadow);
			ImGui::SliderFloat("Shadow darkness", &m_DirectionalLight.shadowDarkness, 0.0f, 1.0f);

			ImGui::DragFloat("Near", &m_DirectionalLight.shadowMapNearPlane);
			ImGui::DragFloat("Far", &m_DirectionalLight.shadowMapFarPlane);
			ImGui::DragFloat("Zoom", &m_DirectionalLight.shadowMapZoom);

			if (ImGui::CollapsingHeader("Preview"))
			{
				ImGui::Image((void*)m_DirectionalLight.shadowTextureID, ImVec2(256, 256));
			}

			ImGui::TreePop();
		}

		i32 i = 0;
		while (i < (i32)m_PointLights.size())
		{
			const std::string iStr = std::to_string(i);

			bool bPointLightEnabled = (m_PointLights[i].enabled == 1);
			if (ImGui::Checkbox(std::string("##point-light-enabled" + iStr).c_str(), &bPointLightEnabled))
			{
				m_PointLights[i].enabled = bPointLightEnabled ? 1 : 0;
			}

			ImGui::SameLine();

			const std::string objectName("Point Light##" + iStr);
			const bool bTreeOpen = ImGui::TreeNode(objectName.c_str());
			bool bRemovedPointLight = false;

			if (ImGui::BeginPopupContextItem())
			{
				static const char* removePointLightStr = "Delete";
				if (ImGui::Button(removePointLightStr))
				{
					m_PointLights.erase(m_PointLights.begin() + i);
					bRemovedPointLight = true;
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			if (!bRemovedPointLight && bTreeOpen)
			{
				ImGui::DragFloat3("Position", &m_PointLights[i].position.x, 0.1f);
				ImGui::ColorEdit4("Color ", &m_PointLights[i].color.r, colorEditFlags);
				ImGui::SliderFloat("Brightness", &m_PointLights[i].brightness, 0.0f, 1000.0f);
			}

			if (bTreeOpen)
			{
				ImGui::TreePop();
			}

			if (!bRemovedPointLight)
			{
				++i;
			}
		}

		if (m_PointLights.size() < MAX_POINT_LIGHT_COUNT)
		{
			static const char* newPointLightStr = "Add point light";
			if (ImGui::Button(newPointLightStr))
			{
				PointLight newPointLight = {};
				InitializePointLight(newPointLight);
			}
		}
	}

	bool Renderer::InitializeDirectionalLight(const DirectionalLight& dirLight)
	{
		m_DirectionalLight = dirLight;
		return true;
	}

	PointLightID Renderer::InitializePointLight(const PointLight& pointLight)
	{
		if (m_PointLights.size() == MAX_POINT_LIGHT_COUNT)
		{
			PrintWarn("Attempted to add point light when already at max capacity of %i\n", MAX_POINT_LIGHT_COUNT);
			return InvalidPointLightID;
		}

		m_PointLights.push_back(pointLight);
		return m_PointLights.size() - 1;
	}

	void Renderer::ClearDirectionalLight()
	{
		m_DirectionalLight = {};
	}

	void Renderer::ClearPointLights()
	{
		m_PointLights.clear();
	}

	DirectionalLight& Renderer::GetDirectionalLight()
	{
		return m_DirectionalLight;
	}

	PointLight& Renderer::GetPointLight(PointLightID pointLight)
	{
		return m_PointLights[pointLight];
	}

	i32 Renderer::GetNumPointLights()
	{
		return m_PointLights.size();
	}

	i32 Renderer::GetFramesRenderedCount() const
	{
		return m_FramesRendered;
	}

	Renderer::PostProcessSettings& Renderer::GetPostProcessSettings()
	{
		return m_PostProcessSettings;
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

			bool bVSyncEnabled = g_Renderer->GetVSyncEnabled();
			static const char* vSyncEnabledStr = "VSync";
			if (ImGui::Checkbox(vSyncEnabledStr, &bVSyncEnabled))
			{
				g_Renderer->SetVSyncEnabled(bVSyncEnabled);
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

				static const char* wireframeStr = "Wireframe";
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
			real minOffset = -0.35f;
			real maxOffset = 0.35f;
			ImGui::SliderFloat3(offsetStr, &m_PostProcessSettings.offset.r, minOffset, maxOffset);
			ImGui::SameLine();
			ImGui::ColorButton("##2", ImVec4(
				(m_PostProcessSettings.offset.r - minOffset) / (maxOffset - minOffset),
				(m_PostProcessSettings.offset.g - minOffset) / (maxOffset - minOffset),
				(m_PostProcessSettings.offset.b - minOffset) / (maxOffset - minOffset), 1));

			static const char* saturationStr = "Saturation";
			const real maxSaturation = 2.0f;
			ImGui::SliderFloat(saturationStr, &m_PostProcessSettings.saturation, 0.0f, maxSaturation);
			ImGui::SameLine();
			ImGui::ColorButton("##3", ImVec4(
				m_PostProcessSettings.saturation / maxSaturation,
				m_PostProcessSettings.saturation / maxSaturation,
				m_PostProcessSettings.saturation / maxSaturation, 1));

			ImGui::TreePop();
		}
	}
} // namespace flex