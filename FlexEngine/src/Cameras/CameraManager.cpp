
#include "stdafx.hpp"

#include "Cameras/CameraManager.hpp"

#include "Cameras/BaseCamera.hpp"

namespace flex
{
	CameraManager::CameraManager()
	{
	}

	CameraManager::~CameraManager()
	{
	}

	void CameraManager::Initialize()
	{
		m_Cameras[m_ActiveCameraIndex]->Initialize();
	}

	void CameraManager::Update()
	{
		m_Cameras[m_ActiveCameraIndex]->Update();
	}

	void CameraManager::OnSceneChanged()
	{
		m_Cameras[m_ActiveCameraIndex]->OnSceneChanged();
	}

	void CameraManager::DestroyCameras()
	{
		for (BaseCamera* camera : m_Cameras)
		{
			SafeDelete(camera);
		}
		m_Cameras.clear();
		m_ActiveCameraIndex = -1;
	}

	BaseCamera* CameraManager::CurrentCamera() const
	{
		return m_Cameras[m_ActiveCameraIndex];
	}

	i32 CameraManager::CameraCount() const
	{
		return (i32)m_Cameras.size();
	}

	void CameraManager::AddCamera(BaseCamera* camera, bool bSwitchTo)
	{
		assert(camera);

		i32 cameraIndex = GetCameraIndex(camera);
		if (cameraIndex == -1) // Only add camera if it hasn't been added before
		{
			m_Cameras.push_back(camera);

			if (bSwitchTo || m_ActiveCameraIndex == -1)
			{
				m_ActiveCameraIndex = (m_Cameras.size() - 1);
			}
		}
	}

	void CameraManager::SwtichTo(BaseCamera* camera, bool bAlign)
	{
		assert(camera);

		i32 newCameraIndex = GetCameraIndex(camera);

		if (newCameraIndex == -1)
		{
			PrintError("Attempted to switch to camera which doesn't exist! %s\n", camera->GetName().c_str());
		}
		else
		{
			SwtichToIndex(newCameraIndex, bAlign);
		}
	}

	void CameraManager::SwtichToIndex(i32 index, bool bAlign)
	{
		if (index >= 0 && index < (i32)m_Cameras.size() && index != m_ActiveCameraIndex)
		{
			if (m_ActiveCameraIndex != -1)
			{
				m_Cameras[m_ActiveCameraIndex]->OnDepossess();
			}

			if (bAlign)
			{
				AlignCameras(m_Cameras[m_ActiveCameraIndex], m_Cameras[index]);
			}

			m_ActiveCameraIndex = index;

			m_Cameras[m_ActiveCameraIndex]->Initialize();
			m_Cameras[m_ActiveCameraIndex]->OnPossess();
		}
	}

	void CameraManager::SetActiveIndexRelative(i32 delta, bool bAlign)
	{
		i32 newIndex = m_ActiveCameraIndex + delta;
		i32 numCameras = (i32)m_Cameras.size();
		if (newIndex < 0)
		{
			newIndex += numCameras;
		}
		else if (newIndex >= numCameras)
		{
			newIndex -= numCameras;
		}

		SwtichToIndex(newIndex, bAlign);
	}

	void CameraManager::SetActiveCameraByType(const std::string& typeStr)
	{
		for (i32 i = 0; i < (i32)m_Cameras.size(); ++i)
		{
			if (m_Cameras[i]->GetName().compare(typeStr) == 0)
			{
				SwtichToIndex(i, false);
				return;
			}
		}

		PrintWarn("Attempted to set camera to unknown name: %s\n", typeStr.c_str());
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
					SetActiveIndexRelative(-1, false);
				}

				ImGui::SameLine();

				std::string cameraNameStr = currentCamera->GetName();
				ImGui::TextUnformatted(cameraNameStr.c_str());

				ImGui::SameLine();

				if (ImGui::Button(arrowNextStr))
				{
					SetActiveIndexRelative(1, false);
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

	i32 CameraManager::GetCameraIndex(BaseCamera* camera)
	{
		i32 cameraIndex = -1;
		for (u32 i = 0; i < m_Cameras.size(); ++i)
		{
			if (m_Cameras[i] == camera)
			{
				cameraIndex = i;
				break;
			}
		}
		return cameraIndex;
	}

	void CameraManager::AlignCameras(BaseCamera* from, BaseCamera* to)
	{
		to->SetPosition(from->GetPosition());
		to->SetPitch(from->GetPitch());
		to->SetYaw(from->GetYaw());
		to->SetFOV(from->GetFOV());
	}
} // namespace flex
