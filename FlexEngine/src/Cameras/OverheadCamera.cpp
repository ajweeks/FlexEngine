#include "stdafx.hpp"

#include "Cameras/OverheadCamera.hpp"

#pragma warning(push, 0)
#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

#include "Helpers.hpp"
#include "Logger.hpp"
#include "Scene/BaseScene.hpp" 
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	OverheadCamera::OverheadCamera(GameContext& gameContext, real FOV, real zNear, real zFar) :
		BaseCamera("Overhead Camera", gameContext, FOV, zNear, zFar)
	{
		ResetOrientation();
		RecalculateViewProjection(gameContext);
	}

	OverheadCamera::~OverheadCamera()
	{
	}

	void OverheadCamera::Initialize(const GameContext& gameContext)
	{
		player0 = gameContext.sceneManager->CurrentScene()->FirstObjectWithTag("Player0");
		player1 = gameContext.sceneManager->CurrentScene()->FirstObjectWithTag("Player1");

		Update(gameContext);

		BaseCamera::Initialize(gameContext);
	}

	void OverheadCamera::Update(const GameContext& gameContext)
	{
		glm::vec3 dPlayerPos = player0->GetTransform()->GetWorldlPosition() -
			player1->GetTransform()->GetWorldlPosition();
		real dist = glm::length(dPlayerPos);

		glm::vec3 targetSpot = (player0->GetTransform()->GetWorldlPosition() +
								player1->GetTransform()->GetWorldlPosition()) / 2.0f;

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
			real maxDist = glm::clamp(dPosMag * 1000.0f * gameContext.deltaTime, 0.0f, 10.0f);
			m_Position = targetPos - maxDist * glm::normalize(dPos);
		}

		CalculateYawAndPitchFromForward();
		RecalculateViewProjection(gameContext);
	}
} // namespace flex
