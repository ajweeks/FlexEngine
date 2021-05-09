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
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	VehicleCamera::VehicleCamera(real FOV) :
		BaseCamera("vehicle", CameraType::VEHICLE, true, FOV),
		m_SpeedFactors(256),
		m_TargetFollowDist(256),
		m_LastLookOffset(VEC2_ZERO)
	{
		bPossessPlayer = true;
		m_SpeedFactors.overrideMin = 0.0f;
		m_SpeedFactors.overrideMax = 1.0f;
		ResetValues();
	}

	VehicleCamera::~VehicleCamera()
	{
	}

	void VehicleCamera::Initialize()
	{
		if (!m_bInitialized)
		{
			if (m_TrackedVehicle == nullptr)
			{
				FindActiveVehicle();
			}

			m_TargetPosRollingAvg = RollingAverage<glm::vec3>(15, SamplingType::LINEAR);
			m_TargetForwardRollingAvg = RollingAverage<glm::vec3>(30, SamplingType::LINEAR);
			m_TargetVelMagnitudeRollingAvg = RollingAverage<real>(30, SamplingType::LINEAR);
			m_LookOffsetRollingAvg = RollingAverage<glm::vec2>(15, SamplingType::CUBIC);

			ResetValues();

			BaseCamera::Initialize();
		}
	}

	void VehicleCamera::OnSceneChanged()
	{
		BaseCamera::OnSceneChanged();

		m_TrackedVehicle = nullptr;
		FindActiveVehicle();

		ResetValues();
	}

	void VehicleCamera::Update()
	{
		BaseCamera::Update();

		if (m_TrackedVehicle == nullptr)
		{
			return;
		}

		Transform* targetTransform = m_TrackedVehicle->GetTransform();
		btRigidBody* trackedRB = m_TrackedVehicle->GetRigidBody()->GetRigidBodyInternal();

		m_TargetForwardRollingAvg.AddValue(targetTransform->GetForward());
		m_TargetVelMagnitudeRollingAvg.AddValue(trackedRB->getLinearVelocity().length());

		m_TargetPosRollingAvg.AddValue(targetTransform->GetWorldPosition());
		m_TargetLookAtPos = m_TargetPosRollingAvg.currentAverage;

		real newLookOffsetH = -g_InputManager->GetActionAxisValue(Action::VEHICLE_LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::VEHICLE_LOOK_RIGHT);
		real newLookOffsetV = -g_InputManager->GetActionAxisValue(Action::VEHICLE_LOOK_UP) + g_InputManager->GetActionAxisValue(Action::VEHICLE_LOOK_DOWN);
		m_LookOffsetRollingAvg.AddValue(glm::vec2(newLookOffsetH, newLookOffsetV) * m_LookOffsetMagnitude);

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
		if (m_TrackedVehicle != nullptr)
		{
			if (ImGui::TreeNode("Vehicle camera"))
			{
				Transform* targetTransform = m_TrackedVehicle->GetTransform();
				glm::vec3 start = targetTransform->GetWorldPosition();
				glm::vec3 end = start + m_TargetForwardRollingAvg.currentAverage * 10.0f;
				g_Renderer->GetDebugDrawer()->drawLine(ToBtVec3(start), ToBtVec3(end), btVector3(1.0f, 1.0f, 1.0f));

				ImGui::Text("Avg target forward: %s", VecToString(m_TargetForwardRollingAvg.currentAverage, 2).c_str());
				ImGui::Text("For: %s", VecToString(forward, 2).c_str());
				ImGui::TreePop();

				ImGui::SliderFloat("Rotation update speed", &m_RotationUpdateSpeed, 0.001f, 50.0f);
				ImGui::SliderFloat("Dist update speed", &m_DistanceUpdateSpeed, 0.001f, 10.0f);

				ImGui::SliderFloat("Min target speed zoom threshold", &m_MinSpeed, 1.0f, 50.0f);
				ImGui::SliderFloat("Max target speed zoom threshold", &m_MaxSpeed, 1.0f, 50.0f);

				ImGui::SliderFloat("Closest dist", &m_ClosestDist, 1.0f, 60.0f);
				ImGui::SliderFloat("Furthest dist", &m_FurthestDist, 1.0f, 90.0f);

				ImGui::SliderFloat("Min downward angle", &m_MinDownwardAngle, 0.0f, 3.0f);
				ImGui::SliderFloat("Max downward angle", &m_MaxDownwardAngle, 0.0f, 3.0f);

				ImGui::SliderFloat("Look dist lerp speed", &m_LookDistLerpSpeed, 0.0f, 50.0f);
				ImGui::SliderFloat("Look offset magnitude", &m_LookOffsetMagnitude, 0.0f, 10.0f);

				m_SpeedFactors.DrawImGui();
				m_TargetFollowDist.DrawImGui();
			}
		}
	}

	glm::vec3 VehicleCamera::GetOffsetPosition(const glm::vec3& lookAtPos)
	{
		// TODO: Handle camera cut to stationary vehicle? (use vehicle forward rather than vel)
		glm::vec3 backward = -m_TargetForwardRollingAvg.currentAverage;

		real minOffsetY = 0.0f;
		if (backward.y < minOffsetY)
		{
			backward.y = minOffsetY;
			backward = glm::normalize(backward);
		}

		// TODO: Check for collisions

		real speedFactor = glm::clamp((m_TargetVelMagnitudeRollingAvg.currentAverage - m_MinSpeed) / (m_MaxSpeed - m_MinSpeed), 0.0f, 1.0f);
		glm::vec3 upward = VEC3_UP * Lerp(m_MaxDownwardAngle, m_MinDownwardAngle, speedFactor);
		real targetFollowDistance = Lerp(m_ClosestDist, m_FurthestDist, speedFactor);
		real previousFollowDistance = glm::distance(position, lookAtPos);
		real distUpdateDelta = glm::clamp(1.0f - g_DeltaTime * m_DistanceUpdateSpeed / 1000.0f, 0.0f, 1.0f);
		real followDistance = Lerp(previousFollowDistance, targetFollowDistance, distUpdateDelta);
		glm::vec3 offsetVec = glm::normalize(upward + backward) * followDistance;

		m_SpeedFactors.AddElement(speedFactor);
		m_TargetFollowDist.AddElement(glm::length(offsetVec));

		Transform* targetTransform = m_TrackedVehicle->GetTransform();
		glm::vec2 lookOffset = MoveTowards(m_LastLookOffset, m_LookOffsetRollingAvg.currentAverage, g_DeltaTime * m_LookDistLerpSpeed);
		glm::vec3 lookOffset3 = targetTransform->GetRight() * lookOffset.x + targetTransform->GetUp() * lookOffset.y;
		m_LastLookOffset = lookOffset;

		return lookAtPos + offsetVec + lookOffset3;
	}

	void VehicleCamera::SetPosAndLookAt()
	{
		if (m_TrackedVehicle == nullptr)
		{
			return;
		}

		Transform* targetTransform = m_TrackedVehicle->GetTransform();

		m_TargetLookAtPos = targetTransform->GetWorldPosition();

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

	void VehicleCamera::FindActiveVehicle()
	{
		Player* player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player0 != nullptr)
		{
			GameObject* interactingWith = player0->GetObjectInteractingWith();
			if (interactingWith != nullptr && interactingWith->GetTypeID() == SID("vehicle"))
			{
				m_TrackedVehicle = (Vehicle*)interactingWith;
				return;
			}
		}

		PrintError("Vehicle camera failed to find active vehicle\n");
	}

	void VehicleCamera::ResetValues()
	{
		ResetOrientation();

		m_Vel = VEC3_ZERO;
		pitch = -PI_DIV_FOUR;
		SetPosAndLookAt();

		if (m_TrackedVehicle != nullptr)
		{
			Transform* targetTransform = m_TrackedVehicle->GetTransform();
			m_TargetPosRollingAvg.Reset(targetTransform->GetWorldPosition());
			m_TargetForwardRollingAvg.Reset(targetTransform->GetForward());
		}
		else
		{
			m_TargetPosRollingAvg.Reset();
			m_TargetForwardRollingAvg.Reset();
		}

		RecalculateViewProjection();
	}

} // namespace flex
