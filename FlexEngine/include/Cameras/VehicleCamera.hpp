#pragma once

#include "BaseCamera.hpp"

#include "Histogram.hpp"
#include "RollingAverage.hpp"

namespace flex
{
	class GameObject;
	class Vehicle;

	class VehicleCamera final : public BaseCamera
	{
	public:
		explicit VehicleCamera(real FOV = glm::radians(45.0f));
		virtual ~VehicleCamera();

		virtual void Initialize() override;
		virtual void OnPostSceneChange() override;
		virtual void FixedUpdate() override;
		virtual void Update() override;
		virtual void LateUpdate() override;

		virtual void DrawImGuiObjects() override;

		// Smoothness measurements (for tests, also enabled by the FLEX_VEHICLE_CAM_STATS environment variable)
		struct JitterStats
		{
			u32 frameCount = 0;
			// Frames around hitches (frame time far from the recent average) aren't measured, they jump regardless
			u32 hitchFrameCount = 0;
			real dtSum = 0.0f;
			real dtSqrSum = 0.0f;
			// Second difference of the target's position in view space (zero when it moves smoothly on screen)
			real viewSecondDiffSqrSum = 0.0f;
			real viewSecondDiffMax = 0.0f;
			// Frame to frame change of the camera to target offset
			glm::vec3 offsetDeltaSum = VEC3_ZERO;
			glm::vec3 offsetDeltaSqrSum = VEC3_ZERO;
			// Camera acceleration
			real camAccelSqrSum = 0.0f;
			real camAccelMax = 0.0f;
		};
		static void SetStatsEnabled(bool bEnabled);
		// Tilts the camera up (-) or down (+) from its default angle, in radians (mouse Y does this too)
		void AddPitchOffset(real radians);
		real GetPitchOffset() const { return m_PitchOffset; }
		static void ResetStats();
		static void PrintStats(const char* label);

	private:
		glm::vec3 GetOffsetPosition(const glm::vec3& pos, const glm::vec3& targetForward);
		void ResetSmoothedSamples();
		void SetPosAndLookAt();
		void SetLookAt();
		void FindActiveVehicle();

		void ResetValues();

		// The vehicle or boat being ridden
		GameObject* m_TrackedVehicle = nullptr;

		RollingAverage<glm::vec3> m_TargetPosRollingAvg;
		RollingAverage<glm::vec3> m_TargetForwardRollingAvg;
		RollingAverage<real> m_TargetVelMagnitudeRollingAvg;
		// Rolling averages after the two most recent fixed steps. The camera is placed between them each frame
		// using the physics interpolation alpha so it stays in sync with the (interpolated) vehicle
		glm::vec3 m_PrevTargetPosAvg = VEC3_ZERO;
		glm::vec3 m_CurrTargetPosAvg = VEC3_ZERO;
		glm::vec3 m_PrevTargetForwardAvg = VEC3_FORWARD;
		glm::vec3 m_CurrTargetForwardAvg = VEC3_FORWARD;
		real m_PrevTargetSpeedAvg = 0.0f;
		real m_CurrTargetSpeedAvg = 0.0f;
		real m_TargetSpeed = 0.0f; // Interpolated between the two above
		real m_FollowDistance = -1.0f; // < 0: snap to the target distance

		glm::vec3 m_TargetLookAtPos;
		glm::vec3 m_Vel;
		real m_DistanceUpdateSpeed = 8.0f; // Rate the follow distance approaches its target (1/s)
		real m_RotationUpdateSpeed = 10.0f;

		real m_ClosestDist = 10.0f;
		real m_FurthestDist = 14.0f;
		real m_MaxDownwardAngle = 0.35f; // Angle which is used when vehicle is at min speed
		real m_MinDownwardAngle = 0.2f; // Angle which is used when vehicle is at max speed
		real m_MinSpeed = 2.0f; // Distance at which when begin zooming out
		real m_MaxSpeed = 15.0f; // Distance at which we're the furthest zoomed out

		RollingAverage<glm::vec2> m_LookOffsetRollingAvg;
		glm::vec2 m_LastLookOffset;
		real m_LookDistLerpSpeed = 15.0f;

		// Player controlled tilt (mouse Y), added to the angle the camera looks down at the vehicle from
		real m_PitchOffset = 0.0f;
		real m_MousePitchSensitivity = 0.003f;
		real m_MinElevation = glm::radians(-5.0f);
		real m_MaxElevation = glm::radians(75.0f);
		real m_LookOffsetMagnitude = 1.25f;

		void UpdateStats();

		struct StatsHistory
		{
			u32 count = 0;
			glm::vec3 viewPos[2];
			glm::vec3 offset;
			glm::vec3 camPos;
			glm::vec3 camVel;
			real dtAverage = 0.0f;
			u32 framesSinceHitch = 0;
		};
		StatsHistory m_StatsHistory;

		// Debug
		Histogram m_SpeedFactors;
		Histogram m_TargetFollowDist;

	};
} // namespace flex
