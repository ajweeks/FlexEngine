#include "stdafx.hpp"

#include "Cameras/DebugCamera.hpp"

#pragma warning(push, 0)
#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

#include "Cameras/CameraManager.hpp"
#include "Helpers.hpp"
#include "Logger.hpp"
#include "Window/Window.hpp"

namespace flex
{
	DebugCamera::DebugCamera(GameContext& gameContext, real FOV, real zNear, real zFar) :
		BaseCamera("Debug Camera", gameContext, FOV, zNear, zFar)
	{
		ResetOrientation();
		RecalculateViewProjection(gameContext);

		LoadDefaultKeybindings();
	}

	DebugCamera::~DebugCamera()
	{
	}

	void DebugCamera::Update(const GameContext& gameContext)
	{
		if (m_EnableGamepadMovement)
		{
			real yawO = gameContext.inputManager->GetGamepadAxisValue(0, InputManager::GamepadAxis::RIGHT_STICK_X) *
				m_GamepadRotationSpeed * gameContext.deltaTime;
			real pitchO = -gameContext.inputManager->GetGamepadAxisValue(0, InputManager::GamepadAxis::RIGHT_STICK_Y) *
				m_GamepadRotationSpeed * gameContext.deltaTime;
			m_Yaw += yawO * gameContext.deltaTime;
			m_Pitch += pitchO * gameContext.deltaTime;
			ClampPitch();
			m_Pitch = glm::clamp(m_Pitch, -glm::pi<real>(), glm::pi<real>());

			CalculateAxisVectors();

			real posYO = -gameContext.inputManager->GetGamepadAxisValue(0, InputManager::GamepadAxis::LEFT_TRIGGER) *
				m_MoveSpeed * 0.5f * gameContext.deltaTime;
			posYO += gameContext.inputManager->GetGamepadAxisValue(0, InputManager::GamepadAxis::RIGHT_TRIGGER) *
				m_MoveSpeed * 0.5f * gameContext.deltaTime;
			real posXO = -gameContext.inputManager->GetGamepadAxisValue(0, InputManager::GamepadAxis::LEFT_STICK_X) *
				m_MoveSpeed * gameContext.deltaTime;
			real posZO = -gameContext.inputManager->GetGamepadAxisValue(0, InputManager::GamepadAxis::LEFT_STICK_Y) *
				m_MoveSpeed * gameContext.deltaTime;

			bool moveFast = gameContext.inputManager->IsGamepadButtonDown(0, InputManager::GamepadButton::RIGHT_BUMPER);
			real moveSpeedMultiplier = moveFast ? m_MoveSpeedFastMultiplier : 1.0f;
			m_Position +=
				m_Right * posXO * moveSpeedMultiplier +
				m_Up * posYO * moveSpeedMultiplier +
				m_Forward * posZO * moveSpeedMultiplier;
		}

		if (m_EnableKeyboardMovement)
		{
			glm::vec2 look(0.0f);
			if (gameContext.inputManager->GetMouseButtonDown(InputManager::MouseButton::LEFT))
			{
				look = gameContext.inputManager->GetMouseMovement();
				look.y = -look.y;

				m_Yaw += look.x * m_MouseRotationSpeed;
				m_Pitch += look.y * m_MouseRotationSpeed;

				ClampPitch();
			}

			CalculateAxisVectors();

			glm::vec3 translation(0.0f);
			if (!gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_CONTROL))
			{
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
		}

		RecalculateViewProjection(gameContext);
	}

	void DebugCamera::LoadDefaultKeybindings()
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

} // namespace flex
