
#include "stdafx.hpp"

#include "Cameras/CameraManager.hpp"

#include "Cameras/BaseCamera.hpp"
#include "InputManager.hpp"

namespace flex
{
	void CameraManager::Initialize()
	{
		BaseCamera* camera = CurrentCamera();
		assert(camera != nullptr);

		camera->Initialize();
		camera->OnPossess();
		m_bInitialized = true;
	}

	void CameraManager::Destroy()
	{
		while (!m_CameraStack.empty())
		{
			m_CameraStack.pop();
		}

		for (BaseCamera* camera : m_Cameras)
		{
			camera->Destroy();
			SafeDelete(camera);
		}
		m_Cameras.clear();

		m_bInitialized = false;
	}

	void CameraManager::Update()
	{
		if (g_InputManager->GetActionPressed(Action::DBG_SWITCH_TO_PREV_CAM))
		{
			i32 newIdx = GetCameraIndex(m_CameraStack.top()) - 1;
			if (newIdx < 0)
			{
				newIdx += (i32)m_Cameras.size();
			}
			SetCamera(m_Cameras[newIdx], false);
		}
		else if (g_InputManager->GetActionPressed(Action::DBG_SWITCH_TO_NEXT_CAM))
		{
			i32 newIdx = GetCameraIndex(m_CameraStack.top()) + 1;
			if (newIdx >= (i32)m_Cameras.size())
			{
				newIdx -= (i32)m_Cameras.size();
			}
			SetCamera(m_Cameras[newIdx], false);
		}
	}

	void CameraManager::OnSceneChanged()
	{
		for (BaseCamera* cam : m_Cameras)
		{
			cam->OnSceneChanged();
		}
	}

	BaseCamera* CameraManager::CurrentCamera() const
	{
		return m_CameraStack.top();
	}

	i32 CameraManager::CameraCount() const
	{
		return (i32)m_Cameras.size();
	}

	void CameraManager::AddCamera(BaseCamera* camera, bool bSwitchTo)
	{
		assert(camera != nullptr);

		i32 cameraIndex = GetCameraIndex(camera);
		if (cameraIndex == -1) // Only add camera if it hasn't been added before
		{
			m_Cameras.push_back(camera);

			if (bSwitchTo)
			{
				SetCamera(camera, false);
			}
		}
	}

	void CameraManager::SetCamera(BaseCamera* camera, bool bAlignWithPrevious)
	{
		if (!m_CameraStack.empty())
		{
			BaseCamera* activeCamera = CurrentCamera();
			activeCamera->OnDepossess();
			activeCamera->Destroy();
		}

		while (!m_CameraStack.empty())
		{
			m_CameraStack.pop();
		}

		PushCamera(camera, bAlignWithPrevious);
	}

	void CameraManager::CycleCamera(i32 deltaIndex, bool bAlignWithPrevious)
	{
		i32 newIndex = GetCameraIndex(m_CameraStack.top()) + deltaIndex;
		i32 numCameras = (i32)m_Cameras.size();
		if (newIndex < 0)
		{
			newIndex += numCameras;
		}
		else if (newIndex >= numCameras)
		{
			newIndex -= numCameras;
		}

		SetCamera(m_Cameras[newIndex], bAlignWithPrevious);
	}

	void CameraManager::SetCameraByName(const std::string& name, bool bAlignWithPrevious)
	{
		SetCamera(GetCameraByName(name), bAlignWithPrevious);
	}

	void CameraManager::PushCamera(BaseCamera* camera, bool bAlignWithPrevious)
	{
		assert(camera != nullptr);

		BaseCamera* pActiveCam = nullptr;
		if (!m_CameraStack.empty())
		{
			pActiveCam = m_CameraStack.top();

			pActiveCam->OnDepossess();
			pActiveCam->Destroy();
		}

		m_CameraStack.push(camera);

		if (bAlignWithPrevious && pActiveCam != nullptr)
		{
			AlignCameras(pActiveCam, camera);
		}

		if (m_bInitialized)
		{
			camera->Initialize();
			camera->OnPossess();
		}
	}

	void CameraManager::PushCameraByName(const std::string& name, bool bAlignWithPrevious)
	{
		PushCamera(GetCameraByName(name), bAlignWithPrevious);
	}

	void CameraManager::PopCamera()
	{
		if (m_CameraStack.size() <= 1)
		{
			PrintError("CameraManager::PopCamera - Attempted to pop final camera from stack\n");
			return;
		}

		BaseCamera* currentCamera = CurrentCamera();
		currentCamera->OnDepossess();
		currentCamera->Destroy();

		m_CameraStack.pop();

		currentCamera = CurrentCamera();
		currentCamera->OnPossess();
		currentCamera->Initialize();
	}

	void CameraManager::DrawImGuiObjects()
	{
		const char* cameraStr = "Camera";
		if (ImGui::TreeNode(cameraStr))
		{
			BaseCamera* currentCamera = CurrentCamera();

			const i32 cameraCount = (i32)m_Cameras.size();

			if (cameraCount > 1) // Only show arrows if other cameras exist
			{
				static const char* arrowPrevStr = "<";
				static const char* arrowNextStr = ">";

				if (ImGui::Button(arrowPrevStr))
				{
					i32 newIdx = GetCameraIndex(m_CameraStack.top()) - 1;
					if (newIdx < 0)
					{
						newIdx += (i32)m_Cameras.size();
					}
					SetCamera(m_Cameras[newIdx], false);
				}

				ImGui::SameLine();

				std::string cameraNameStr = currentCamera->GetName();
				ImGui::TextUnformatted(cameraNameStr.c_str());

				ImGui::SameLine();

				if (ImGui::Button(arrowNextStr))
				{
					i32 newIdx = GetCameraIndex(m_CameraStack.top()) + 1;
					if (newIdx >= (i32)m_Cameras.size())
					{
						newIdx -= (i32)m_Cameras.size();
					}
					SetCamera(m_Cameras[newIdx], false);
				}
			}

			static const char* moveSpeedStr = "Move speed";
			float moveSpeed = currentCamera->GetMoveSpeed();
			if (ImGui::SliderFloat(moveSpeedStr, &moveSpeed, 1.0f, 250.0f))
			{
				currentCamera->SetMoveSpeed(moveSpeed);
			}

			static const char* turnSpeedStr = "Turn speed";
			float turnSpeed = glm::degrees(currentCamera->GetRotationSpeed());
			if (ImGui::SliderFloat(turnSpeedStr, &turnSpeed, 0.01f, 0.3f))
			{
				currentCamera->SetRotationSpeed(glm::radians(turnSpeed));
			}

			glm::vec3 camPos = currentCamera->GetPosition();
			if (ImGui::DragFloat3("Position", &camPos.x, 0.1f))
			{
				currentCamera->SetPosition(camPos);
			}

			glm::vec2 camYawPitch;
			camYawPitch[0] = glm::degrees(currentCamera->GetYaw());
			camYawPitch[1] = glm::degrees(currentCamera->GetPitch());
			if (ImGui::DragFloat2("Yaw & Pitch", &camYawPitch.x, 0.05f))
			{
				currentCamera->SetYaw(glm::radians(camYawPitch[0]));
				currentCamera->SetPitch(glm::radians(camYawPitch[1]));
			}

			real camFOV = glm::degrees(currentCamera->GetFOV());
			if (ImGui::DragFloat("FOV", &camFOV, 0.05f, 10.0f, 150.0f))
			{
				currentCamera->SetFOV(glm::radians(camFOV));
			}

			if (ImGui::Button("Reset orientation"))
			{
				currentCamera->ResetOrientation();
			}

			ImGui::SameLine();
			if (ImGui::Button("Reset position"))
			{
				currentCamera->ResetPosition();
			}

			ImGui::TreePop();
		}
	}

	BaseCamera* CameraManager::GetCameraByName(const std::string& name)
	{
		for (i32 i = 0; i < (i32)m_Cameras.size(); ++i)
		{
			if (m_Cameras[i]->GetName().compare(name) == 0)
			{
				return m_Cameras[i];
			}
		}

		return nullptr;
	}

	i32 CameraManager::GetCameraIndex(BaseCamera* camera)
	{
		for (u32 i = 0; i < m_Cameras.size(); ++i)
		{
			if (m_Cameras[i] == camera)
			{
				return i;
			}
		}

		return -1;
	}

	void CameraManager::AlignCameras(BaseCamera* from, BaseCamera* to)
	{
		to->SetPosition(from->GetPosition());
		to->SetPitch(from->GetPitch());
		to->SetYaw(from->GetYaw());
		to->SetFOV(from->GetFOV());
	}

} // namespace flex
