#include "stdafx.hpp"

#include "Cameras/OverheadCamera.hpp"

#pragma warning(push, 0)
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp> // for rotateY
#include <glm/vec2.hpp>
#pragma warning(pop)

#include "Helpers.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	OverheadCamera::OverheadCamera(real FOV, real zNear, real zFar) :
		BaseCamera("overhead",FOV, zNear, zFar)
	{
		ResetOrientation();
		RecalculateViewProjection();
	}

	OverheadCamera::~OverheadCamera()
	{
	}

	void OverheadCamera::Initialize()
	{
		BaseCamera::Initialize();

		FindPlayer();
		Update();

		m_PlayerPosRollingAvg = RollingAverage<glm::vec3>(12);
		m_PlayerForwardRollingAvg = RollingAverage<glm::vec3>(12);

		ResetValues();
	}

	void OverheadCamera::OnSceneChanged()
	{
		BaseCamera::OnSceneChanged();

		FindPlayer();
		Update();

		ResetValues();
	}

	void OverheadCamera::Update()
	{
		if (!m_Player0)
		{
			return;
		}

		m_PlayerForwardRollingAvg.AddValue(m_Player0->GetTransform()->GetForward());

		m_PlayerPosRollingAvg.AddValue(m_Player0->GetTransform()->GetWorldPosition());
		m_TargetLookAtPos = m_PlayerPosRollingAvg.currentAverage;

		SetLookAt();

		glm::vec3 desiredPos = GetOffsetPosition(m_TargetLookAtPos);
		glm::vec3 dPos = desiredPos - m_Position;
		real dPosMag = glm::length(dPos);
		if (dPosMag > glm::epsilon<real>())
		{
			//real maxDist = glm::clamp(dPosMag * 1000.0f * g_DeltaTime, 0.0f, 10.0f);
			m_Vel += dPos / g_DeltaTime * 0.1f;
		}

		m_Vel *= 0.95f;
		m_Position += m_Vel * g_DeltaTime;
		if (m_Vel.x > 0.0f && m_Position.x > desiredPos.x ||
			m_Vel.x < 0.0f && m_Position.x < desiredPos.x)
		{
			m_Position.x = desiredPos.x;
			m_Vel.x = 0.0f;
		}
		if (m_Vel.y > 0.0f && m_Position.y > desiredPos.y ||
			m_Vel.y < 0.0f && m_Position.y < desiredPos.y)
		{
			m_Position.y = desiredPos.y;
			m_Vel.y = 0.0f;
		}
		if (m_Vel.z > 0.0f && m_Position.z > desiredPos.z ||
			m_Vel.z < 0.0f && m_Position.z < desiredPos.z)
		{
			m_Position.z = desiredPos.z;
			m_Vel.z = 0.0f;
		}

		CalculateYawAndPitchFromForward();
		RecalculateViewProjection();
	}

	void OverheadCamera::DrawImGuiObjects()
	{
		if (ImGui::TreeNode("Overhead camera"))
		{
			ImGui::Text("Avg player forward: %s",Vec3ToString(m_PlayerForwardRollingAvg.currentAverage, 2));
			ImGui::Text("For: %s", Vec3ToString(m_Forward, 2).c_str());

			ImGui::TreePop();
		}
	}

	glm::vec3 OverheadCamera::GetOffsetPosition(const glm::vec3& pos) const
	{
		glm::vec3 playerForward = m_PlayerForwardRollingAvg.currentAverage;
		glm::vec3 offsetVec(VEC3_UP * m_OffsetY + playerForward * m_OffsetZ);
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
		m_Position = desiredPos;

		SetLookAt();
	}

	void OverheadCamera::SetLookAt()
	{
		m_Forward = glm::normalize(m_TargetLookAtPos - m_Position);
		m_Right = normalize(glm::cross(VEC3_UP, m_Forward));
		m_Up = cross(m_Forward, m_Right);
	}

	void OverheadCamera::FindPlayer()
	{
		m_Player0 = g_SceneManager->CurrentScene()->FirstObjectWithTag("Player0");
	}

	void OverheadCamera::ResetValues()
	{
		m_Vel = VEC3_ZERO;
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
		m_OffsetY = 14.0f;
		m_OffsetZ = -16.0f;
	}
} // namespace flex
