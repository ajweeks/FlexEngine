#include "stdafx.hpp"

#include "Cameras/OverheadCamera.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp> // for rotateY
#include <glm/vec2.hpp>

#include <LinearMath/btIDebugDraw.h>
IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	OverheadCamera::OverheadCamera(real FOV) :
		BaseCamera("overhead", CameraType::OVERHEAD, true, FOV)
	{
		bPossessPlayer = true;
		ResetValues();
	}

	OverheadCamera::~OverheadCamera()
	{
	}

	void OverheadCamera::Initialize()
	{
		if (!m_bInitialized)
		{
			if (m_Player0 == nullptr)
			{
				FindPlayer();
			}

			m_PlayerPosRollingAvg = RollingAverage<glm::vec3>(15, SamplingType::LINEAR);
			m_PlayerForwardRollingAvg = RollingAverage<glm::vec3>(30, SamplingType::LINEAR);

			ResetValues();

			BaseCamera::Initialize();
		}
	}

	void OverheadCamera::OnSceneChanged()
	{
		BaseCamera::OnSceneChanged();

		m_Player0 = nullptr;
		FindPlayer();

		ResetValues();
	}

	void OverheadCamera::Update()
	{
		BaseCamera::Update();

		if (!m_Player0)
		{
			return;
		}

		if (g_InputManager->GetActionPressed(Action::ZOOM_OUT))
		{
			m_TargetZoomLevel += (m_MaxZoomLevel - m_MinZoomLevel) / (real)(m_ZoomLevels - 1);
			m_TargetZoomLevel = glm::clamp(m_TargetZoomLevel, m_MinZoomLevel, m_MaxZoomLevel);
		}

		if (g_InputManager->GetActionPressed(Action::ZOOM_IN))
		{
			m_TargetZoomLevel -= (m_MaxZoomLevel - m_MinZoomLevel) / (real)(m_ZoomLevels - 1);
			m_TargetZoomLevel = glm::clamp(m_TargetZoomLevel, m_MinZoomLevel, m_MaxZoomLevel);
		}

		if (!NearlyEquals(m_ZoomLevel, m_TargetZoomLevel, 0.01f))
		{
			m_ZoomLevel = MoveTowards(m_ZoomLevel, m_TargetZoomLevel, g_DeltaTime * 15.0f);
		}

		m_PlayerForwardRollingAvg.AddValue(m_Player0->GetTransform()->GetForward());

		m_PlayerPosRollingAvg.AddValue(m_Player0->GetTransform()->GetWorldPosition());
		m_TargetLookAtPos = m_PlayerPosRollingAvg.currentAverage;

#if THOROUGH_CHECKS
		ENSURE(!IsNanOrInf(m_TargetLookAtPos));
#endif

		SetLookAt();

		glm::vec3 desiredPos = GetOffsetPosition(m_TargetLookAtPos);
		position = desiredPos;

		CalculateYawAndPitchFromForward();
		RecalculateViewProjection();
	}

	void OverheadCamera::DrawImGuiObjects()
	{
		if (m_Player0 != nullptr)
		{
			if (ImGui::TreeNode("Overhead camera"))
			{
				glm::vec3 start = m_Player0->GetTransform()->GetWorldPosition();
				glm::vec3 end = start + m_PlayerForwardRollingAvg.currentAverage * 10.0f;
				g_Renderer->GetDebugDrawer()->drawLine(ToBtVec3(start), ToBtVec3(end), btVector3(1.0f, 1.0f, 1.0f));

				ImGui::Text("Avg player forward: %s", VecToString(m_PlayerForwardRollingAvg.currentAverage, 2).c_str());
				ImGui::Text("For: %s", VecToString(forward, 2).c_str());
				ImGui::TreePop();
			}
		}
	}

	glm::vec3 OverheadCamera::GetOffsetPosition(const glm::vec3& pos)
	{
		glm::vec3 backward = -m_PlayerForwardRollingAvg.currentAverage;
		glm::vec3 offsetVec = glm::vec3(VEC3_UP * 2.0f + backward * 2.0f) * m_ZoomLevel;
		//glm::vec3 offsetVec = glm::rotate(backward, pitch, m_Player0->GetTransform()->GetRight()) * m_ZoomLevel;
		return pos + offsetVec;
	}

	void OverheadCamera::SetPosAndLookAt()
	{
		if (!m_Player0)
		{
			return;
		}

		m_TargetLookAtPos = m_Player0->GetTransform()->GetWorldPosition();

		glm::vec3 desiredPos = GetOffsetPosition(m_TargetLookAtPos);
		position = desiredPos;

		SetLookAt();
	}

	void OverheadCamera::SetLookAt()
	{
		forward = glm::normalize(m_TargetLookAtPos - position);
		right = normalize(glm::cross(VEC3_UP, forward));
		up = cross(forward, right);
	}

	void OverheadCamera::FindPlayer()
	{
		m_Player0 = g_SceneManager->CurrentScene()->FirstObjectWithTag("Player0");
	}

	void OverheadCamera::ResetValues()
	{
		ResetOrientation();

		m_Vel = VEC3_ZERO;
		m_TargetZoomLevel = (real)(m_ZoomLevels / 2) * ((m_MaxZoomLevel - m_MinZoomLevel) / (real)(m_ZoomLevels - 1)) + m_MinZoomLevel;
		m_ZoomLevel = m_TargetZoomLevel;
		pitch = -PI_DIV_FOUR;
		SetPosAndLookAt();

		if (m_Player0)
		{
			m_PlayerPosRollingAvg.Reset(m_Player0->GetTransform()->GetWorldPosition());
			m_PlayerForwardRollingAvg.Reset(m_Player0->GetTransform()->GetForward());
		}
		else
		{
			m_PlayerPosRollingAvg.Reset();
			m_PlayerForwardRollingAvg.Reset();
		}

		RecalculateViewProjection();
	}

} // namespace flex
