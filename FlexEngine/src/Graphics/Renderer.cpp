#include "stdafx.hpp"

#include "Graphics/Renderer.hpp"

#pragma warning(push, 0)
#include <imgui.h>
#include <imgui_internal.h>
#pragma warning(pop)

#include "Logger.hpp"
#include "Scene/GameObject.hpp"
#include "Physics/RigidBody.hpp"

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

	void Renderer::SetPostProcessingEnabled(bool bEnabled)
	{
		m_bPostProcessingEnabled = bEnabled;
	}

	bool Renderer::GetPostProcessingEnabled() const
	{
		return m_bPostProcessingEnabled;
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

		Transform* transform = gameObject->GetTransform();
		static int transformSpace = 0;

		//if (bStatic)
		//{
		//	ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		//	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		//}

		//static const char* localStr = "local";
		//static const char* globalStr = "global";
		//
		//ImGui::RadioButton(localStr, &transformSpace, 0); ImGui::SameLine();
		//ImGui::RadioButton(globalStr, &transformSpace, 1);
		//
		//const bool local = (transformSpace == 0);

		//glm::vec3 translation = local ? transform->GetLocalPosition() : transform->GetWorldPosition();
		//glm::vec3 rotation = glm::degrees((glm::eulerAngles(local ? transform->GetLocalRotation() : transform->GetWorldRotation())));
		//glm::vec3 scale = local ? transform->GetLocalScale() : transform->GetWorldScale();

		glm::vec3 translation = transform->GetLocalPosition();
		glm::vec3 rotation = glm::degrees((glm::eulerAngles(transform->GetLocalRotation())));
		glm::vec3 scale = transform->GetLocalScale();

		bool valueChanged = false;

		valueChanged = ImGui::DragFloat3("T", &translation[0], 0.1f) || valueChanged;
		valueChanged = ImGui::DragFloat3("R", &rotation[0], 0.1f) || valueChanged;
		valueChanged = ImGui::DragFloat3("S", &scale[0], 0.01f) || valueChanged;

		if (valueChanged)
		{
			//if (local)
			{
				transform->SetLocalPosition(translation, false);
				transform->SetLocalRotation(glm::quat(glm::radians(rotation)), false);
				transform->SetLocalScale(scale, true);

				if (gameObject->GetRigidBody())
				{
					gameObject->GetRigidBody()->MatchParentTransform();
				}
			}
			//else
			//{
			//	transform->SetWorldPosition(translation, false);
			//	transform->SetWorldRotation(glm::quat(glm::radians(rotation)), false);
			//	transform->SetWorldScale(scale, true);
			//
			//	if (gameObject->GetRigidBody())
			//	{
			//		gameObject->GetRigidBody()->MatchParentTransform();
			//	}
			//}
		}

		//if (bStatic)
		//{
		//	ImGui::PopItemFlag();
		//	ImGui::PopStyleVar();
		//}
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
			ImGui::DragFloat3("Rotation", &m_DirectionalLight.direction.x, 0.01f);
			ImGui::ColorEdit4("Color ", &m_DirectionalLight.color.r, colorEditFlags);
			ImGui::SliderFloat("Brightness", &m_DirectionalLight.brightness, 0.0f, 15.0f);

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
				ImGui::DragFloat3("Translation", &m_PointLights[i].position.x, 0.1f);
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
			Logger::LogWarning("Attempted to add point light when already at max capacity! (" + std::to_string(MAX_POINT_LIGHT_COUNT) + ')');
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

	Renderer::PostProcessSettings& Renderer::GetPostProcessSettings()
	{
		return m_PostProcessSettings;
	}
} // namespace flex