#pragma once

#include "BaseCamera.hpp"

#include "RollingAverage.hpp"

namespace flex
{
	class GameObject;
	class Player;

	class OverheadCamera final : public BaseCamera
	{
	public:
		explicit OverheadCamera(real FOV = glm::radians(45.0f));
		virtual ~OverheadCamera();

		virtual void Initialize() override;
		virtual void OnPostSceneChange() override;
		virtual void FixedUpdate() override;
		virtual void LateUpdate() override;

		virtual void DrawImGuiObjects() override;

		// Tilts the camera, positive values move it higher above the player
		void AddToElevation(real deltaElevation);

	private:
		glm::vec3 GetOffsetPosition(const glm::vec3& pos, const glm::vec3& playerForward);
		// Pulls the camera in towards the pivot (the player) when something solid is between them, & eases it back
		// out once it's clear. Always casts from the pivot (never from the previous camera position), so the camera
		// can't get stuck inside geometry.
		glm::vec3 ResolveCollision(const glm::vec3& pivot, const glm::vec3& desiredPos);
		void SetPosAndLookAt();
		void SetLookAt();
		void FindPlayer();
		void TrackPlayer();
		void ResetSmoothedSamples();

		void ResetValues();

		Player* m_Player0 = nullptr;

		RollingAverage<glm::vec3> m_PlayerPosRollingAvg;
		RollingAverage<glm::vec3> m_PlayerForwardRollingAvg;
		// Rolling averages after the two most recent fixed steps. The camera is placed between them each frame
		// using the same interpolation alpha as the player so the two stay in sync
		glm::vec3 m_PrevPlayerPosAvg = VEC3_ZERO;
		glm::vec3 m_CurrPlayerPosAvg = VEC3_ZERO;
		glm::vec3 m_PrevPlayerForwardAvg = VEC3_FORWARD;
		glm::vec3 m_CurrPlayerForwardAvg = VEC3_FORWARD;

		real m_ZoomLevel;
		real m_TargetZoomLevel;
		const real m_MinZoomLevel = 3.0f;
		const real m_MaxZoomLevel = 15.0f;
		const i32 m_ZoomLevels = 7;

		// Angle (radians) of the camera above the player's horizontal plane
		real m_Elevation;
		const real m_MinElevation = 0.0f;
		const real m_MaxElevation = PI_DIV_TWO - 0.05f;

		// Where we point at on the ground
		glm::vec3 m_TargetLookAtPos;
		glm::vec3 m_Vel;

		// Fraction of the desired distance from the player the camera is at (< 1 while pulled in by collision)
		real m_CollisionFraction = 1.0f;
		bool m_bCollisionHit = false;
		// Radius of the sphere swept from the player towards the camera
		const real m_CollisionRadius = 0.3f;
		// Closest the camera is pulled in to the player
		const real m_MinCollisionDistance = 0.8f;
		// Rate (1/s) the camera eases back out once clear (pulling in is immediate)
		const real m_CollisionReturnSpeed = 2.0f;

	};
} // namespace flex
