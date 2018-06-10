#include "stdafx.hpp"

#include "Graphics/Renderer.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include "Logger.hpp"
#include "Scene/GameObject.hpp"

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
		ImGui::Text("Static: %s", (bStatic ? "true" : "false"));

		Transform* transform = gameObject->GetTransform();
		static int transformSpace = 0;

		//if (bStatic)
		//{
		//	ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		//	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		//}

		static const char* localStr = "local";
		static const char* globalStr = "global";

		ImGui::RadioButton(localStr, &transformSpace, 0); ImGui::SameLine();
		ImGui::RadioButton(globalStr, &transformSpace, 1);

		const bool local = (transformSpace == 0);

		glm::vec3 translation = local ? transform->GetLocalPosition() : transform->GetWorldlPosition();
		glm::vec3 rotation = glm::degrees((glm::eulerAngles(local ? transform->GetLocalRotation() : transform->GetWorldlRotation())));
		glm::vec3 scale = local ? transform->GetLocalScale() : transform->GetWorldlScale();

		bool valueChanged = false;

		valueChanged = ImGui::DragFloat3("Translation", &translation[0], 0.1f) || valueChanged;
		valueChanged = ImGui::DragFloat3("Rotation", &rotation[0], 0.1f) || valueChanged;
		valueChanged = ImGui::DragFloat3("Scale", &scale[0], 0.01f) || valueChanged;

		if (valueChanged)
		{
			if (local)
			{
				transform->SetLocalPosition(translation);
				transform->SetLocalRotation(glm::quat(glm::radians(rotation)));
				transform->SetLocalScale(scale);
			}
			else
			{
				transform->SetWorldlPosition(translation);
				transform->SetWorldRotation(glm::quat(glm::radians(rotation)));
				transform->SetWorldScale(scale);
			}
		}

		//if (bStatic)
		//{
		//	ImGui::PopItemFlag();
		//	ImGui::PopStyleVar();
		//}
	}

	void Renderer::DrawImGuiLights()
	{
		if (ImGui::TreeNode("Lights"))
		{
			ImGui::AlignFirstTextHeightToWidgets();

			ImGuiColorEditFlags colorEditFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_RGB | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_HDR;

			bool dirLightEnabled = m_DirectionalLight.enabled == 1;
			ImGui::Checkbox("##dir-light-enabled", &dirLightEnabled);
			m_DirectionalLight.enabled = dirLightEnabled ? 1 : 0;
			ImGui::SameLine();
			if (ImGui::TreeNode("Directional Light"))
			{
				ImGui::DragFloat3("Rotation", &m_DirectionalLight.direction.x, 0.01f);

				ImGui::ColorEdit4("Color ", &m_DirectionalLight.color.r, colorEditFlags);

				ImGui::SliderFloat("Brightness", &m_DirectionalLight.brightness, 0.0f, 15.0f);

				ImGui::TreePop();
			}

			for (size_t i = 0; i < m_PointLights.size(); ++i)
			{
				const std::string iStr = std::to_string(i);
				const std::string objectName("Point Light##" + iStr);

				bool PointLightEnabled = m_PointLights[i].enabled == 1;
				ImGui::Checkbox(std::string("##enabled" + iStr).c_str(), &PointLightEnabled);
				m_PointLights[i].enabled = PointLightEnabled ? 1 : 0;
				ImGui::SameLine();
				if (ImGui::TreeNode(objectName.c_str()))
				{
					ImGui::DragFloat3("Translation", &m_PointLights[i].position.x, 0.1f);

					ImGui::ColorEdit4("Color ", &m_PointLights[i].color.r, colorEditFlags);

					ImGui::SliderFloat("Brightness", &m_PointLights[i].brightness, 0.0f, 1000.0f);

					ImGui::TreePop();
				}
			}

			ImGui::TreePop();
		}
	}

	bool Renderer::InitializeDirectionalLight(const DirectionalLight& dirLight)
	{
		m_DirectionalLight = dirLight;
		return true;
	}

	PointLightID Renderer::InitializePointLight(const PointLight& pointLight)
	{
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

	bool Renderer::GetFXAAEnabled() const
	{
		return m_bEnableFXAA;
	}

	void Renderer::SetFXAAEnabled(bool bEnabled)
	{
		m_bEnableFXAA = bEnabled;
	}
	
	bool Renderer::GetFXAADEBUGShowEdgesEnabled() const
	{
		return m_bEnableFXAADEBUGShowEdges;
	}

	void Renderer::SetFXAADEBUGShowEdgesEnabled(bool bEnabled)
	{
		m_bEnableFXAADEBUGShowEdges = bEnabled;
	}
} // namespace flex