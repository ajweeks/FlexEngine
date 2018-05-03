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

		if (bStatic)
		{
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		}

		static const char* localStr = "local";
		static const char* globalStr = "global";

		ImGui::RadioButton(localStr, &transformSpace, 0); ImGui::SameLine();
		ImGui::RadioButton(globalStr, &transformSpace, 1);

		const bool local = (transformSpace == 0);

		glm::vec3 translation = local ? transform->GetLocalPosition() : transform->GetGlobalPosition();
		glm::vec3 rotation = glm::degrees((glm::eulerAngles(local ? transform->GetLocalRotation() : transform->GetGlobalRotation())));
		glm::vec3 scale = local ? transform->GetLocalScale() : transform->GetGlobalScale();

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
				transform->SetGlobalPosition(translation);
				transform->SetGlobalRotation(glm::quat(glm::radians(rotation)));
				transform->SetGlobalScale(scale);
			}
		}

		if (bStatic)
		{
			ImGui::PopItemFlag();
			ImGui::PopStyleVar();
		}

	}
} // namespace flex