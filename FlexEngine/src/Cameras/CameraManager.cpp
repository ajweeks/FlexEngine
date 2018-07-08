
#include "stdafx.hpp"

#include "Cameras/CameraManager.hpp"

#include "Cameras/BaseCamera.hpp"
#include "GameContext.hpp"

namespace flex
{
	CameraManager::CameraManager()
	{
	}

	CameraManager::~CameraManager()
	{
		DestroyCameras();
	}

	void CameraManager::Initialize(const GameContext& gameContext)
	{
		m_Cameras[m_ActiveCameraIndex]->Initialize(gameContext);
	}

	void CameraManager::Update(const GameContext& gameContext)
	{
		m_Cameras[m_ActiveCameraIndex]->Update(gameContext);
	}

	void CameraManager::OnSceneChanged(const GameContext& gameContext)
	{
		m_Cameras[m_ActiveCameraIndex]->OnSceneChanged(gameContext);
	}

	void CameraManager::DestroyCameras()
	{
		for (u32 i = 0; i < m_Cameras.size(); ++i)
		{
			SafeDelete(m_Cameras[i]);
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

	void CameraManager::SwtichTo(const GameContext& gameContext, BaseCamera* camera, bool bAlign)
	{
		assert(camera);

		i32 newCameraIndex = GetCameraIndex(camera);

		if (newCameraIndex == -1)
		{
			PrintError("Attempted to switch to camera which doesn't exist! %s\n", camera->GetName().c_str());
		}
		else
		{
			SwtichToIndex(gameContext, newCameraIndex, bAlign);
		}
	}

	void CameraManager::SwtichToIndex(const GameContext& gameContext, i32 index, bool bAlign)
	{
		if (index >= 0 && index < (i32)m_Cameras.size())
		{
			if (bAlign)
			{
				AlignCameras(m_Cameras[m_ActiveCameraIndex], m_Cameras[index]);
			}

			m_ActiveCameraIndex = index;

			m_Cameras[m_ActiveCameraIndex]->Initialize(gameContext);
		}
	}

	void CameraManager::SetActiveIndexRelative(const GameContext& gameContext, i32 delta, bool bAlign)
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

		SwtichToIndex(gameContext, newIndex, bAlign);
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
