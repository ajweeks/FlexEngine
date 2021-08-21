#pragma once

#include "Helpers.hpp" // For TurningDir
#include "Callbacks/InputCallbacks.hpp"
#include "Transform.hpp"

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

		EventReply OnActionEvent(Action action, ActionEvent actionEvent);
		ActionCallback<PlayerController> m_ActionCallback;

		EventReply OnMouseMovedEvent(const glm::vec2& dMousePos);
		MouseMovedCallback<PlayerController> m_MouseMovedCallback;

		real m_MoveAcceleration = 200.0f;
		real m_MaxMoveSpeed = 20.0f;
		real m_RotateHSpeedFirstPerson = 1.5f;
		real m_RotateHSpeedThirdPerson = 4.0f;
		real m_RotateVSpeed = 1.0f;
		real m_MouseRotateHSpeed = 0.1f;
		real m_MouseRotateVSpeed = 0.1f;
		bool m_bInvertMouseV = false;
		// If the player has a velocity magnitude of this value or lower, their
		// rotation speed will linearly decrease as their velocity approaches 0
		// TODO: Use again
		//real m_MaxSlowDownRotationSpeedVel = 10.0f;

		real m_ItemPickupMaxDist = 100.0f;
		GameObject* m_ItemPickingUp = nullptr;
		const real m_ItemPickingDuration = 0.5f;
		real m_ItemPickingTimer = -1.0f;

		sec m_SecondsAttemptingToTurn = 0.0f;
		// How large the joystick x value must be to enter a turning state
		const real m_TurnStartStickXThreshold = 0.7f;
		const sec m_AttemptToTurnTimeThreshold = 0.2f;
		// How long after completing a turn around the player can start accumulating turn time again
		const sec m_TurnAroundCooldown = 0.5f;

		TurningDir m_DirTurning = TurningDir::NONE;

		bool m_bAttemptCompleteTrack = false;
		bool m_bPreviewPlaceItemFromInventory = false;
		bool m_bAttemptPlaceItemFromInventory = false;
		bool m_bAttemptInteract = false;
		bool m_bAttemptPickup = false;

		//Transform m_TargetItemPlacementTransform;
		glm::vec3 m_TargetItemPlacementPos;
		glm::quat m_TargetItemPlacementRot;
		bool m_bItemPlacementValid = false;

		AudioSourceID m_PlaceItemAudioID = InvalidAudioSourceID;
		AudioSourceID m_PlaceItemFailureAudioID = InvalidAudioSourceID;

		enum class Mode
		{
			THIRD_PERSON,
			FIRST_PERSON
		};

		Mode m_Mode = Mode::FIRST_PERSON;
		i32 m_PlayerIndex = -1;
		Player* m_Player = nullptr; // TODO: Store as GameObjectID

	};
} // namespace flex
