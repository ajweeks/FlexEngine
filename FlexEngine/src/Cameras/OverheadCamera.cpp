#include "stdafx.hpp"

#include "Cameras/OverheadCamera.hpp"

#pragma warning(push, 0)
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#pragma warning(pop)

#include "Helpers.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	OverheadCamera::OverheadCamera(real FOV, real zNear, real zFar) :
		BaseCamera("overhead",FOV, zNear, zFar)
	{
		ResetOrientation();
		RecalculateViewProjection();
	}

	OverheadCamera::~OverheadCamera()
	{
	}

	void OverheadCamera::Initialize()
	{
		FindPlayers();
		Update();

		BaseCamera::Initialize();
	}

	void OverheadCamera::OnSceneChanged()
	{
		FindPlayers();
		Update();
	}

	void OverheadCamera::Update()
	{
		if (!player0 ||
			!player1)
		{
			return;
		}

		glm::vec3 dPlayerPos = player0->GetTransform()->GetWorldPosition() -
			player1->GetTransform()->GetWorldPosition();
		real dist = glm::length(dPlayerPos);

		glm::vec3 targetSpot = (player0->GetTransform()->GetWorldPosition() +
								player1->GetTransform()->GetWorldPosition()) / 2.0f;

		m_Forward = glm::normalize(targetSpot - m_Position);

		glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
		m_Right = normalize(glm::cross(worldUp, m_Forward));
		m_Up = cross(m_Forward, m_Right);

		real minHeight = 25.0f;
		real maxHeight = 50.0f;
		real minDistBack = 20.0f;
		real maxDistBack = 50.0f;

		real targetYO = glm::clamp(dist * 1.0f, minHeight, maxHeight);
		real targetZO = glm::clamp(dist * 1.0f, minDistBack, maxDistBack);

		glm::vec3 upVec = glm::vec3(0, 1, 0);
		glm::vec3 backVec = glm::vec3(0, 0, -1);

		glm::vec3 targetPos = targetSpot +
			upVec * targetYO +
			backVec * targetZO;
		glm::vec3 dPos = targetPos - m_Position;
		real dPosMag = glm::length(dPos);
		if (dPosMag > glm::epsilon<real>())
		{
			real maxDist = glm::clamp(dPosMag * 1000.0f * g_DeltaTime, 0.0f, 10.0f);
			m_Position = targetPos - maxDist * glm::normalize(dPos);
		}

		CalculateYawAndPitchFromForward();
		RecalculateViewProjection();
	}

	void OverheadCamera::FindPlayers()
	{
		player0 = g_SceneManager->CurrentScene()->FirstObjectWithTag("Player0");
		player1 = g_SceneManager->CurrentScene()->FirstObjectWithTag("Player1");
	}

} // namespace flex
