#pragma once

namespace flex
{
	class BezierCurveList;
	class Player;

	// The player controller is responsible for setting the player's
	// transform based on either player input, or an AI behavior
	class PlayerController final
	{
	public:
		PlayerController();
		~PlayerController();

		void Initialize(Player* player);
		void Update();
		void Destroy();

		void ResetTransformAndVelocities();

		void UpdateIsPossessed();
		bool IsPossessed() const;

		real GetRailAttachDistThreshold() const;
		real GetDistAlongRail() const;
		BezierCurveList* GetRailRiding() const;

	private:
		void SnapPosToRail();

		real m_MoveAcceleration = 120.0f;
		real m_MaxMoveSpeed = 20.0f;
		real m_RotateHSpeed = 4.5f;
		real m_RotateVSpeed = 1.6f;
		real m_RotateFriction = 0.03f;
		// How quickly to turn towards direction of movement
		real m_RotationSnappiness = 80.0f;
		// If the player has a velocity magnitude of this value or lower, their
		// rotation speed will linearly decrease as their velocity approaches 0
		real m_MaxSlowDownRotationSpeedVel = 10.0f;

		enum class Mode
		{
			THIRD_PERSON,
			FIRST_PERSON
		};

		Mode m_Mode = Mode::FIRST_PERSON;
		i32 m_PlayerIndex = -1;
		Player* m_Player = nullptr;

		bool m_bGrounded = false;
		bool m_bPossessed = false;

		BezierCurveList* m_RailRiding = nullptr;
		real m_DistAlongRail = 0.0f;
		real m_RailMoveSpeed = 0.25f;
		real m_RailAttachMinDist = 4.0f;
		// Is true when player began accelerating while facing down the rail
		real m_pDRailMovement = 0.0f;
		bool m_bMovingForwardDownRail = true;

		AudioSourceID m_SoundRailAttachID;
		AudioSourceID m_SoundRailDetachID;

	};
} // namespace flex
