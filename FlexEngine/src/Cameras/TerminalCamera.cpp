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
		BaseCamera("terminal", CameraType::TERMINAL, true, FOV),
		m_StartingPos(VEC3_ZERO)
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
			if (m_Terminal == nullptr)
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

							m_StartingPitch = pitch;
							m_StartingYaw = yaw;
							m_StartingPos = glm::vec3(targetXZPos.x, position.y, targetXZPos.z);

							m_TargetPlayerPos = m_StartingPos + terminalForward * 3.0f;
							m_TargetPlayerRot = glm::quat(glm::vec3(0, atan2(forward.x, forward.z), 0));

							// Will call SetTerminal on us
							player->SetInteractingWithTerminal(closestTerminal);
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
			yaw = MoveTowards(yaw, m_TargetYaw, g_DeltaTime * m_LerpSpeed);
			pitch = MoveTowards(pitch, m_TargetPitch, g_DeltaTime * m_LerpSpeed);
			position = MoveTowards(position, m_TargetPos, g_DeltaTime * m_LerpSpeed);

			if (m_bTransitioningIn)
			{
				Player* p0 = g_SceneManager->CurrentScene()->GetPlayer(0);
				Transform* playerTransform = p0->GetTransform();
				playerTransform->SetWorldPosition(MoveTowards(playerTransform->GetWorldPosition(), m_TargetPlayerPos, g_DeltaTime * m_LerpSpeed));
				playerTransform->SetWorldRotation(MoveTowards(playerTransform->GetWorldRotation(), m_TargetPlayerRot, g_DeltaTime * m_LerpSpeed));
			}

			if (NearlyEquals(yaw, m_TargetYaw, 0.01f) &&
				NearlyEquals(pitch, m_TargetPitch, 0.01f) &&
				NearlyEquals(position, m_TargetPos, 0.01f))
			{
				if (m_bTransitioningIn)
				{
					m_bTransitioningIn = false;
				}
				if (m_bTransitioningOut)
				{
					m_bTransitioningOut = false;
					g_SceneManager->CurrentScene()->GetPlayer(0)->SetInteractingWithTerminal(nullptr);
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

			m_StartingPitch = pitch;
			m_StartingYaw = yaw;
			m_StartingPos = position;

			Transform* terminalTransform = terminal->GetTransform();
			m_TargetPos = terminalTransform->GetWorldPosition() +
				terminalTransform->GetUp() * 1.0f +
				terminalTransform->GetForward() * 4.0f;
			glm::vec3 targetForward = glm::normalize(terminalTransform->GetWorldPosition() - m_TargetPos);
			m_TargetPitch = 0.0f;
			m_TargetYaw = atan2(targetForward.z, targetForward.x);
			WrapTargetYaw();
			m_bTransitioningIn = true;
		}
	}

	void TerminalCamera::TransitionOut()
	{
		m_TargetPitch = m_StartingPitch;
		m_TargetYaw = m_StartingYaw;
		WrapTargetYaw();
		m_TargetPos = m_StartingPos;

		m_bTransitioningOut = true;
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
