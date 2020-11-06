#pragma once

#include "Helpers.hpp" // For TurningDir
#include "Callbacks/InputCallbacks.hpp"

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

		void DrawImGuiObjects();

		void UpdateMode();

	private:
		void SnapPosToTrack(real pDistAlongTrack, bool bReversingDownTrack);

		EventReply OnActionEvent(Action action);
		ActionCallback<PlayerController> m_ActionCallback;

		real m_MoveAcceleration = 120.0f;
		real m_MaxMoveSpeed = 20.0f;
		real m_RotateHSpeedFirstPerson = 1.5f;
		real m_RotateHSpeedThirdPerson = 4.0f;
		real m_RotateVSpeed = 1.0f;
		// If the player has a velocity magnitude of this value or lower, their
		// rotation speed will linearly decrease as their velocity approaches 0
		// TODO: Use again
		//real m_MaxSlowDownRotationSpeedVel = 10.0f;

		sec m_SecondsAttemptingToTurn = 0.0f;
		// How large the joystick x value must be to enter a turning state
		const real m_TurnStartStickXThreshold = 0.7f;
		const sec m_AttemptToTurnTimeThreshold = 0.2f;
		// How long after completing a turn around the player can start accumulating turn time again
		const sec m_TurnAroundCooldown = 0.5f;

		TurningDir m_DirTurning = TurningDir::NONE;

		bool m_bAttemptCompleteTrack = false;
		bool m_bAttemptPlaceItemFromInventory = false;
		bool m_bAttemptInteract = false;

		enum class Mode
		{
			THIRD_PERSON,
			FIRST_PERSON
		};

		Mode m_Mode = Mode::FIRST_PERSON;
		i32 m_PlayerIndex = -1;
		Player* m_Player = nullptr;


	};
} // namespace flex
