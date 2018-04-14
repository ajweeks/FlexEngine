
#include "stdafx.hpp"

#include "Cameras/CameraManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "GameContext.hpp"
#include "Logger.hpp"

namespace flex
{
	CameraManager::CameraManager()
	{
	}

	CameraManager::~CameraManager()
	{
		for (u32 i = 0; i < m_Cameras.size(); ++i)
		{
			SafeDelete(m_Cameras[i]);
		}
		m_Cameras.clear();
	}

	void CameraManager::Update(const GameContext& gameContext)
	{
		m_Cameras[m_ActiveCameraIndex]->Update(gameContext);
	}

	BaseCamera* CameraManager::CurrentCamera() const
	{
		return m_Cameras[m_ActiveCameraIndex];
	}

	i32 CameraManager::CameraCount() const
	{
		return (i32)m_Cameras.size();
	}

	void CameraManager::AddCamera(BaseCamera* camera, bool switchTo)
	{
		assert(camera);

		i32 cameraIndex = GetCameraIndex(camera);
		if (cameraIndex == -1) // Only add camera if it hasn't been added before
		{
			m_Cameras.push_back(camera);

			if (switchTo || m_ActiveCameraIndex == -1)
			{
				m_ActiveCameraIndex = (m_Cameras.size() - 1);
			}
		}
	}

	void CameraManager::SwtichTo(BaseCamera* camera, bool align)
	{
		assert(camera);

		i32 newCameraIndex = GetCameraIndex(camera);

		if (newCameraIndex == -1)
		{
			Logger::LogError("Attempted to switch to camera which doesn't exist! " + camera->GetName());
		}
		else
		{
			if (align)
			{
				AlignCameras(m_Cameras[m_ActiveCameraIndex], camera);
			}

			m_ActiveCameraIndex = newCameraIndex;
		}
	}

	void CameraManager::SwtichToIndex(i32 index, bool align)
	{
		if (index >= 0 && index < (i32)m_Cameras.size())
		{
			if (align)
			{
				AlignCameras(m_Cameras[m_ActiveCameraIndex], m_Cameras[index]);
			}

			m_ActiveCameraIndex = index;
		}
	}

	void CameraManager::SwtichToIndexRelative(i32 delta, bool align)
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

		SwtichToIndex(newIndex, align);
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
