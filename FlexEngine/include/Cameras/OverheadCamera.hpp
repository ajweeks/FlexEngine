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
		// When even that would bring it closer than m_MinObstructedDistance, it stays that far back & moves sideways instead
		glm::vec3 ResolveCollision(const glm::vec3& pivot, const glm::vec3& desiredPos);
		// How far a sphere (m_CollisionRadius) can move from 'from' towards 'to' before hitting something large & static
		real ClearDistance(const glm::vec3& from, const glm::vec3& to) const;
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
		// Closest the camera comes when obstructed, if there's a clear view from the side at that distance
		const real m_MinObstructedDistance = 2.5f;
		// Current sideways offset (meters, along the camera's right) & the side last used
		real m_SideOffset = 0.0f;
		real m_SideTarget = 0.0f;
		real m_SideSign = 1.0f;
		bool m_bPushedAside = false;
		// Rate (1/s) the sideways offset eases towards its target
		const real m_SideOffsetSpeed = 5.0f;

	};
} // namespace flex
