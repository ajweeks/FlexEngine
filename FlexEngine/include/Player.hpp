#pragma once

#include "Scene/GameObject.hpp"

#include "ResourceManager.hpp"
#include "Types.hpp" // For TrackState
#include "Track/BezierCurve3D.hpp"
#include "Track/BezierCurveList.hpp"
#include "Systems/TrackManager.hpp" // For TrackPointRef

namespace flex
{
	class PlayerController;

	enum class InventoryType
	{
		PLAYER_INVENTORY,
		MINER_INVENTORY,
		QUICK_ACCESS,
		WEARABLES,
		NONE
	};

	enum class Hand
	{
		LEFT,
		RIGHT
	};

	// The player is instructed by its player controller how to move by means of its
	// transform component being updated, and it applies those changes to its rigid body itself
	static constexpr StringID PlayerSID = SID("player");
	class Player : public GameObject
	{
	public:
		explicit Player(i32 index, GameObjectID gameObjectID);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Update() override;
		virtual void FixedUpdate() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		void SetPitch(real pitch);
		void AddToPitch(real deltaPitch);
		real GetPitch() const;
		glm::quat GetLookYawRotation() const;

		void Reset();

		glm::vec3 GetLookDirection() const;
		glm::quat GetLookRotation() const;
		glm::vec3 GetHeldItemPosWS(Hand hand) const;

		i32 GetIndex() const;
		real GetHeight() const;
		PlayerController* GetController();

		real GetDistAlongTrack() const;

		void UpdateIsPossessed();

		void ClampPitch();
		void UpdateIsGrounded();

		void AttachToTrack(TrackID trackID, real distAlongTrack);
		void DetachFromTrack();
		bool IsFacingDownTrack() const;
		void BeginTurnTransition();

		void DropSelectedItem();
		bool HasFullSelectedInventorySlot();
		i32 GetNextFreeInventorySlot();
		i32 GetNextFreeQuickAccessInventorySlot();
		i32 GetNextFreeMinerInventorySlot();

		bool IsRidingTrack();

		GameObjectStack* GetGameObjectStackFromInventory(GameObjectStackID stackID, InventoryType& outInventoryType);
		bool MoveItemStack(GameObjectStackID fromID, GameObjectStackID toID);
		bool MoveSingleItemFromStack(GameObjectStackID fromID, GameObjectStackID toID);
		bool DropItemStack(GameObjectStackID stackID, bool bDestroyItem);
		bool DropSingleItemFromStack(GameObjectStackID stackID, bool bDestroyItem);
		i32 MoveStackBetweenInventories(GameObjectStackID stackID, InventoryType destInventoryType, i32 countToMove);
		static GameObjectStackID GetGameObjectStackIDForInventory(u32 slotIndex);
		static GameObjectStackID GetGameObjectStackIDForQuickAccessInventory(u32 slotIndex);
		static GameObjectStackID GetGameObjectStackIDForWearablesInventory(u32 slotIndex);
		static GameObjectStackID GetGameObjectStackIDForMinerInventory(u32 slotIndex);

		// Adds the specified items to any inventory with space
		void AddToInventory(DroppedItem* droppedItem);
		void AddToInventory(const PrefabID& prefabID, i32 count);
		void AddToInventory(const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData);
		// Adds the given items to the specified inventory, returns number of items that weren't added
		u32 AddToInventory(const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData, InventoryType inventoryType);

		u32 MoveToInventory(GameObjectStack* inventory, u32 inventorySize, const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData);

		void ClearInventory();
		void ParseInventoryFile();
		void SerializeInventoryToFile();

		void SetInteractingWithTerminal(Terminal* terminal);
		void SetRidingVehicle(Vehicle* vehicle);

		bool PickupWithFreeHand(GameObject* object);
		bool IsHolding(GameObject* object);
		void DropIfHolding(GameObject* object);
		bool HasFreeHand() const;

		void OnWearableEquipped(GameObjectStack const* wearableStack);
		void OnWearableUnequipped(GameObjectStack const* wearableStack);

		bool IsAnyInventoryShowing() const;
		bool IsInventoryShowing() const;
		bool IsMinerInventoryShowing() const;

		i32 GetSelectedQuickAccessItemSlot() const { return m_SelectedQuickAccessItemSlot; }

		// Tracks
		bool IsPlacingTrack() const { return m_TrackBuildingContext.m_bPlacingTrack; }
		bool IsEditingTrack() const { return m_TrackBuildingContext.m_bEditingTrack; }
		// Called every frame while placing or editing
		void UpdateTrackBuilding();
		// Places a node when placing, grabs/releases the hovered point when editing
		void OnTrackBuildingPrimaryAction();
		// Removes the last placed node when placing, deletes the hovered node when editing
		void OnTrackBuildingUndoAction();
		// Deletes the highlighted track when editing
		void OnTrackBuildingDeleteTrackAction();
		bool AttemptCompleteTrack(const glm::vec3& endDir = VEC3_ZERO);
		void TogglePlacingTrack();
		void ToggleEditingTrack();
		void OnTrackRemoved(TrackID removedTrackID);
		void DrawTrackBuildingHUD() const;

		GameObjectID GetRidingVehicleID() const { return m_RidingVehicleID; }

		GameObjectID GetHeldItem(Hand hand) { if (hand == Hand::LEFT) return m_HeldItemLeftHand; return m_HeldItemRightHand; }
		void SetHeldItem(Hand hand, GameObjectID gameObjectID);

		void SpawnWire();

		bool AbleToInteract() const;

		TrackID GetTrackRidingID() const { return m_TrackRidingID; }
		real GetTrackAttachMinDist() const { return m_TrackAttachMinDist; }

		void SetSelectedQuickAccessItemSlot(i32 selectedQuickAccessItemSlot);

		void ResetItemPickingTimer();

		GameObject* GetObjectPointedAt() const;
		bool IsWieldingPickAxe() const;

		static const u32 WEARABLES_ITEM_COUNT = 3;
		static const u32 QUICK_ACCESS_ITEM_COUNT = 11;
		static const u32 INVENTORY_ITEM_ROW_COUNT = 5;
		static const u32 INVENTORY_ITEM_COL_COUNT = 7;
		static const u32 INVENTORY_ITEM_COUNT = INVENTORY_ITEM_ROW_COUNT * INVENTORY_ITEM_COL_COUNT;

		void SetItemPickingUp(GameObject* pickedItem);
		real GetItemPickingTimer() const { return m_ItemPickingTimer; }
		real GetItemPickingDuration() const { return m_ItemPickingDuration; }

	private:
		friend class PlayerController;

		void CreateDroppedItemFromStack(GameObjectStack* stack);

		struct GrabbedTrackEndDirs
		{
			TrackID trackID = InvalidTrackID;
			glm::vec3 startDir = VEC3_ZERO;
			glm::vec3 endDir = VEC3_ZERO;
		};

		struct TrackBuildingContext
		{
			bool m_bPlacingTrack = false; // Placing a new track
			bool m_bEditingTrack = false; // Editing an existing track

			// Nodes of the track being placed, curve handles are generated to smoothly pass through them
			std::vector<glm::vec3> m_PlacedNodes;
			// Non-zero when the first node was snapped onto an existing track, so the new track lines up with it
			glm::vec3 m_StartDir = VEC3_ZERO;

			// Where the player is aiming, updated every frame
			bool m_bReticleValid = false;
			glm::vec3 m_ReticlePos = VEC3_ZERO;
			// Point of an existing track the reticle is snapped to/hovering over
			TrackPointRef m_HoveredPoint;
			// Index into m_PlacedNodes the reticle is snapped to (placing here closes a loop)
			i32 m_HoveredPlacedNodeIndex = -1;
			// Track under the reticle in edit mode, which will be deleted by the delete action
			TrackID m_HoveredTrackID = InvalidTrackID;

			// Nodes being dragged in edit mode (several when dragging a junction)
			std::vector<TrackPointRef> m_GrabbedPoints;
			// Tracks being reshaped by the drag, and the directions their junction ends are held at
			std::vector<GrabbedTrackEndDirs> m_GrabbedTrackEndDirs;

			// Config vars
			real m_SnapThreshold = 1.0f;
			real m_MaxReach = 30.0f;
		};

		void UpdateTrackBuildingReticle();
		void ClearTrackBuildingState();

		static const glm::vec3 HEADLAMP_MOUNT_POS;

		static const u32 INVENTORY_MIN = 0;
		static const u32 INVENTORY_MAX = 999;
		static const u32 INVENTORY_QUICK_ACCESS_MIN = 1000;
		static const u32 INVENTORY_QUICK_ACCESS_MAX = 1999;
		static const u32 INVENTORY_WEARABLES_MIN = 2000;
		static const u32 INVENTORY_WEARABLES_MAX = 2999;
		static const u32 INVENTORY_MINER_MIN = 3000;
		static const u32 INVENTORY_MINER_MAX = 3999;

		const real m_TurnToFaceDownTrackInvSpeed = 1.0f / 0.1f;

		PlayerController* m_Controller = nullptr;
		i32 m_Index = 0;

		// TODO: Store IDs rather than raw pointer

		real m_MoveFriction = 12.0f;
		real m_Height = 4.0f;

		real m_Pitch = 0.0f;
		// Yaw of the view relative to the body while riding a track
		real m_RidingLookYaw = 0.0f;

		TrackBuildingContext m_TrackBuildingContext;

		bool m_bGrounded = false;
		bool m_bPossessed = false;

		TrackID m_TrackRidingID = InvalidTrackID;
		real m_DistAlongTrack = 0.0f;
		real m_TrackMoveSpeed = 0.20f;
		real m_pDTrackMovement = 0.0f;

		real m_TrackAttachMinDist = 4.0f;

		real m_ItemPickupRadius = 4.0f;
		real m_ItemDropPosForwardOffset = 1.5f;
		real m_ItemDropForwardVelocity = 25.0f;

		TrackState m_TrackState;

		std::array<GameObjectStack, INVENTORY_ITEM_COUNT> m_Inventory;
		std::array<GameObjectStack, QUICK_ACCESS_ITEM_COUNT> m_QuickAccessInventory;
		std::array<GameObjectStack, WEARABLES_ITEM_COUNT> m_WearablesInventory;
		bool m_bInventoryShowing = false;
		bool m_bMinerInventoryShowing = false;
		i32 m_SelectedQuickAccessItemSlot = 0;

		// The itemized item the player can interact using (based on selected quick access item slot)
		// One proxy per prefab seen, only the active one is visible
		std::vector<ActiveItem> m_ActiveItemCache;
		i32 m_ActiveItemIndex = -1;

		bool m_bPreviewPlaceItemFromInventory = false;

		real m_ItemPickupMaxDist = 100.0f;
		GameObject* m_ItemPickingUp = nullptr;
		const real m_ItemPickingDuration = 0.5f;
		real m_ItemPickingTimer = -1.0f;

		// TODO: Remove? References objects the player is holding onto (but are not itemized)
		GameObjectID m_HeldItemLeftHand = InvalidGameObjectID;
		GameObjectID m_HeldItemRightHand = InvalidGameObjectID;

		GameObjectID m_RidingVehicleID = InvalidGameObjectID;
		// TODO: Merge these two?
		GameObjectID m_TerminalInteractingWithID = InvalidGameObjectID;
		GameObjectID m_ObjectInteractingWithID = InvalidGameObjectID;

		AudioSourceID m_SoundPlaceTrackNodeID = InvalidAudioSourceID;
		AudioSourceID m_SoundPlaceFinalTrackNodeID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackAttachID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackDetachID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackSwitchDirID = InvalidAudioSourceID;

		TextureID m_CrosshairTextureID = InvalidTextureID;
	};
} // namespace flex
