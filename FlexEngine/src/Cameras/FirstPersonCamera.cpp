#include "stdafx.hpp"

#include "Cameras/FirstPersonCamera.hpp"

#pragma warning(push, 0)
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#pragma warning(pop)

#include "Helpers.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	FirstPersonCamera::FirstPersonCamera(real FOV, real zNear, real zFar) :
		BaseCamera("first-person",FOV, zNear, zFar)
	{
		ResetOrientation();
		RecalculateViewProjection();
	}

	FirstPersonCamera::~FirstPersonCamera()
	{
	}

	void FirstPersonCamera::Initialize()
	{
		BaseCamera::Initialize();

		FindPlayer();
		Update();
	}

	void FirstPersonCamera::OnSceneChanged()
	{
		BaseCamera::OnSceneChanged();

		FindPlayer();
		Update();
	}

	void FirstPersonCamera::Update()
	{
		if (!player)
		{
			return;
		}

		Transform* playerTransform = player->GetTransform();

		glm::vec3 targetSpot = playerTransform->GetWorldPosition();

		m_Forward = player->GetLookDirection();
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
		player = (Player*)(g_SceneManager->CurrentScene()->FirstObjectWithTag("Player0"));
	}

} // namespace flex
