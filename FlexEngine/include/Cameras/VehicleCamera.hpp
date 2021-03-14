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
		virtual void OnSceneChanged() override;
		virtual void Update() override;

		virtual void DrawImGuiObjects() override;

	private:
		glm::vec3 GetOffsetPosition(const glm::vec3& pos);
		void SetPosAndLookAt();
		void SetLookAt();
		void FindActiveVehicle();

		void ResetValues();

		Vehicle* m_TrackedVehicle = nullptr;

		RollingAverage<glm::vec3> m_TargetPosRollingAvg;
		RollingAverage<glm::vec3> m_TargetForwardRollingAvg;
		RollingAverage<real> m_TargetVelMagnitudeRollingAvg;

		glm::vec3 m_TargetLookAtPos;
		glm::vec3 m_Vel;
		real m_DistanceUpdateSpeed = 1.0f;
		real m_RotationUpdateSpeed = 10.0f;

		real m_ClosestDist = 10.0f;
		real m_FurthestDist = 14.0f;
		real m_MaxDownwardAngle = 0.35f; // Angle which is used when vehicle is at min speed
		real m_MinDownwardAngle = 0.2f; // Angle which is used when vehicle is at max speed
		real m_MinSpeed = 2.0f; // Distance at which when begin zooming out
		real m_MaxSpeed = 15.0f; // Distance at which we're the furthest zoomed out

		// Debug
		Histogram m_SpeedFactors;
		Histogram m_TargetFollowDist;

	};
} // namespace flex
