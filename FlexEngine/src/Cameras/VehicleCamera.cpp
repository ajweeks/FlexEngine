#include "stdafx.hpp"

#include "Cameras/VehicleCamera.hpp"

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
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/Vehicle.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	namespace
	{
		bool s_bStatsEnabled = getenv("FLEX_VEHICLE_CAM_STATS") != nullptr;
		VehicleCamera::JitterStats s_Stats;
		// Set to reset the history (so a gap in sampling doesn't register as a jump)
		bool s_bStatsHistoryReset = true;
	}

	void VehicleCamera::SetStatsEnabled(bool bEnabled)
	{
		s_bStatsEnabled = bEnabled;
		ResetStats();
	}

	void VehicleCamera::ResetStats()
	{
		s_Stats = {};
		s_bStatsHistoryReset = true;
	}

	void VehicleCamera::PrintStats(const char* label)
	{
		const JitterStats& s = s_Stats;
		if (s.frameCount < 3)
		{
			Print("Vehicle camera stats (%s): not enough frames\n", label);
			return;
		}
		const real n = (real)s.frameCount;
		const real dtMean = s.dtSum / n;
		const real dtStd = glm::sqrt(glm::max(s.dtSqrSum / n - dtMean * dtMean, 0.0f));
		const glm::vec3 offsetMean = s.offsetDeltaSum / n;
		const glm::vec3 offsetVar = glm::max(s.offsetDeltaSqrSum / n - offsetMean * offsetMean, VEC3_ZERO);
		Print("Vehicle camera stats (%s): %u frames (%u near hitches skipped), dt %.2fms (std %.2fms) | target on screen 2nd diff: rms %.2fmm max %.2fmm | "
			"cam-target offset delta std: %.2fmm (x %.2f y %.2f z %.2f) | cam accel: rms %.2f max %.2f m/s^2\n",
			label, s.frameCount, s.hitchFrameCount, dtMean * 1000.0f, dtStd * 1000.0f,
			glm::sqrt(s.viewSecondDiffSqrSum / n) * 1000.0f, s.viewSecondDiffMax * 1000.0f,
			glm::sqrt(offsetVar.x + offsetVar.y + offsetVar.z) * 1000.0f,
			glm::sqrt(offsetVar.x) * 1000.0f, glm::sqrt(offsetVar.y) * 1000.0f, glm::sqrt(offsetVar.z) * 1000.0f,
			glm::sqrt(s.camAccelSqrSum / n), s.camAccelMax);
	}

	void VehicleCamera::UpdateStats()
	{
		if (!s_bStatsEnabled || m_TrackedVehicle == nullptr || g_DeltaTime <= 0.0f)
		{
			return;
		}

		// Per frame values for offline analysis
		static FILE* csv = nullptr;
		// FLEX_VEHICLE_CAM_CSV=<any value>: writes per frame camera & vehicle values (dt, alpha, positions, look-at, speed) to vehicle_cam.csv in the working directory
		if (csv == nullptr && getenv("FLEX_VEHICLE_CAM_CSV") != nullptr)
		{
			csv = fopen("vehicle_cam.csv", "w");
			if (csv != nullptr)
			{
				fprintf(csv, "dt,alpha,tx,ty,tz,cx,cy,cz,lx,ly,lz,fx,fy,fz,speed\n");
			}
		}
		if (csv != nullptr)
		{
			PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();
			const glm::vec3 t = m_TrackedVehicle->GetTransform()->GetWorldPosition();
			fprintf(csv, "%.6f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f\n", g_DeltaTime,
				physicsWorld != nullptr ? physicsWorld->GetInterpolationAlpha() : -1.0f,
				t.x, t.y, t.z, position.x, position.y, position.z, m_TargetLookAtPos.x, m_TargetLookAtPos.y, m_TargetLookAtPos.z,
				forward.x, forward.y, forward.z, m_TargetVelMagnitudeRollingAvg.currentAverage);
		}

		StatsHistory& h = m_StatsHistory;
		if (s_bStatsHistoryReset)
		{
			h.count = 0;
			s_bStatsHistoryReset = false;
		}

		// The vehicle's rendered (interpolated) pose
		const glm::vec3 targetPos = m_TrackedVehicle->GetTransform()->GetWorldPosition();
		const glm::vec3 viewPos = glm::vec3(GetView() * glm::vec4(targetPos, 1.0f));
		const glm::vec3 offset = position - targetPos;
		const glm::vec3 camVel = (position - h.camPos) / g_DeltaTime;

		// Frame times which differ a lot from the recent average (streaming hitches) aren't representative
		const bool bHitch = h.dtAverage > 0.0f && (g_DeltaTime > h.dtAverage * 1.5f || g_DeltaTime < h.dtAverage / 1.5f);
		h.dtAverage = h.dtAverage > 0.0f ? Lerp(h.dtAverage, g_DeltaTime, 0.05f) : g_DeltaTime;
		h.framesSinceHitch = bHitch ? 0 : h.framesSinceHitch + 1;
		if (h.count >= 2 && h.framesSinceHitch < 3)
		{
			++s_Stats.hitchFrameCount;
		}
		else if (h.count >= 2)
		{
			const real secondDiff = glm::length(viewPos - 2.0f * h.viewPos[0] + h.viewPos[1]);
			const glm::vec3 offsetDelta = offset - h.offset;
			const real camAccel = glm::length(camVel - h.camVel) / g_DeltaTime;

			JitterStats& s = s_Stats;
			++s.frameCount;
			s.dtSum += g_DeltaTime;
			s.dtSqrSum += g_DeltaTime * g_DeltaTime;
			s.viewSecondDiffSqrSum += secondDiff * secondDiff;
			s.viewSecondDiffMax = glm::max(s.viewSecondDiffMax, secondDiff);
			s.offsetDeltaSum += offsetDelta;
			s.offsetDeltaSqrSum += offsetDelta * offsetDelta;
			s.camAccelSqrSum += camAccel * camAccel;
			s.camAccelMax = glm::max(s.camAccelMax, camAccel);
		}

		h.viewPos[1] = h.viewPos[0];
		h.viewPos[0] = viewPos;
		h.offset = offset;
		h.camPos = position;
		h.camVel = camVel;
		h.count = glm::min(h.count + 1, 3u);
	}

	void VehicleCamera::AddPitchOffset(real radians)
	{
		// The final angle is clamped too (the default angle varies with speed), this just stops it winding up
		m_PitchOffset = glm::clamp(m_PitchOffset + radians, m_MinElevation - m_MaxDownwardAngle, m_MaxElevation);
	}

	VehicleCamera::VehicleCamera(real FOV) :
		BaseCamera("vehicle", CameraType::VEHICLE, true, FOV),
		m_SpeedFactors(256),
		m_TargetFollowDist(256),
		m_LastLookOffset(VEC2_ZERO)
	{
		bDEBUGCyclable = false;
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
			FindActiveVehicle();

			m_TargetPosRollingAvg = RollingAverage<glm::vec3>(15, SamplingType::LINEAR);
			m_TargetForwardRollingAvg = RollingAverage<glm::vec3>(30, SamplingType::LINEAR);
			m_TargetVelMagnitudeRollingAvg = RollingAverage<real>(30, SamplingType::LINEAR);
			m_LookOffsetRollingAvg = RollingAverage<glm::vec2>(15, SamplingType::CUBIC);

			ResetValues();

			BaseCamera::Initialize();
		}
	}

	void VehicleCamera::OnPostSceneChange()
	{
		BaseCamera::OnPostSceneChange();

		FindActiveVehicle();

		ResetValues();
	}

	void VehicleCamera::FixedUpdate()
	{
		if (m_TrackedVehicle == nullptr)
		{
			return;
		}

		// Sample once per fixed step (after the vehicle has moved) so the averages track the simulation at a steady rate
		Transform* targetTransform = m_TrackedVehicle->GetTransform();
		btRigidBody* trackedRB = m_TrackedVehicle->GetRigidBody()->GetRigidBodyInternal();

		m_TargetForwardRollingAvg.AddValue(targetTransform->GetForward());
		m_TargetVelMagnitudeRollingAvg.AddValue(trackedRB->getLinearVelocity().length());
		m_TargetPosRollingAvg.AddValue(targetTransform->GetWorldPosition());

		m_PrevTargetPosAvg = m_CurrTargetPosAvg;
		m_CurrTargetPosAvg = m_TargetPosRollingAvg.currentAverage;
		m_PrevTargetForwardAvg = m_CurrTargetForwardAvg;
		m_CurrTargetForwardAvg = m_TargetForwardRollingAvg.currentAverage;
		m_PrevTargetSpeedAvg = m_CurrTargetSpeedAvg;
		m_CurrTargetSpeedAvg = m_TargetVelMagnitudeRollingAvg.currentAverage;
	}

	void VehicleCamera::Update()
	{
		BaseCamera::Update();

		if (m_TrackedVehicle == nullptr)
		{
			return;
		}

		// Mouse Y tilts the camera (moving the mouse down looks further down at the vehicle, like first person)
		if (g_Window->GetCursorMode() == CursorMode::DISABLED && g_Window->HasFocus() && bIsGameplayCam)
		{
			const real mouseDeltaY = g_InputManager->GetMouseMovement().y;
			if (mouseDeltaY != 0.0f)
			{
				AddPitchOffset(mouseDeltaY * m_MousePitchSensitivity);
			}
		}

		real newLookOffsetH = -g_InputManager->GetActionAxisValue(Action::VEHICLE_LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::VEHICLE_LOOK_RIGHT);
		real newLookOffsetV = -g_InputManager->GetActionAxisValue(Action::VEHICLE_LOOK_UP) + g_InputManager->GetActionAxisValue(Action::VEHICLE_LOOK_DOWN);
		m_LookOffsetRollingAvg.AddValue(glm::vec2(newLookOffsetH, newLookOffsetV) * m_LookOffsetMagnitude);
	}

	// Positioned after the scene has updated so the camera matches the vehicle's interpolated transform this frame
	void VehicleCamera::LateUpdate()
	{
		if (m_TrackedVehicle != nullptr)
		{
			PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();
			real alpha = (physicsWorld != nullptr) ? physicsWorld->GetInterpolationAlpha() : 1.0f;
			m_TargetLookAtPos = glm::mix(m_PrevTargetPosAvg, m_CurrTargetPosAvg, alpha);
			glm::vec3 targetForward = glm::mix(m_PrevTargetForwardAvg, m_CurrTargetForwardAvg, alpha);
			// The speed (which sets the follow distance & angle) is interpolated too, otherwise they step at the physics rate
			m_TargetSpeed = Lerp(m_PrevTargetSpeedAvg, m_CurrTargetSpeedAvg, alpha);

#if THOROUGH_CHECKS
			ENSURE(!IsNanOrInf(m_TargetLookAtPos));
#endif

			// Position first, then aim from there. (Aiming from last frame's position made the view direction lag
			// by a frame, by an amount which varied with the frame time: the main cause of the jitter, especially
			// while turning.)
			position = GetOffsetPosition(m_TargetLookAtPos, targetForward);
			SetLookAt();

			CalculateYawAndPitchFromForward();
			RecalculateViewProjection();

			UpdateStats();
		}

		BaseCamera::LateUpdate();
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
				g_Renderer->GetDebugRenderer()->drawLine(ToBtVec3(start), ToBtVec3(end), btVector3(1.0f, 1.0f, 1.0f));

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
				ImGui::SliderFloat("Pitch offset", &m_PitchOffset, m_MinElevation - m_MaxDownwardAngle, m_MaxElevation);
				ImGui::SliderFloat("Mouse pitch sensitivity", &m_MousePitchSensitivity, 0.0f, 0.02f);
				ImGui::SliderFloat("Look offset magnitude", &m_LookOffsetMagnitude, 0.0f, 10.0f);

				m_SpeedFactors.DrawImGui();
				m_TargetFollowDist.DrawImGui();
			}
		}
	}

	glm::vec3 VehicleCamera::GetOffsetPosition(const glm::vec3& lookAtPos, const glm::vec3& targetForward)
	{
		// TODO: Handle camera cut to stationary vehicle? (use vehicle forward rather than vel)
		glm::vec3 backward = -targetForward;

		real minOffsetY = 0.0f;
		if (backward.y < minOffsetY)
		{
			backward.y = minOffsetY;
			backward = glm::normalize(backward);
		}

		// TODO: Check for collisions

		real speedFactor = glm::clamp((m_TargetSpeed - m_MinSpeed) / (m_MaxSpeed - m_MinSpeed), 0.0f, 1.0f);
		glm::vec3 upward = VEC3_UP * Lerp(m_MaxDownwardAngle, m_MinDownwardAngle, speedFactor);
		real targetFollowDistance = Lerp(m_ClosestDist, m_FurthestDist, speedFactor);
		// Frame rate independent smoothing
		if (m_FollowDistance < 0.0f)
		{
			m_FollowDistance = targetFollowDistance;
		}
		m_FollowDistance = Lerp(m_FollowDistance, targetFollowDistance, 1.0f - glm::exp(-g_DeltaTime * m_DistanceUpdateSpeed));
		real followDistance = m_FollowDistance;
		glm::vec3 offsetDir = glm::normalize(upward + backward);
		if (m_PitchOffset != 0.0f)
		{
			// Tilt about the camera's horizontal axis by the player's pitch offset
			glm::vec3 horizontal = glm::vec3(offsetDir.x, 0.0f, offsetDir.z);
			horizontal = glm::length(horizontal) > 1e-4f ? glm::normalize(horizontal) : -VEC3_FORWARD;
			const real elevation = glm::clamp(glm::asin(glm::clamp(offsetDir.y, -1.0f, 1.0f)) + m_PitchOffset, m_MinElevation, m_MaxElevation);
			offsetDir = horizontal * glm::cos(elevation) + VEC3_UP * glm::sin(elevation);
		}
		glm::vec3 offsetVec = offsetDir * followDistance;

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

		glm::vec3 desiredPos = GetOffsetPosition(m_TargetLookAtPos, targetTransform->GetForward());
		position = desiredPos;

		SetLookAt();
	}

	void VehicleCamera::SetLookAt()
	{
		forward = glm::normalize(m_TargetLookAtPos - position);
		right = normalize(glm::cross(VEC3_UP, forward));
		up = cross(forward, right);
	}

	void VehicleCamera::ResetSmoothedSamples()
	{
		m_CurrTargetPosAvg = m_PrevTargetPosAvg = m_TargetPosRollingAvg.currentAverage;
		m_CurrTargetForwardAvg = m_PrevTargetForwardAvg = m_TargetForwardRollingAvg.currentAverage;
		m_CurrTargetSpeedAvg = m_PrevTargetSpeedAvg = m_TargetSpeed = m_TargetVelMagnitudeRollingAvg.currentAverage;
		m_FollowDistance = -1.0f;
	}

	void VehicleCamera::FindActiveVehicle()
	{
		Player* player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player0 != nullptr)
		{
			m_TrackedVehicle = player0->GetRidingVehicleID().Get();
		}
		else
		{
			m_TrackedVehicle = nullptr;
		}
	}

	void VehicleCamera::ResetValues()
	{
		ResetOrientation();

		m_Vel = VEC3_ZERO;
		pitch = -PI_DIV_FOUR;
		m_FollowDistance = -1.0f;
		m_TargetVelMagnitudeRollingAvg.Reset();
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

		ResetSmoothedSamples();

		RecalculateViewProjection();
	}

} // namespace flex
