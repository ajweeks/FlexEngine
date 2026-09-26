#include "stdafx.hpp"

#include "Cameras/OverheadCamera.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp> // for rotateY
#include <glm/vec2.hpp>
IGNORE_WARNINGS_POP

#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Player.hpp"
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
			FindPlayer();

			m_PlayerPosRollingAvg = RollingAverage<glm::vec3>(15, SamplingType::LINEAR);
			m_PlayerForwardRollingAvg = RollingAverage<glm::vec3>(30, SamplingType::LINEAR);

			ResetValues();

			BaseCamera::Initialize();
		}
	}

	void OverheadCamera::OnPostSceneChange()
	{
		BaseCamera::OnPostSceneChange();

		m_Player0 = nullptr;
		FindPlayer();

		ResetValues();
	}

	void OverheadCamera::FixedUpdate()
	{
		if (m_Player0 == nullptr)
		{
			return;
		}

		// Sample on the fixed step so smoothing is independent of frame rate
		m_PlayerForwardRollingAvg.AddValue(m_Player0->GetTransform()->GetForward());
		m_PlayerPosRollingAvg.AddValue(m_Player0->GetTransform()->GetWorldPosition());

		m_PrevPlayerPosAvg = m_CurrPlayerPosAvg;
		m_CurrPlayerPosAvg = m_PlayerPosRollingAvg.currentAverage;
		m_PrevPlayerForwardAvg = m_CurrPlayerForwardAvg;
		m_CurrPlayerForwardAvg = m_PlayerForwardRollingAvg.currentAverage;
	}

	void OverheadCamera::LateUpdate()
	{
		TrackPlayer();

		BaseCamera::LateUpdate();
	}

	void OverheadCamera::TrackPlayer()
	{
		if (m_Player0 == nullptr)
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

		PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();
		real alpha = (physicsWorld != nullptr) ? physicsWorld->GetInterpolationAlpha() : 1.0f;
		m_TargetLookAtPos = glm::mix(m_PrevPlayerPosAvg, m_CurrPlayerPosAvg, alpha);
		glm::vec3 playerForward = glm::mix(m_PrevPlayerForwardAvg, m_CurrPlayerForwardAvg, alpha);

#if THOROUGH_CHECKS
		ENSURE(!IsNanOrInf(m_TargetLookAtPos));
#endif

		position = GetOffsetPosition(m_TargetLookAtPos, playerForward);
		SetLookAt();

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
				g_Renderer->GetDebugRenderer()->drawLine(ToBtVec3(start), ToBtVec3(end), btVector3(1.0f, 1.0f, 1.0f));

				ImGui::Text("Avg player forward: %s", VecToString(m_PlayerForwardRollingAvg.currentAverage, 2).c_str());
				ImGui::Text("For: %s", VecToString(forward, 2).c_str());
				ImGui::TreePop();
			}
		}
	}

	glm::vec3 OverheadCamera::GetOffsetPosition(const glm::vec3& pos, const glm::vec3& playerForward)
	{
		glm::vec3 backward = -playerForward;
		glm::vec3 offsetVec = glm::vec3(VEC3_UP * 2.0f + backward * 2.0f) * m_ZoomLevel;
		//glm::vec3 offsetVec = glm::rotate(backward, pitch, m_Player0->GetTransform()->GetRight()) * m_ZoomLevel;
		return pos + offsetVec;
	}

	void OverheadCamera::SetPosAndLookAt()
	{
		if (m_Player0 == nullptr)
		{
			return;
		}

		m_TargetLookAtPos = m_Player0->GetTransform()->GetWorldPosition();

		glm::vec3 desiredPos = GetOffsetPosition(m_TargetLookAtPos, m_Player0->GetTransform()->GetForward());
		position = desiredPos;

		SetLookAt();
	}

	void OverheadCamera::SetLookAt()
	{
		forward = glm::normalize(m_TargetLookAtPos - position);
		right = normalize(glm::cross(VEC3_UP, forward));
		up = cross(forward, right);
	}

	void OverheadCamera::ResetSmoothedSamples()
	{
		m_CurrPlayerPosAvg = m_PrevPlayerPosAvg = m_PlayerPosRollingAvg.currentAverage;
		m_CurrPlayerForwardAvg = m_PrevPlayerForwardAvg = m_PlayerForwardRollingAvg.currentAverage;
	}

	void OverheadCamera::FindPlayer()
	{
		m_Player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
	}

	void OverheadCamera::ResetValues()
	{
		ResetOrientation();

		m_Vel = VEC3_ZERO;
		m_TargetZoomLevel = (real)(m_ZoomLevels / 2) * ((m_MaxZoomLevel - m_MinZoomLevel) / (real)(m_ZoomLevels - 1)) + m_MinZoomLevel;
		m_ZoomLevel = m_TargetZoomLevel;
		pitch = -PI_DIV_FOUR;
		SetPosAndLookAt();

		if (m_Player0 != nullptr)
		{
			m_PlayerPosRollingAvg.Reset(m_Player0->GetTransform()->GetWorldPosition());
			m_PlayerForwardRollingAvg.Reset(m_Player0->GetTransform()->GetForward());
		}
		else
		{
			m_PlayerPosRollingAvg.Reset();
			m_PlayerForwardRollingAvg.Reset();
		}

		ResetSmoothedSamples();

		RecalculateViewProjection();
	}

} // namespace flex
