#include "stdafx.hpp"

#include "Cameras/FirstPersonCamera.hpp"

#pragma warning(push, 0)
#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

#include "Helpers.hpp"
#include "Scene/BaseScene.hpp" 
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	FirstPersonCamera::FirstPersonCamera(real FOV, real zNear, real zFar) :
		BaseCamera("First Person Camera",FOV, zNear, zFar)
	{
		ResetOrientation();
		RecalculateViewProjection();
	}

	FirstPersonCamera::~FirstPersonCamera()
	{
	}

	void FirstPersonCamera::Initialize()
	{
		FindPlayer();
		Update();

		BaseCamera::Initialize();
	}

	void FirstPersonCamera::OnSceneChanged()
	{
		FindPlayer();
		Update();
	}

	void FirstPersonCamera::Update()
	{
		if (!player)
		{
			return;
		}

		glm::vec3 targetSpot = player->GetTransform()->GetWorldPosition();

		m_Forward = player->GetTransform()->GetForward();

		glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
		m_Right = normalize(glm::cross(worldUp, m_Forward));
		m_Up = cross(m_Forward, m_Right);

		m_Position = player->GetTransform()->GetWorldPosition();

		CalculateYawAndPitchFromForward();
		RecalculateViewProjection();
	}

	void FirstPersonCamera::FindPlayer()
	{
		player = g_SceneManager->CurrentScene()->FirstObjectWithTag("Player0");
	}

} // namespace flex
