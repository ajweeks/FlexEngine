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
		BaseCamera("first-person", CameraType::FIRST_PERSON, true, FOV)
	{
		bIsFirstPerson = true;
		bPossessPlayer = true;
		ResetOrientation();
		RecalculateViewProjection();
	}

	FirstPersonCamera::~FirstPersonCamera()
	{
	}

	void FirstPersonCamera::Initialize()
	{
		if (!m_bInitialized)
		{
			if (m_Player == nullptr)
			{
				FindPlayer();
			}

			Update();

			BaseCamera::Initialize();
		}
	}

	void FirstPersonCamera::OnPostSceneChange()
	{
		BaseCamera::OnPostSceneChange();

		FindPlayer();
		TrackPlayer();

		if (m_Player != nullptr)
		{
			m_Player->UpdateIsPossessed();
		}
	}

	void FirstPersonCamera::FixedUpdate()
	{
		TrackPlayer();
	}

	void FirstPersonCamera::TrackPlayer()
	{
		if (m_Player == nullptr)
		{
			return;
		}

		Transform* playerTransform = m_Player->GetTransform();

		forward = m_Player->GetLookDirection();
		right = playerTransform->GetRight();
		up = cross(forward, right);

		//btTransform t;
		//m_Player->GetRigidBody()->GetRigidBodyInternal()->getMotionState()->getWorldTransform(t);
		//position = ToVec3(t.getOrigin());

		position = playerTransform->GetWorldPosition();

		CalculateYawAndPitchFromForward();

		glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

		right = normalize(glm::cross(worldUp, forward));
		up = cross(forward, right);

		RecalculateViewProjection();
	}

	void FirstPersonCamera::FindPlayer()
	{
		m_Player = g_SceneManager->CurrentScene()->GetPlayer(0);
	}

} // namespace flex
