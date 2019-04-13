#include "stdafx.hpp"

#include "Cameras/FirstPersonCamera.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
IGNORE_WARNINGS_POP

#include "Helpers.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	FirstPersonCamera::FirstPersonCamera(real FOV) :
		BaseCamera("first-person", true, FOV)
	{
		ResetOrientation();
		RecalculateViewProjection();
	}

	FirstPersonCamera::~FirstPersonCamera()
	{
	}

	void FirstPersonCamera::Initialize()
	{
		if (m_bInitialized)
		{
			if (m_Player == nullptr)
			{
				FindPlayer();
			}

			if (!m_bInitialized)
			{
				m_bInitialized = true;

				BaseCamera::Initialize();

				Update();
			}

			m_bInitialized = true;
		}
	}

	void FirstPersonCamera::OnSceneChanged()
	{
		BaseCamera::OnSceneChanged();

		FindPlayer();
		Update();

		if (m_Player)
		{
			m_Player->UpdateIsPossessed();
		}
	}

	void FirstPersonCamera::Update()
	{
		if (!m_Player)
		{
			return;
		}

		Transform* playerTransform = m_Player->GetTransform();

		m_Forward = m_Player->GetLookDirection();
		m_Right = playerTransform->GetRight();
		m_Up = cross(m_Forward, m_Right);

		m_Position = playerTransform->GetWorldPosition();

		CalculateYawAndPitchFromForward();

		glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

		m_Right = normalize(glm::cross(worldUp, m_Forward));
		m_Up = cross(m_Forward, m_Right);

		RecalculateViewProjection();
	}

	void FirstPersonCamera::FindPlayer()
	{
		m_Player = static_cast<Player*>(g_SceneManager->CurrentScene()->FirstObjectWithTag("Player0"));
	}

} // namespace flex
