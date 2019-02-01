#include "stdafx.hpp"

#include "Cameras/TerminalCamera.hpp"

#include "Scene/GameObject.hpp"
#include "Helpers.hpp" // For MoveTowards
#include "Graphics/Renderer.hpp"

namespace flex
{
	TerminalCamera::TerminalCamera(real FOV, real zNear, real zFar) :
		BaseCamera("terminal", true, FOV, zNear, zFar)
	{
	}

	TerminalCamera::~TerminalCamera()
	{
	}

	void TerminalCamera::Update()
	{
		if (m_bMovingToTarget)
		{
			m_Yaw = MoveTowards(m_Yaw, m_TargetYaw, g_DeltaTime * m_LerpSpeed);
			m_Pitch = MoveTowards(m_Pitch, m_TargetPitch, g_DeltaTime * m_LerpSpeed);
			m_Position = MoveTowards(m_Position, m_TargetPos, g_DeltaTime * m_LerpSpeed);

			if (NearlyEquals(m_Yaw, m_TargetYaw, 0.001f) &&
				NearlyEquals(m_Pitch, m_TargetPitch, 0.001f) &&
				NearlyEquals(m_Position, m_TargetPos, 0.001f))
			{
				m_bMovingToTarget = false;
			}
		}

		CalculateAxisVectorsFromPitchAndYaw();
		RecalculateViewProjection();
	}

	void TerminalCamera::SetTerminal(Terminal* terminal)
	{
		m_Terminal = terminal;
		Transform* terminalTransform = terminal->GetTransform();
		m_TargetPos = terminalTransform->GetWorldPosition() +
			terminalTransform->GetUp() * 1.0f +
			terminalTransform->GetForward() * 4.0f;
		glm::vec3 dPos = m_TargetPos - terminalTransform->GetWorldPosition();
		m_TargetPitch = 0.0f;
		m_TargetYaw = -atan2(dPos.z, dPos.x);
		m_bMovingToTarget = true;
	}

} // namespace flex
