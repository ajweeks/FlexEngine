#pragma once

#include "Scene/GameObject.hpp"

#include "Types.hpp" // For TrackState
#include "Track/BezierCurve3D.hpp"
#include "Track/BezierCurveList.hpp"

namespace flex
{
	class PlayerController;

	// The player is instructed by its player controller how to move by means of its
	// transform component being updated, and it applies those changes to its rigid body itself
	class Player : public GameObject
	{
	public:
		explicit Player(i32 index, GameObjectID gameObjectID = InvalidGameObjectID);

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void DrawImGuiObjects() override;
		virtual bool AllowInteractionWith(GameObject* gameObject) override;
		virtual void SetInteractingWith(GameObject* gameObject) override;

		void SetPitch(real pitch);
		void AddToPitch(real deltaPitch);
		real GetPitch() const;

		void Reset();

		glm::vec3 GetLookDirection() const;

		i32 GetIndex() const;
		real GetHeight() const;
		PlayerController* GetController();

		real GetDistAlongTrack() const;

		void UpdateIsPossessed();

		void ClampPitch();
		void UpdateIsGrounded();

		glm::vec3 GetTrackPlacementReticlePosWS(real snapThreshold = -1.0f, bool bSnapToHandles = false) const;

		void AttachToTrack(TrackID trackID, real distAlongTrack);
		void DetachFromTrack();
		bool IsFacingDownTrack() const;
		void BeginTurnTransition();

		i32 GetNextFreeQuickAccessInventorySlot();
		i32 GetNextFreeInventorySlot();

		bool IsRidingTrack();

		GameObjectStack* GetGameObjectStackFromInventory(GameObjectStackID stackID);
		bool MoveItem(GameObjectStackID fromID, GameObjectStackID toID);
		static GameObjectStackID GetGameObjectStackIDForQuickAccessInventory(i32 slotIndex);
		static GameObjectStackID GetGameObjectStackIDForInventory(i32 slotIndex);

		void AddToInventory(const PrefabID& prefabID, i32 count);

		void ClearInventory();
		void ParseInventoryFile();
		void SerializeInventoryToFile();

		PlayerController* m_Controller = nullptr;
		i32 m_Index = 0;

		// TODO: Store IDs rather than raw pointer
		GameObject* m_MapTablet = nullptr;
		GameObject* m_MapTabletHolder = nullptr;
		deg m_TabletOrbitAngleUp = 13.3f;
		deg m_TabletOrbitAngleDown = -45.0f;
		real m_TabletOrbitAngle = m_TabletOrbitAngleUp;
		bool m_bTabletUp = false;

		real m_MoveFriction = 12.0f;
		real m_Height = 4.0f;

		real m_Pitch = 0.0f;

		i32 m_CurveNodesPlaced = 0;
		BezierCurveList m_TrackPlacing;
		BezierCurve3D m_CurvePlacing;
		bool m_bPlacingTrack = false;
		TrackID m_TrackEditingID = InvalidTrackID;
		i32 m_TrackEditingCurveIdx = -1;
		i32 m_TrackEditingPointIdx = -1;
		bool m_bEditingTrack = false;
		glm::vec3 m_TrackPlacementReticlePos; // Local offset

		bool m_bGrounded = false;
		bool m_bPossessed = false;

		TrackID m_TrackRidingID = InvalidTrackID;
		real m_DistAlongTrack = 0.0f;
		real m_TrackMoveSpeed = 0.20f;
		real m_pDTrackMovement = 0.0f;

		real m_TrackAttachMinDist = 4.0f;

		TrackState m_TrackState;

		static const i32 QUICK_ACCESS_ITEM_COUNT = 11;
		static const i32 INVENTORY_ITEM_ROW_COUNT = 5;
		static const i32 INVENTORY_ITEM_COL_COUNT = 7;
		static const i32 INVENTORY_ITEM_COUNT = INVENTORY_ITEM_ROW_COUNT * INVENTORY_ITEM_COL_COUNT;
		static const i32 MAX_STACK_SIZE = 32;

		std::array<GameObjectStack, INVENTORY_ITEM_COUNT> m_Inventory;
		std::array<GameObjectStack, QUICK_ACCESS_ITEM_COUNT> m_QuickAccessInventory;
		bool bInventoryShowing = false;
		i32 heldItemSlot = 0;

		const real m_TurnToFaceDownTrackInvSpeed = 25.0f;
		const real m_FlipTrackDirInvSpeed = 45.0f;

	private:
		friend class PlayerController;

		AudioSourceID m_SoundPlaceTrackNodeID = InvalidAudioSourceID;
		AudioSourceID m_SoundPlaceFinalTrackNodeID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackAttachID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackDetachID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackSwitchDirID = InvalidAudioSourceID;

		TextureID m_CrosshairTextureID = InvalidTextureID;
	};
} // namespace flex
