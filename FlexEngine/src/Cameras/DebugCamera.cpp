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
		BaseCamera("debug", false, FOV, zNear, zFar),
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

		const bool bModFaster = g_InputManager->GetActionDown(Action::EDITOR_MOD_FASTER);
		const bool bModSlower = g_InputManager->GetActionDown(Action::EDITOR_MOD_SLOWER);

		const real moveSpeedMultiplier = bModFaster ? m_MoveSpeedFastMultiplier : bModSlower ? m_MoveSpeedSlowMultiplier : 1.0f;
		const real turnSpeedMultiplier = bModFaster ? m_TurnSpeedFastMultiplier : bModSlower ? m_TurnSpeedSlowMultiplier : 1.0f;

		real lookH = g_InputManager->GetActionAxisValue(Action::DBG_CAM_LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::DBG_CAM_LOOK_RIGHT);
		real lookV = g_InputManager->GetActionAxisValue(Action::DBG_CAM_LOOK_DOWN) + g_InputManager->GetActionAxisValue(Action::DBG_CAM_LOOK_UP);
		real yawO = lookH * m_GamepadRotationSpeed * turnSpeedMultiplier * g_DeltaTime;
		// Horizontal FOV is roughly twice as wide as vertical
		real pitchO = lookV * 0.6f * m_GamepadRotationSpeed * turnSpeedMultiplier * g_DeltaTime;

		m_TurnVel += glm::vec2(yawO, pitchO);

		m_Yaw += m_TurnVel.x;
		m_Pitch += m_TurnVel.y;
		ClampPitch();
		m_Pitch = glm::clamp(m_Pitch, -glm::pi<real>(), glm::pi<real>());

		CalculateAxisVectorsFromPitchAndYaw();

		// If someone else handled the mouse up event we'll never release
		if (!g_InputManager->IsMouseButtonDown(MouseButton::LEFT))
		{
			m_bDraggingMouse = false;
		}

		bool bPOribiting = m_bOrbiting;
		m_bOrbiting = g_InputManager->GetActionDown(Action::EDITOR_ORBIT);

		if (bOrbiting == false && bPOribiting == true)
		{
			m_MoveVel = VEC3_ZERO;
			m_TurnVel = VEC2_ZERO;
		}

		if (m_bDraggingMouse)
		{
			if (m_bOrbiting)
			{
				orbitingCenter = g_EngineInstance->GetSelectedObjectsCenter();
				bOrbiting = true;
				targetDPos += m_Right * -m_MouseDragDist.x * m_OrbitingSpeed * turnSpeedMultiplier +
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
		real moveF = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_FORWARD);
		if (moveF != 0.0f)
		{
			translation += m_Forward * moveF;
		}
		real moveB = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_BACKWARD);
		if (moveB != 0.0f)
		{
			translation += m_Forward * moveB;
		}
		real moveL = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_LEFT);
		if (moveL != 0.0f)
		{
			translation += m_Right * moveL;
		}
		real moveR = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_RIGHT);
		if (moveR != 0.0f)
		{
			translation += m_Right * moveR;
		}
		real moveU = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_UP);
		if (moveU != 0.0f)
		{
			translation += m_Up * moveU;
		}
		real moveD = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_DOWN);
		if (moveD != 0.0f)
		{
			translation += m_Up * moveD;
		}

		// TODO: Handle in action callback
		if (g_InputManager->IsMouseButtonPressed(MouseButton::MIDDLE))
		{
			m_DragStartPosition = m_Position;
		}
		else
		{
			// TODO: Handle in action callback
			if (g_InputManager->IsMouseButtonDown(MouseButton::MIDDLE))
			{
				glm::vec2 dragDist = g_InputManager->GetMouseDragDistance(MouseButton::MIDDLE);
				glm::vec2 frameBufferSize = (glm::vec2)g_Window->GetFrameBufferSize();
				glm::vec2 normDragDist = dragDist / frameBufferSize;
				m_Position = (m_DragStartPosition + (-normDragDist.x * m_Right + normDragDist.y * m_Up) * m_PanSpeed);
			}
		}

		real scrollDistance = g_InputManager->GetVerticalScrollDistance();
		if (scrollDistance != 0.0f)
		{
			translation += m_Forward * scrollDistance * m_ScrollDollySpeed;
		}

		// TODO: Handle in action callback
		if (g_InputManager->IsMouseButtonDown(MouseButton::RIGHT))
		{
			glm::vec2 zoom = g_InputManager->GetMouseMovement();
			translation += m_Forward * -zoom.y * m_DragDollySpeed;
		}

		targetDPos += translation * m_MoveSpeed * moveSpeedMultiplier * g_DeltaTime;

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

		m_MouseDragDist = VEC2_ZERO;
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
				m_MouseDragDist = VEC2_ZERO;
				m_bDraggingMouse = true;
				return EventReply::UNCONSUMED;
			}
			else if (action == KeyAction::RELEASE)
			{
				m_MouseDragDist = VEC2_ZERO;
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
