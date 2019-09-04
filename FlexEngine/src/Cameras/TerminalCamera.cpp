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
	TerminalCamera::TerminalCamera(real FOV) :
		BaseCamera("terminal", true, FOV)
	{
		bDEBUGCyclable = false;
	}

	void TerminalCamera::Initialize()
	{
		if (!m_bInitialized)
		{
			if (m_Terminal ==  nullptr)
			{
				Player* p0 = g_SceneManager->CurrentScene()->GetPlayer(0);
				if (p0 != nullptr)
				{
					// TODO: Also take into account directionality, like done in PlayerController::Update
					std::vector<Terminal*> terminals = g_SceneManager->CurrentScene()->GetObjectsOfType<Terminal>();
					if (!terminals.empty())
					{
						glm::vec3 playerPos = p0->GetTransform()->GetWorldPosition();
						real shortestDist = FLT_MAX;
						Terminal* closestTerminal = nullptr;
						for (Terminal* t : terminals)
						{
							real distToTerm = glm::distance(playerPos, t->GetTransform()->GetWorldPosition());
							if (distToTerm < shortestDist)
							{
								shortestDist = distToTerm;
								closestTerminal = t;
							}
						}

						if (closestTerminal != nullptr)
						{
							// Will call SetTerminal on us
							p0->SetInteractingWith(closestTerminal);
							closestTerminal->SetInteractingWith(p0);
						}
					}
				}
			}

			BaseCamera::Initialize();
		}
	}

	void TerminalCamera::Update()
	{
		BaseCamera::Update();

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
			glm::vec3 targetForward = glm::normalize(terminalTransform->GetWorldPosition() - m_TargetPos);
			m_TargetPitch = 0.0f;
			m_TargetYaw = atan2(targetForward.z, targetForward.x);
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
