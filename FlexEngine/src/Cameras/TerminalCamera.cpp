#include "stdafx.hpp"

#include "Cameras/TerminalCamera.hpp"

#include "Cameras/CameraManager.hpp"
#include "Helpers.hpp" // For MoveTowards
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	TerminalCamera::TerminalCamera(real FOV, real zNear, real zFar) :
		BaseCamera("terminal", true, FOV, zNear, zFar)
	{
	}

	void TerminalCamera::Update()
	{
		if (m_bTransitioningIn || m_bTransitioningOut)
		{
			m_Yaw = MoveTowards(m_Yaw, m_TargetYaw, g_DeltaTime * m_LerpSpeed);
			m_Pitch = MoveTowards(m_Pitch, m_TargetPitch, g_DeltaTime * m_LerpSpeed);
			m_Position = MoveTowards(m_Position, m_TargetPos, g_DeltaTime * m_LerpSpeed);

			if (NearlyEquals(m_Yaw, m_TargetYaw, 0.01f) &&
				NearlyEquals(m_Pitch, m_TargetPitch, 0.01f) &&
				NearlyEquals(m_Position, m_TargetPos, 0.01f))
			{
				if (m_bTransitioningIn)
				{
					m_bTransitioningIn = false;
				}
				if (m_bTransitioningOut)
				{
					m_bTransitioningOut = false;
					m_Terminal->SetInteractingWith(nullptr);
					g_SceneManager->CurrentScene()->GetPlayer(0)->SetInteractingWith(nullptr);
					g_CameraManager->PopCamera();
				}
			}
		}

		CalculateAxisVectorsFromPitchAndYaw();
		RecalculateViewProjection();
	}

	void TerminalCamera::SetTerminal(Terminal* terminal)
	{
		if (terminal == nullptr)
		{
			m_Terminal->SetCamera(nullptr);
			m_Terminal = nullptr;
		}
		else
		{
			m_Terminal = terminal;
			m_Terminal->SetCamera(this);

			m_StartingPitch = m_Pitch;
			m_StartingYaw = m_Yaw;
			m_StartingPos = m_Position;

			Transform* terminalTransform = terminal->GetTransform();
			m_TargetPos = terminalTransform->GetWorldPosition() +
				terminalTransform->GetUp() * 1.0f +
				terminalTransform->GetForward() * 4.0f;
			glm::vec3 dPos = m_TargetPos - terminalTransform->GetWorldPosition();
			m_TargetPitch = 0.0f;
			m_TargetYaw = -atan2(dPos.z, dPos.x);
			m_bTransitioningIn = true;
		}
	}

	void TerminalCamera::TransitionOut()
	{
		m_TargetPitch = m_StartingPitch;
		m_TargetYaw = m_StartingYaw;
		m_TargetPos = m_StartingPos;
		m_bTransitioningOut = true;
	}

} // namespace flex
