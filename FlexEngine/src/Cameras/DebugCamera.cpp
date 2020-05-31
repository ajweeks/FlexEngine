#include "stdafx.hpp"

#include "Cameras/DebugCamera.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
IGNORE_WARNINGS_POP

#include "Cameras/CameraManager.hpp"
#include "Editor.hpp"
#include "FlexEngine.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Window/Window.hpp"

namespace flex
{
	DebugCamera::DebugCamera(real FOV) :
		BaseCamera("debug", false, FOV),
		mouseButtonCallback(this, &DebugCamera::OnMouseButtonEvent),
		mouseMovedCallback(this, &DebugCamera::OnMouseMovedEvent),
		m_RollOnTurnAmount(1.5f),
		m_MouseDragDist(0.0f),
		m_MoveVel(0.0f),
		m_TurnVel(0.0f)
	{
		ResetOrientation();
		m_DragHistory = Histogram(120);
	}

	DebugCamera::~DebugCamera()
	{
	}

	void DebugCamera::Initialize()
	{
		if (!m_bInitialized)
		{
			RecalculateViewProjection();

			g_InputManager->BindMouseButtonCallback(&mouseButtonCallback, 10);
			g_InputManager->BindMouseMovedCallback(&mouseMovedCallback, 10);

			BaseCamera::Initialize();
		}
	}

	void DebugCamera::Destroy()
	{
		if (m_bInitialized)
		{
			g_InputManager->UnbindMouseButtonCallback(&mouseButtonCallback);
			g_InputManager->UnbindMouseMovedCallback(&mouseMovedCallback);

			BaseCamera::Destroy();
		}
	}

	void DebugCamera::DrawImGuiObjects()
	{
		if (ImGui::TreeNode("Debug Cam"))
		{
			m_DragHistory.DrawImGui();
			ImGui::TreePop();
		}
	}

	void DebugCamera::Update()
	{
		BaseCamera::Update();
		// Override value from base update
		roll = Lerp(roll, 0.0f, rollRestorationSpeed * g_UnpausedDeltaTime);

		m_DragHistory.AddElement(m_MouseDragDist.y);

		glm::vec3 targetDPos(0.0f);

		const bool bModFaster = g_InputManager->GetActionDown(Action::EDITOR_MOD_FASTER) > 0;
		const bool bModSlower = g_InputManager->GetActionDown(Action::EDITOR_MOD_SLOWER) > 0;

		const real moveSpeedMultiplier = bModFaster ? moveSpeedFastMultiplier : bModSlower ? moveSpeedSlowMultiplier : 1.0f;
		const real turnSpeedMultiplier = bModFaster ? turnSpeedFastMultiplier : bModSlower ? turnSpeedSlowMultiplier : 1.0f;

		real lookH = g_InputManager->GetActionAxisValue(Action::DBG_CAM_LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::DBG_CAM_LOOK_RIGHT);
		real lookV = g_InputManager->GetActionAxisValue(Action::DBG_CAM_LOOK_DOWN) + g_InputManager->GetActionAxisValue(Action::DBG_CAM_LOOK_UP);
		real yawO = -lookH * gamepadRotationSpeed * turnSpeedMultiplier * g_UnpausedDeltaTime;
		// Horizontal FOV is roughly twice as wide as vertical
		real pitchO = lookV * 0.6f * gamepadRotationSpeed * turnSpeedMultiplier * g_UnpausedDeltaTime;

		m_TurnVel += glm::vec2(yawO, pitchO);

		yaw += m_TurnVel.x;
		pitch += m_TurnVel.y;
		ClampPitch();
		pitch = glm::clamp(pitch, -glm::pi<real>(), glm::pi<real>());

		CalculateAxisVectorsFromPitchAndYaw();

		// If someone else handled the mouse up event we'll never release
		if (!g_InputManager->IsMouseButtonDown(MouseButton::LEFT))
		{
			m_bDraggingLMB = false;
		}

		// If someone else handled the mouse up event we'll never release
		if (!g_InputManager->IsMouseButtonDown(MouseButton::MIDDLE))
		{
			m_bDraggingMMB = false;
		}

		bool bOrbiting = false;
		glm::vec3 orbitingCenter(0.0f);

		bool bPOribiting = m_bOrbiting;
		m_bOrbiting = g_InputManager->GetActionDown(Action::EDITOR_ORBIT) > 0;

		if (!m_bOrbiting && bPOribiting)
		{
			m_MoveVel = VEC3_ZERO;
			m_TurnVel = VEC2_ZERO;
		}

		if (m_bDraggingLMB)
		{
			if (m_bOrbiting)
			{
				orbitingCenter = g_Editor->GetSelectedObjectsCenter();
				bOrbiting = true;

				float dr = glm::dot(forward, VEC3_UP);
				if (abs(dr) > 0.995f && glm::sign(m_MouseDragDist.y) != glm::sign(dr))

				{
					// Facing nearly entirely up or down, only allow movement around pole (slowed slightly)
					targetDPos += right * (m_MouseDragDist.x * orbitingSpeed * turnSpeedMultiplier * 0.5f);
				}
				else
				{
					targetDPos += right * (m_MouseDragDist.x * orbitingSpeed * turnSpeedMultiplier) +
						up * (m_MouseDragDist.y * orbitingSpeed * turnSpeedMultiplier);
				}
			}
			else
			{
				m_MouseDragDist.y = -m_MouseDragDist.y;

				m_TurnVel += glm::vec2(-m_MouseDragDist.x * mouseRotationSpeed * turnSpeedMultiplier,
					m_MouseDragDist.y * mouseRotationSpeed * turnSpeedMultiplier);

				roll += m_TurnVel.x * m_RollOnTurnAmount * g_UnpausedDeltaTime;

				yaw += m_TurnVel.x * g_UnpausedDeltaTime;
				pitch += m_TurnVel.y * g_UnpausedDeltaTime;
				ClampPitch();
			}
		}

		glm::vec3 translation(0.0f);
		real moveF = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_FORWARD);
		if (moveF != 0.0f)
		{
			translation += forward * moveF;
		}
		real moveB = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_BACKWARD);
		if (moveB != 0.0f)
		{
			translation += forward * moveB;
		}
		real moveL = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_LEFT);
		if (moveL != 0.0f)
		{
			translation += -right * moveL;
		}
		real moveR = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_RIGHT);
		if (moveR != 0.0f)
		{
			translation += -right * moveR;
		}
		real moveU = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_UP);
		if (moveU != 0.0f)
		{
			translation += up * moveU;
		}
		real moveD = g_InputManager->GetActionAxisValue(Action::DBG_CAM_MOVE_DOWN);
		if (moveD != 0.0f)
		{
			translation += up * moveD;
		}

		if (m_bDraggingMMB)
		{
			if (g_InputManager->IsMouseButtonDown(MouseButton::MIDDLE))
			{
				const real multiplier = bModFaster ? panSpeedFastMultiplier : bModSlower ? panSpeedSlowMultiplier : 1.0f;

				glm::vec2 dragDist = g_InputManager->GetMouseDragDistance(MouseButton::MIDDLE) * multiplier;
				glm::vec2 frameBufferSize = (glm::vec2)g_Window->GetFrameBufferSize();
				glm::vec2 normDragDist = dragDist / frameBufferSize;
				position = (m_DragStartPosition + (normDragDist.x * right + normDragDist.y * up) * panSpeed);
			}
		}

		real scrollDistance = g_InputManager->GetVerticalScrollDistance();
		if (scrollDistance != 0.0f && g_Window->HasFocus())
		{
			translation += forward * scrollDistance * scrollDollySpeed;
		}

		if (g_InputManager->IsMouseButtonDown(MouseButton::RIGHT))
		{
			glm::vec2 zoom = g_InputManager->GetMouseMovement();
			translation += forward * -zoom.y * dragDollySpeed * g_UnpausedDeltaTime;
		}

		targetDPos += translation * moveSpeed * moveSpeedMultiplier * g_UnpausedDeltaTime;

		real distFromCenter = glm::length(position - orbitingCenter);

		m_MoveVel += targetDPos;

		position += m_MoveVel;
		m_DragStartPosition += m_MoveVel;

		if (bOrbiting)
		{
			glm::vec3 orientationFromCenter = glm::normalize(position - orbitingCenter);
			position = orbitingCenter + orientationFromCenter * distFromCenter;

			LookAt(orbitingCenter);
		}

		// TODO: Incorporate lag in frame-rate-independent way that doesn't change max vel
		m_MoveVel *= m_MoveLag;
		m_TurnVel *= m_TurnLag;

		CalculateAxisVectorsFromPitchAndYaw();
		RecalculateViewProjection();

		m_MouseDragDist = VEC2_ZERO;
	}

	EventReply DebugCamera::OnMouseButtonEvent(MouseButton button, KeyAction action)
	{
		if (button == MouseButton::LEFT)
		{
			if (action == KeyAction::PRESS)
			{
				m_MouseDragDist = VEC2_ZERO;
				m_bDraggingLMB = true;
				return EventReply::UNCONSUMED;
			}
			else if (action == KeyAction::RELEASE)
			{
				m_MouseDragDist = VEC2_ZERO;
				m_bDraggingLMB = false;
				m_MoveVel = VEC3_ZERO;
				m_TurnVel = VEC2_ZERO;
				return EventReply::UNCONSUMED;
			}
		}
		else if (button == MouseButton::MIDDLE)
		{
			if (action == KeyAction::PRESS)
			{
				m_DragStartPosition = position;
				m_bDraggingMMB = true;
				return EventReply::CONSUMED;
			}
			else
			{
				m_bDraggingMMB = false;
				return EventReply::CONSUMED;
			}
		}
		return EventReply::UNCONSUMED;
	}

	EventReply DebugCamera::OnMouseMovedEvent(const glm::vec2& dMousePos)
	{
		if (m_bDraggingLMB)
		{
			m_MouseDragDist = dMousePos;
			return EventReply::CONSUMED;
		}
		return EventReply::UNCONSUMED;
	}

} // namespace flex
