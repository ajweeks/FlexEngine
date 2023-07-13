#include "stdafx.hpp"

#include "Cameras/TerminalCamera.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include "Cameras/CameraManager.hpp"
#include "Helpers.hpp" // For MoveTowards
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	TerminalCamera::TerminalCamera(real FOV) :
		BaseCamera("terminal", CameraType::TERMINAL, true, FOV)
	{
		bDEBUGCyclable = false;
		bIsFirstPerson = true;
	}

	TerminalCamera::~TerminalCamera()
	{
	}

	void TerminalCamera::Initialize()
	{
		if (!m_bInitialized)
		{
			// Handle game startup in terminal cam
			if (!m_TerminalID.IsValid())
			{
				Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
				if (player != nullptr)
				{
					std::vector<Terminal*> terminals = g_SceneManager->CurrentScene()->GetObjectsOfType<Terminal>(TerminalSID);
					if (!terminals.empty())
					{
						Transform* playerTransform = player->GetTransform();
						glm::vec3 playerPos = playerTransform->GetWorldPosition();
						real shortestSqDist = FLT_MAX;
						Terminal* closestTerminal = nullptr;
						for (Terminal* t : terminals)
						{
							real sqDist = glm::distance2(playerPos, t->GetTransform()->GetWorldPosition());
							if (sqDist < shortestSqDist)
							{
								shortestSqDist = sqDist;
								closestTerminal = t;
							}
						}

						if (closestTerminal != nullptr)
						{
							Transform* terminalTransform = closestTerminal->GetTransform();
							glm::vec3 terminalForward = terminalTransform->GetForward();
							forward = -terminalForward;
							CalculateYawAndPitchFromForward();

							glm::vec3 targetXZPos = terminalTransform->GetWorldPosition() + terminalForward * 2.5f;

							m_StartingPos = glm::vec3(targetXZPos.x, position.y, targetXZPos.z);
							m_StartingPitch = pitch;
							m_StartingYaw = yaw;

							m_PlayerStartingPos = playerTransform->GetWorldPosition();
							m_PlayerStartingRot = playerTransform->GetWorldRotation();

							// Will call SetTerminal on us
							player->SetInteractingWithTerminal(closestTerminal);

							m_TerminalID = closestTerminal->ID;
						}
					}
				}
			}

			BaseCamera::Initialize();
		}
	}

	void TerminalCamera::OnLostTerminal()
	{
		PrintError("TerminalCamera lost closest terminal game object, was it destroyed?\n");

		g_SceneManager->CurrentScene()->GetPlayer(0)->SetInteractingWithTerminal(nullptr);
		g_CameraManager->PopCamera();
		m_TerminalID = InvalidGameObjectID;
		m_TransitionRemaining = 0.0f;
	}

	void TerminalCamera::Update()
	{
		BaseCamera::Update();

		if (m_TransitionRemaining != 0.0f)
		{
			const bool bTransitioningIn = m_TransitionRemaining > 0.0f;
			const bool bTransitioningOut = m_TransitionRemaining < 0.0f;
			bool bTransitionComplete = false;

			if (bTransitioningIn)
			{
				m_TransitionRemaining -= g_DeltaTime;
				if (m_TransitionRemaining <= 0.0f)
				{
					m_TransitionRemaining = 0.0f;
					bTransitionComplete = true;
				}
			}
			else
			{
				m_TransitionRemaining += g_DeltaTime;
				if (m_TransitionRemaining >= 0.0f)
				{
					m_TransitionRemaining = 0.0f;
					bTransitionComplete = true;
				}
			}

			Player* p0 = g_SceneManager->CurrentScene()->GetPlayer(0);
			Transform* playerTransform = p0->GetTransform();
			Terminal* terminal = (Terminal*)m_TerminalID.Get();
			if (terminal == nullptr)
			{
				OnLostTerminal();
				return;
			}
			Transform* terminalTransform = terminal->GetTransform();

			glm::vec3 terminalForward = terminalTransform->GetForward();
			glm::vec3 targetPlayerPos = m_StartingPos + terminalForward * m_TargetPlayerOffset;
			glm::quat targetPlayerRot = glm::quat(glm::vec3(0, atan2(forward.x, forward.z), 0));

			if (bTransitionComplete)
			{
				yaw = m_TargetYaw;
				pitch = m_TargetPitch;
				position = m_TargetPos;

				if (bTransitioningIn)
				{
					playerTransform->SetWorldPosition(targetPlayerPos);
					playerTransform->SetWorldRotation(targetPlayerRot);
				}
				else
				{
					playerTransform->SetWorldPosition(m_PlayerStartingPos);
					playerTransform->SetWorldRotation(m_PlayerStartingRot);

					g_SceneManager->CurrentScene()->GetPlayer(0)->SetInteractingWithTerminal(nullptr);
					g_CameraManager->PopCamera(true);
					m_TerminalID = InvalidGameObjectID;
				}
			}
			else
			{
				real percent = 1.0f - Saturate(abs(m_TransitionRemaining) / m_CurrentTransitionDuration);
				real lerpAmount = glm::pow(percent, 0.75f);
				yaw = Lerp(m_TransitionStartingYaw, m_TargetYaw, lerpAmount);
				pitch = Lerp(m_TransitionStartingPitch, m_TargetPitch, lerpAmount);
				position = Lerp(m_TransitionStartingPos, m_TargetPos, lerpAmount);

				playerTransform->SetWorldPosition(Lerp(m_PlayerStartingPos, targetPlayerPos, bTransitioningIn ? lerpAmount : 1.0f - lerpAmount));
				playerTransform->SetWorldRotation(Slerp(m_PlayerStartingRot, targetPlayerRot, bTransitioningIn ? lerpAmount : 1.0f - lerpAmount));
			}
		}

		CalculateAxisVectorsFromPitchAndYaw();
		RecalculateViewProjection();
	}

	void TerminalCamera::SetTerminal(Terminal* terminal)
	{
		if (terminal == nullptr)
		{
			Terminal* prevTerminal = (Terminal*)m_TerminalID.Get();
			if (prevTerminal == nullptr)
			{
				OnLostTerminal();
				return;
			}
			prevTerminal->SetCamera(nullptr);
			prevTerminal = nullptr;
		}
		else
		{
			m_TerminalID = terminal->ID;
			terminal->SetCamera(this);

			Transform* terminalTransform = terminal->GetTransform();
			glm::vec3 terminalUp = terminalTransform->GetUp();
			glm::vec3 terminalForward = terminalTransform->GetForward();

			m_StartingPos = position;
			m_StartingPitch = pitch;
			m_StartingYaw = yaw;

			m_TransitionStartingPos = position;
			m_TransitionStartingPitch = pitch;
			m_TransitionStartingYaw = yaw;

			m_TargetPos = terminalTransform->GetWorldPosition() +
				terminalUp * 1.0f +
				terminalForward * 4.0f;
			glm::vec3 targetForward = glm::normalize(terminalTransform->GetWorldPosition() - m_TargetPos);
			m_TargetPitch = 0.0f;
			m_TargetYaw = atan2(targetForward.z, targetForward.x);
			WrapTargetYaw();

			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			Transform* playerTransform = player->GetTransform();
			m_PlayerStartingPos = playerTransform->GetWorldPosition();
			m_PlayerStartingRot = playerTransform->GetWorldRotation();

			if (m_TransitionRemaining != 0.0f)
			{
				m_TransitionRemaining = -(m_CurrentTransitionDuration - abs(m_TransitionRemaining));
				m_CurrentTransitionDuration = abs(m_TransitionRemaining);
			}
			else
			{
				glm::vec3 targetPlayerPos = m_StartingPos + terminalForward * m_TargetPlayerOffset;

				real dist = glm::distance(m_PlayerStartingPos, targetPlayerPos);
				m_TransitionRemaining = glm::clamp(m_TransitionTimePerM * dist, -m_MaxTransitionDuration, m_MaxTransitionDuration);
				m_CurrentTransitionDuration = abs(m_TransitionRemaining);
			}
		}
	}

	void TerminalCamera::TransitionOut()
	{
		m_TransitionStartingPos = position;
		m_TransitionStartingPitch = pitch;
		m_TransitionStartingYaw = yaw;

		m_TargetPos = m_StartingPos;
		m_TargetPitch = m_StartingPitch;
		m_TargetYaw = m_StartingYaw;
		WrapTargetYaw();

		if (m_TransitionRemaining != 0.0f)
		{
			m_TransitionRemaining = -(m_CurrentTransitionDuration - abs(m_TransitionRemaining));
			m_CurrentTransitionDuration = abs(m_TransitionRemaining);
		}
		else
		{
			Terminal* terminal = (Terminal*)m_TerminalID.Get();
			if (terminal == nullptr)
			{
				OnLostTerminal();
				return;
			}
			Transform* terminalTransform = terminal->GetTransform();

			glm::vec3 terminalForward = terminalTransform->GetForward();
			glm::vec3 targetPlayerPos = m_StartingPos + terminalForward * m_TargetPlayerOffset;

			real dist = glm::distance(m_PlayerStartingPos, targetPlayerPos);
			m_TransitionRemaining = glm::clamp(-m_TransitionTimePerM * dist, -m_MaxTransitionDuration, m_MaxTransitionDuration);
			m_CurrentTransitionDuration = abs(m_TransitionRemaining);
		}
	}

	void TerminalCamera::WrapTargetYaw()
	{
		if (m_TargetYaw - yaw > PI)
		{
			m_TargetYaw -= TWO_PI;
		}
		if (m_TargetYaw - yaw < -PI)
		{
			m_TargetYaw += TWO_PI;
		}
	}

} // namespace flex
