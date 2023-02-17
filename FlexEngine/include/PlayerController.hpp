#pragma once

#include "Helpers.hpp" // For TurningDir
#include "Callbacks/InputCallbacks.hpp"
#include "ConfigFile.hpp"
#include "Transform.hpp"

class btBoxShape;

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
		void FixedUpdate();
		void Destroy();

		void ResetTransformAndVelocities();

		void DrawImGuiObjects();

		void UpdateMode();

	private:
		void SnapPosToTrack(real pDistAlongTrack, bool bReversingDownTrack);

		void LoadConfigFile();
		void SerializeConfigFile();

		EventReply OnActionEvent(Action action, ActionEvent actionEvent);
		ActionCallback<PlayerController> m_ActionCallback;

		EventReply OnMouseMovedEvent(const glm::vec2& dMousePos);
		MouseMovedCallback<PlayerController> m_MouseMovedCallback;

		void PreviewPlaceItemFromInventory();
		void UpdateItemPlacementTransform();
		void AttemptPlaceItemFromInventory();
		void AttemptToIteractUsingHeldItem();
		void AttemptToPickUpItem(std::list<Pair<GameObject*, real>>& nearbyInteractables);
		glm::vec3 GetTargetItemPos();

		static const i32 CURRENT_CONFIG_FILE_VERSION = 1;

		real m_MaxMoveSpeed = 5.0f;
		real m_RotateHSpeedFirstPerson = 3.0f;
		real m_RotateHSpeedThirdPerson = 4.0f;
		real m_RotateVSpeed = 1.0f;
		real m_MouseRotateHSpeed = 1.0f;
		real m_MouseRotateVSpeed = 1.0f;
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
		bool m_bCancelPlaceItemFromInventory = false;
		bool m_bAttemptInteractLeftHand = false;
		bool m_bAttemptInteractRightHand = false;
		bool m_bAttemptPickup = false;

		bool m_bCancelPlaceWire = false;
		bool m_bSpawnWire = false;
		bool m_bDropItem = false;

		glm::vec3 m_TargetItemPlacementPos; // Position of to-be-placed item, potentially snapped to the ground
		glm::vec3 m_TargetItemPlacementPosSmoothed;
		glm::quat m_TargetItemPlacementRot; // Rotation of to-be-placed item, potentially snapped to the ground
		glm::quat m_TargetItemPlacementRotSmoothed;
		glm::vec3 m_ItemPlacementGroundedPos; // Position on ground below to-be-placed item location
		bool m_bItemPlacementValid = false;

		AudioSourceID m_PlaceItemAudioID = InvalidAudioSourceID;
		AudioSourceID m_PlaceItemFailureAudioID = InvalidAudioSourceID;

		btBoxShape* m_ItemPlacementBoundingBoxShape = nullptr;

		ConfigFile m_ConfigFile;

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
