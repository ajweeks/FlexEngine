#pragma once

namespace flex
{
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

	private:
		real m_MoveAcceleration = 140.0f;
		real m_MaxMoveSpeed = 24.0f;
		real m_RotateHSpeed = 4.0f;
		real m_RotateVSpeed = 1.5f;
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

	};
} // namespace flex
