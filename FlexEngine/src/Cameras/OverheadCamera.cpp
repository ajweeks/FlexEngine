#include "stdafx.hpp"

#include "Cameras/OverheadCamera.hpp"

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Logger.hpp"
#include "Window/Window.hpp"
#include "Helpers.hpp"

namespace flex
{
	OverheadCamera::OverheadCamera(GameContext& gameContext, real FOV, real zNear, real zFar) :
		BaseCamera("Overhead Camera", gameContext, FOV, zNear, zFar)
	{
		ResetOrientation();
		RecalculateViewProjection(gameContext);

		LoadDefaultKeybindings();
	}

	OverheadCamera::~OverheadCamera()
	{
	}

	void OverheadCamera::Update(const GameContext& gameContext)
	{
		glm::vec2 look(0.0f);
		if (gameContext.inputManager->GetMouseButtonDown(InputManager::MouseButton::LEFT))
		{
			look = gameContext.inputManager->GetMouseMovement();
			look.y = -look.y;

			m_Yaw += look.x * m_RotationSpeed;
			m_Pitch += look.y * m_RotationSpeed;

			real pitchLimit = PI - 0.02f;
			if (m_Pitch > pitchLimit)
			{
				m_Pitch = pitchLimit;
			}
			else if (m_Pitch < -pitchLimit)
			{
				m_Pitch = -pitchLimit;
			}
		}
		
		m_Forward = {};
		m_Forward.x = cos(m_Pitch) * cos(m_Yaw);
		m_Forward.y = sin(m_Pitch);
		m_Forward.z = cos(m_Pitch) * sin(m_Yaw);
		m_Forward = normalize(m_Forward);

		glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

		m_Right = normalize(glm::cross(worldUp, m_Forward));
		m_Up = cross(m_Forward, m_Right);

		glm::vec3 translation(0.0f);
		if (gameContext.inputManager->GetKeyDown(m_MoveForwardKey))
		{
			translation += m_Forward;
		}
		if (gameContext.inputManager->GetKeyDown(m_MoveBackwardKey))
		{
			translation -= m_Forward;
		}
		if (gameContext.inputManager->GetKeyDown(m_MoveLeftKey))
		{
			translation += m_Right;
		}
		if (gameContext.inputManager->GetKeyDown(m_MoveRightKey))
		{
			translation -= m_Right;
		}
		if (gameContext.inputManager->GetKeyDown(m_MoveUpKey))
		{
			translation += m_Up;
		}
		if (gameContext.inputManager->GetKeyDown(m_MoveDownKey))
		{
			translation -= m_Up;
		}

		if (gameContext.inputManager->GetMouseButtonClicked(InputManager::MouseButton::MIDDLE))
		{
			m_DragStartPosition = m_Position;
		}
		else if (gameContext.inputManager->GetMouseButtonDown(InputManager::MouseButton::MIDDLE))
		{
			glm::vec2 dragDist = gameContext.inputManager->GetMouseDragDistance(InputManager::MouseButton::MIDDLE);
			glm::vec2 frameBufferSize = (glm::vec2)gameContext.window->GetFrameBufferSize();
			glm::vec2 normDragDist = dragDist / frameBufferSize;
			m_Position = (m_DragStartPosition + (normDragDist.x * m_Right + normDragDist.y * m_Up) * m_PanSpeed);
		}

		real scrollDistance = gameContext.inputManager->GetVerticalScrollDistance();
		if (scrollDistance != 0.0f)
		{
			translation += m_Forward * scrollDistance * m_ScrollDollySpeed;
		}

		if (gameContext.inputManager->GetMouseButtonDown(InputManager::MouseButton::RIGHT))
		{
			glm::vec2 zoom = gameContext.inputManager->GetMouseMovement();
			translation += m_Forward * -zoom.y * m_DragDollySpeed;
		}

		real speedMultiplier = 1.0f;
		if (gameContext.inputManager->GetKeyDown(m_MoveFasterKey))
		{
			speedMultiplier = m_MoveSpeedFastMultiplier;
		}
		else if (gameContext.inputManager->GetKeyDown(m_MoveSlowerKey))
		{
			speedMultiplier = m_MoveSpeedSlowMultiplier;
		}

		glm::vec3 finalTranslation = translation * m_MoveSpeed * speedMultiplier * gameContext.deltaTime;
		Translate(finalTranslation);
		m_DragStartPosition += finalTranslation;

		RecalculateViewProjection(gameContext);
	}

	void OverheadCamera::LoadDefaultKeybindings()
	{
		m_MoveForwardKey = InputManager::KeyCode::KEY_W;
		m_MoveBackwardKey = InputManager::KeyCode::KEY_S;
		m_MoveLeftKey = InputManager::KeyCode::KEY_A;
		m_MoveRightKey = InputManager::KeyCode::KEY_D;
		m_MoveUpKey = InputManager::KeyCode::KEY_E;
		m_MoveDownKey = InputManager::KeyCode::KEY_Q;
		m_MoveFasterKey = InputManager::KeyCode::KEY_LEFT_SHIFT;
		m_MoveSlowerKey = InputManager::KeyCode::KEY_LEFT_CONTROL;
	}

	void OverheadCamera::LoadAzertyKeybindings()
	{
		m_MoveForwardKey = InputManager::KeyCode::KEY_Z;
		m_MoveBackwardKey = InputManager::KeyCode::KEY_S;
		m_MoveLeftKey = InputManager::KeyCode::KEY_Q;
		m_MoveRightKey = InputManager::KeyCode::KEY_D;
		m_MoveUpKey = InputManager::KeyCode::KEY_E;
		m_MoveDownKey = InputManager::KeyCode::KEY_A;
		m_MoveFasterKey = InputManager::KeyCode::KEY_LEFT_SHIFT;
		m_MoveSlowerKey = InputManager::KeyCode::KEY_LEFT_CONTROL;
	}
} // namespace flex
