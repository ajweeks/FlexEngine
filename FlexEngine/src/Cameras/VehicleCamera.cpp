#include "stdafx.hpp"

#include "Cameras/VehicleCamera.hpp"

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
	VehicleCamera::VehicleCamera(real FOV) :
		BaseCamera("vehicle", CameraType::VEHICLE, true, FOV)
	{
		bPossessPlayer = true;
		ResetValues();
	}

	VehicleCamera::~VehicleCamera()
	{
	}

	void VehicleCamera::Initialize()
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

	void VehicleCamera::OnSceneChanged()
	{
		BaseCamera::OnSceneChanged();

		m_Player0 = nullptr;
		FindPlayer();

		ResetValues();
	}

	void VehicleCamera::Update()
	{
		BaseCamera::Update();

		if (m_Player0 == nullptr)
		{
			return;
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

	void VehicleCamera::DrawImGuiObjects()
	{
		if (m_Player0 != nullptr)
		{
			if (ImGui::TreeNode("Vehicle camera"))
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

	glm::vec3 VehicleCamera::GetOffsetPosition(const glm::vec3& pos)
	{
		glm::vec3 backward = -m_PlayerForwardRollingAvg.currentAverage;
		glm::vec3 offsetVec = glm::vec3(VEC3_UP * 2.0f + backward * 2.0f) * 6.0f;
		//glm::vec3 offsetVec = glm::rotate(backward, pitch, m_Player0->GetTransform()->GetRight()) * m_ZoomLevel;
		return pos + offsetVec;
	}

	void VehicleCamera::SetPosAndLookAt()
	{
		if (m_Player0 == nullptr)
		{
			return;
		}

		m_TargetLookAtPos = m_Player0->GetTransform()->GetWorldPosition();

		glm::vec3 desiredPos = GetOffsetPosition(m_TargetLookAtPos);
		position = desiredPos;

		SetLookAt();
	}

	void VehicleCamera::SetLookAt()
	{
		forward = glm::normalize(m_TargetLookAtPos - position);
		right = normalize(glm::cross(VEC3_UP, forward));
		up = cross(forward, right);
	}

	void VehicleCamera::FindPlayer()
	{
		m_Player0 = g_SceneManager->CurrentScene()->FirstObjectWithTag("Player0");
	}

	void VehicleCamera::ResetValues()
	{
		ResetOrientation();

		m_Vel = VEC3_ZERO;
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

		RecalculateViewProjection();
	}

} // namespace flex
