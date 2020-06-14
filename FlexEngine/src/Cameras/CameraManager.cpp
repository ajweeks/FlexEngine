
#include "stdafx.hpp"

#include "Cameras/CameraManager.hpp"

#include "Cameras/BaseCamera.hpp"
#include "InputManager.hpp"

namespace flex
{
	CameraManager::CameraManager() :
		m_ActionCallback(this, &CameraManager::OnActionEvent)
	{
	}

	void CameraManager::Initialize()
	{
		BaseCamera* camera = CurrentCamera();
		assert(camera != nullptr);

		camera->Initialize();
		camera->OnPossess();

		g_InputManager->BindActionCallback(&m_ActionCallback, 12);

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
			delete camera;
		}
		m_Cameras.clear();

		g_InputManager->UnbindActionCallback(&m_ActionCallback);

		m_bInitialized = false;
	}

	void CameraManager::Update()
	{
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

	BaseCamera* CameraManager::SetCamera(BaseCamera* camera, bool bAlignWithPrevious)
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

		return PushCamera(camera, bAlignWithPrevious);
	}

	BaseCamera* CameraManager::CycleCamera(i32 deltaIndex, bool bAlignWithPrevious)
	{
		assert(glm::abs(deltaIndex) == 1);

		const i32 numCameras = (i32)m_Cameras.size();

		const i32 desiredIndex = GetCameraIndex(m_CameraStack.top()) + deltaIndex;
		i32 newIndex = desiredIndex;
		i32 offset = 0;
		do
		{
			newIndex = desiredIndex + offset;
			offset += deltaIndex;
			if (newIndex < 0)
			{
				newIndex += numCameras;
			}
			else if (newIndex >= numCameras)
			{
				newIndex -= numCameras;
			}
		} while (!m_Cameras[newIndex]->bDEBUGCyclable);

		return SetCamera(m_Cameras[newIndex], bAlignWithPrevious);
	}

	BaseCamera* CameraManager::SetCameraByName(const std::string& name, bool bAlignWithPrevious)
	{
		return SetCamera(GetCameraByName(name), bAlignWithPrevious);
	}

	BaseCamera* CameraManager::PushCamera(BaseCamera* camera, bool bAlignWithPrevious)
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

		return camera;
	}

	BaseCamera* CameraManager::PushCameraByName(const std::string& name, bool bAlignWithPrevious)
	{
		return PushCamera(GetCameraByName(name), bAlignWithPrevious);
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

	void CameraManager::DrawImGuiObjects()
	{
		if (ImGui::TreeNode("Camera"))
		{
			BaseCamera* currentCamera = CurrentCamera();

			const i32 cameraCount = (i32)m_Cameras.size();

			if (cameraCount > 1) // Only show arrows if other cameras exist
			{
				if (ImGui::Button("<"))
				{
					CycleCamera(-1, false);
				}

				ImGui::SameLine();

				std::string cameraNameStr = currentCamera->GetName();
				ImGui::TextUnformatted(cameraNameStr.c_str());

				ImGui::SameLine();

				if (ImGui::Button(">"))
				{
					CycleCamera(1, false);
				}
			}

			ImGui::SliderFloat("Move speed", &currentCamera->moveSpeed, 1.0f, 750.0f);

			real turnSpeed = glm::degrees(currentCamera->mouseRotationSpeed);
			if (ImGui::SliderFloat("Turn speed", &turnSpeed, 0.01f, 0.3f))
			{
				currentCamera->mouseRotationSpeed = glm::radians(turnSpeed);
			}

			ImGui::DragFloat3("Position", &currentCamera->position.x, 0.1f);

			glm::vec2 camYawPitch;
			camYawPitch[0] = glm::degrees(currentCamera->yaw);
			camYawPitch[1] = glm::degrees(currentCamera->pitch);
			if (ImGui::DragFloat2("Yaw & Pitch", &camYawPitch.x, 0.05f))
			{
				currentCamera->yaw = glm::radians(camYawPitch[0]);
				currentCamera->pitch = glm::radians(camYawPitch[1]);
			}

			real camFOV = glm::degrees(currentCamera->FOV);
			if (ImGui::DragFloat("FOV", &camFOV, 0.05f, 10.0f, 150.0f))
			{
				currentCamera->FOV = glm::radians(camFOV);
			}

			ImGui::DragFloat("near", &currentCamera->zNear, 0.05f, 0.0f, 10.0f);

			ImGui::DragFloat("far", &currentCamera->zFar, 0.05f, 0.0f, 10000.0f);

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
		to->position = from->position;
		to->pitch = from->pitch;
		to->yaw = from->yaw;
		to->FOV = from->FOV;
	}

	EventReply CameraManager::OnActionEvent(Action action)
	{
		if (action == Action::DBG_SWITCH_TO_PREV_CAM)
		{
			CycleCamera(-1, false);
			return EventReply::CONSUMED;
		}
		else if (action == Action::DBG_SWITCH_TO_NEXT_CAM)
		{
			CycleCamera(1, false);
			return EventReply::CONSUMED;
		}

		return EventReply::UNCONSUMED;
	}

} // namespace flex
