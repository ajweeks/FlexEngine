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

		real GetTrackAttachDistThreshold() const;
		real GetDistAlongTrack() const;
		BezierCurveList* GetTrackRiding() const;

		void DrawImGuiObjects();

	private:
		void SnapPosToTrack(real pDistAlongTrack);

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
		bool m_bFirstPerson = false;

		BezierCurveList* m_TrackRiding = nullptr;
		real m_DistAlongTrack = 0.0f;
		real m_TrackMoveSpeed = 0.20f;
		real m_TrackAttachMinDist = 4.0f;
		// Is true when player began accelerating while facing down the track
		real m_pDTrackMovement = 0.0f;
		bool m_bUpdateFacingAndForceFoward = false;

		enum class TurningDir
		{
			LEFT,
			NONE,
			RIGHT
		} m_DirTurning;

		sec m_SecondsAttemptingToTurn = 0.0f;
		// How large the joystick x value must be to enter a turning state
		const real m_TurnStartStickXThreshold = 0.15f;
		// How large the dot product between our forward and the track forward must be to turn around
		const real m_MinForDotTurnThreshold = 0.03f;
		const sec m_AttemptToTurnTimeThreshold = 0.2f;
		// How long after completing a turn around the player can start accumulating turn time again
		const sec m_TurnAroundCooldown = 0.5f;

		glm::vec3 m_TargetTrackFor;
		bool m_bTurningAroundOnTrack = false;

		const real m_TurnToFaceDownTrackInvSpeed = 30.0f;
		const real m_FlipTrackDirInvSpeed = 45.0f;

		AudioSourceID m_SoundTrackAttachID;
		AudioSourceID m_SoundTrackDetachID;

	};
} // namespace flex
