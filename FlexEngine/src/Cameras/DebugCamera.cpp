#include "stdafx.hpp"

#include "Cameras/DebugCamera.hpp"

#pragma warning(push, 0)
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#pragma warning(pop)

#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Window/Window.hpp"

namespace flex
{
	DebugCamera::DebugCamera(real FOV, real zNear, real zFar) :
		BaseCamera("debug", FOV, zNear, zFar),
		m_MoveVel(0.0f),
		m_TurnVel(0.0f),
		mouseButtonCallback(this, &DebugCamera::OnMouseButtonEvent),
		mouseMovedCallback(this, &DebugCamera::OnMouseMovedEvent),
		m_MouseDragDist(0.0f)
	{
	}

	DebugCamera::~DebugCamera()
	{
	}

	void DebugCamera::Initialize()
	{
		if (!m_bInitialized)
		{
			ResetOrientation();
			RecalculateViewProjection();

			LoadDefaultKeybindings();

			g_InputManager->BindMouseButtonCallback(&mouseButtonCallback, 10);
			g_InputManager->BindMouseMovedCallback(&mouseMovedCallback, 10);

			m_bInitialized = true;
		}
	}

	void DebugCamera::Destroy()
	{
		if (m_bInitialized)
		{
			g_InputManager->UnbindMouseButtonCallback(&mouseButtonCallback);
			g_InputManager->UnbindMouseMovedCallback(&mouseMovedCallback);
			m_bInitialized = false;
		}
	}

	void DebugCamera::Update()
	{
		glm::vec3 targetDPos(0.0f);

		bool bOrbiting = false;
		glm::vec3 orbitingCenter(0.0f);

		if (m_bEnableGamepadMovement)
		{
			bool turnFast = g_InputManager->IsGamepadButtonDown(0, GamepadButton::RIGHT_BUMPER);
			bool turnSlow = g_InputManager->IsGamepadButtonDown(0, GamepadButton::LEFT_BUMPER);
			real turnSpeedMultiplier = turnFast ? m_TurnSpeedFastMultiplier : turnSlow ? m_TurnSpeedSlowMultiplier : 1.0f;

			real yawO = g_InputManager->GetGamepadAxisValue(0, GamepadAxis::RIGHT_STICK_X) *
				m_GamepadRotationSpeed * turnSpeedMultiplier * g_DeltaTime;
			real pitchO = -g_InputManager->GetGamepadAxisValue(0, GamepadAxis::RIGHT_STICK_Y) *
				m_GamepadRotationSpeed * turnSpeedMultiplier * g_DeltaTime;

			m_TurnVel += glm::vec2(yawO, pitchO);

			m_Yaw += m_TurnVel.x;
			m_Pitch += m_TurnVel.y;
			ClampPitch();
			m_Pitch = glm::clamp(m_Pitch, -glm::pi<real>(), glm::pi<real>());

			CalculateAxisVectorsFromPitchAndYaw();

			bool moveFast = g_InputManager->IsGamepadButtonDown(0, GamepadButton::RIGHT_BUMPER);
			bool moveSlow = g_InputManager->IsGamepadButtonDown(0, GamepadButton::LEFT_BUMPER);
			real moveSpeedMultiplier = moveFast ? m_MoveSpeedFastMultiplier : moveSlow ? m_MoveSpeedSlowMultiplier : 1.0f;

			real posYO = -g_InputManager->GetGamepadAxisValue(0, GamepadAxis::LEFT_TRIGGER) *
				m_MoveSpeed * 0.5f * g_DeltaTime;
			posYO += g_InputManager->GetGamepadAxisValue(0, GamepadAxis::RIGHT_TRIGGER) *
				m_MoveSpeed * 0.5f * g_DeltaTime;
			real posXO = -g_InputManager->GetGamepadAxisValue(0, GamepadAxis::LEFT_STICK_X) *
				m_MoveSpeed * g_DeltaTime;
			real posZO = -g_InputManager->GetGamepadAxisValue(0, GamepadAxis::LEFT_STICK_Y) *
				m_MoveSpeed * g_DeltaTime;

			targetDPos +=
				m_Right * posXO * moveSpeedMultiplier +
				m_Up * posYO * moveSpeedMultiplier +
				m_Forward * posZO * moveSpeedMultiplier;
		}

		// If someone else handled the mouse up event we'll never release
		if (!g_InputManager->IsMouseButtonDown(MouseButton::LEFT))
		{
			m_bDraggingMouse = false;
		}

		if (m_bEnableKeyboardMovement)
		{
			bool bAltDown = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_ALT) > 0;

			if (m_bDraggingMouse)
			{
				real turnSpeedMultiplier = 1.0f;
				if (g_InputManager->GetKeyDown(m_MoveFasterKey))
				{
					turnSpeedMultiplier = m_TurnSpeedFastMultiplier;
				}
				else if (g_InputManager->GetKeyDown(m_MoveSlowerKey))
				{
					turnSpeedMultiplier = m_TurnSpeedSlowMultiplier;
				}

				if (bAltDown)
				{
					orbitingCenter = g_EngineInstance->GetSelectedObjectsCenter();
					bOrbiting = true;
					targetDPos += m_Right * m_MouseDragDist.x * m_OrbitingSpeed * turnSpeedMultiplier +
						m_Up * m_MouseDragDist.y * m_OrbitingSpeed * turnSpeedMultiplier;
				}
				else
				{
					m_MouseDragDist.y = -m_MouseDragDist.y;

					m_TurnVel += glm::vec2(m_MouseDragDist.x * m_MouseRotationSpeed * turnSpeedMultiplier,
						m_MouseDragDist.y * m_MouseRotationSpeed * turnSpeedMultiplier);

					m_Yaw += m_TurnVel.x;
					m_Pitch += m_TurnVel.y;
					ClampPitch();
				}
			}

			CalculateAxisVectorsFromPitchAndYaw();

			glm::vec3 translation(0.0f);
			if (g_InputManager->GetKeyDown(m_MoveForwardKey))
			{
				translation += m_Forward;
			}
			if (g_InputManager->GetKeyDown(m_MoveBackwardKey))
			{
				translation -= m_Forward;
			}
			if (g_InputManager->GetKeyDown(m_MoveLeftKey))
			{
				translation += m_Right;
			}
			if (g_InputManager->GetKeyDown(m_MoveRightKey))
			{
				translation -= m_Right;
			}
			if (g_InputManager->GetKeyDown(m_MoveUpKey))
			{
				translation += m_Up;
			}
			if (g_InputManager->GetKeyDown(m_MoveDownKey))
			{
				translation -= m_Up;
			}

			if (g_InputManager->IsMouseButtonPressed(MouseButton::MIDDLE))
			{
				m_DragStartPosition = m_Position;
			}
			else if (g_InputManager->IsMouseButtonDown(MouseButton::MIDDLE))
			{
				glm::vec2 dragDist = g_InputManager->GetMouseDragDistance(MouseButton::MIDDLE);
				glm::vec2 frameBufferSize = (glm::vec2)g_Window->GetFrameBufferSize();
				glm::vec2 normDragDist = dragDist / frameBufferSize;
				m_Position = (m_DragStartPosition + (normDragDist.x * m_Right + normDragDist.y * m_Up) * m_PanSpeed);
			}

			real scrollDistance = g_InputManager->GetVerticalScrollDistance();
			if (scrollDistance != 0.0f)
			{
				translation += m_Forward * scrollDistance * m_ScrollDollySpeed;
			}

			if (g_InputManager->IsMouseButtonDown(MouseButton::RIGHT))
			{
				glm::vec2 zoom = g_InputManager->GetMouseMovement();
				translation += m_Forward * -zoom.y * m_DragDollySpeed;
			}

			real moveSpeedMultiplier = 1.0f;
			if (g_InputManager->GetKeyDown(m_MoveFasterKey))
			{
				moveSpeedMultiplier = m_MoveSpeedFastMultiplier;
			}
			else if (g_InputManager->GetKeyDown(m_MoveSlowerKey))
			{
				moveSpeedMultiplier = m_MoveSpeedSlowMultiplier;
			}


			targetDPos += translation * m_MoveSpeed * moveSpeedMultiplier * g_DeltaTime;
		}

		real distFromCenter = glm::length(m_Position - orbitingCenter);

		m_MoveVel += targetDPos;

		// TODO: * deltaTime?
		m_Position += m_MoveVel;
		m_DragStartPosition += m_MoveVel;

		if (bOrbiting)
		{
			glm::vec3 orientationFromCenter = glm::normalize(m_Position - orbitingCenter);
			m_Position = orbitingCenter + orientationFromCenter * distFromCenter;

			LookAt(orbitingCenter);
		}

		m_MoveVel *= m_MoveLag;
		m_TurnVel *= m_TurnLag;

		RecalculateViewProjection();

		m_MouseDragDist = glm::vec2(0.0f);
	}

	void DebugCamera::LoadDefaultKeybindings()
	{
		m_MoveForwardKey = KeyCode::KEY_W;
		m_MoveBackwardKey = KeyCode::KEY_S;
		m_MoveLeftKey = KeyCode::KEY_A;
		m_MoveRightKey = KeyCode::KEY_D;
		m_MoveUpKey = KeyCode::KEY_E;
		m_MoveDownKey = KeyCode::KEY_Q;
		m_MoveFasterKey = KeyCode::KEY_LEFT_SHIFT;
		m_MoveSlowerKey = KeyCode::KEY_LEFT_CONTROL;
	}

	bool DebugCamera::IsDebugCam() const
	{
		return true;
	}

	EventReply DebugCamera::OnMouseButtonEvent(MouseButton button, KeyAction action)
	{
		if (button == MouseButton::LEFT)
		{
			if (action == KeyAction::PRESS)
			{
				m_bDraggingMouse = true;
				return EventReply::UNCONSUMED;
			}
			else if (action == KeyAction::RELEASE)
			{
				m_bDraggingMouse = false;
				return EventReply::UNCONSUMED;
			}
		}

		return EventReply::UNCONSUMED;
	}

	EventReply DebugCamera::OnMouseMovedEvent(const glm::vec2& dMousePos)
	{
		if (m_bDraggingMouse)
		{
			m_MouseDragDist = dMousePos;
			return EventReply::CONSUMED;
		}
		return EventReply::UNCONSUMED;
	}

} // namespace flex
