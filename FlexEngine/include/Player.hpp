#pragma once

#include "Scene/GameObject.hpp"

#include "ResourceManager.hpp"
#include "Types.hpp" // For TrackState
#include "Track/BezierCurve3D.hpp"
#include "Track/BezierCurveList.hpp"
#include "Systems/TrackManager.hpp" // For TrackPointRef
#include "Scene/Objects/Building.hpp" // For BuildMode

namespace flex
{
	class PlayerController;
	class Traversal;
	class WoodcraftController;

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
		real GetRadius() const { return m_Radius; }
		// Where the first person camera sits (a little below the top of the capsule)
		glm::vec3 GetEyePosition() const;

		// Contacts with surfaces flatter than this (normal y) can be walked on, steeper ones are walls
		static constexpr real WALKABLE_NORMAL_Y = 0.6f;
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
		// Replaces the inventory's contents
		void ParseInventory(const JSONObject& inventoryObj);
		JSONObject SerializeInventoryToJSON();
		// Empties every inventory (unequipping anything worn)
		void ResetInventory();

		void SetInteractingWithTerminal(Terminal* terminal);
		// Any rideable object (vehicle or boat, see Rideable), nullptr to get out
		void SetRidingVehicle(GameObject* vehicle);

		bool PickupWithFreeHand(GameObject* object);
		bool IsHolding(GameObject* object);
		void DropIfHolding(GameObject* object);
		bool HasFreeHand() const;

		void OnWearableEquipped(GameObjectStack const* wearableStack);
		void OnWearableUnequipped(GameObjectStack const* wearableStack);

		bool IsAnyInventoryShowing() const;
		bool IsInventoryShowing() const;
		void SetInventoryShowing(bool bShowing) { m_bInventoryShowing = bShowing; }
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

		// Digging & filling terrain with a pickaxe
		void UpdateTerrainTool();
		// Keeps the player on top of streamed terrain: places them on the ground when spawning (holding them in place
		// until collision has loaded below), & moves them back to the surface if they end up inside the terrain
		void UpdateTerrainSafety();
		void PlaceOnTerrain(bool bKeepHeight);
		// Holds the player at holdPos until there's terrain collision below it
		void HoldUntilTerrainCollision(const glm::vec3& holdPos);
		// Whether there's something to stand on just below a capsule centered at pos
		bool IsSupportedBelow(const glm::vec3& pos) const;
		void DrawTerrainToolHUD() const;
		void DrawTraversalHUD() const;

		Traversal* GetTraversal() { return m_Traversal; }
		WoodcraftController* GetWoodcraft() { return m_Woodcraft; }
		BuildMode& GetBuildMode() { return m_BuildMode; }
		bool IsBuilding() const { return m_BuildMode.IsActive(); }
		void DrawBuildModeHUD() const { m_BuildMode.DrawHUD(); }
		// Briefly shows a message in the middle of the screen
		void ShowNotification(const std::string& text);
		// Notifications & prompts for nearby things that can be interacted with
		void DrawNotifications() const;
		bool IsGrounded() const { return m_bGrounded; }
		const glm::vec3& GetGroundNormal() const { return m_GroundNormal; }
		// Moves the player to the ground near pos (in streamed terrain), waiting there until collision has loaded.
		// bKeepExactPosition (for restoring saved positions) stays at pos if it's clear of the terrain (e.g. when
		// standing on something built) rather than dropping to the ground below.
		void TeleportOnTerrain(const glm::vec3& pos, bool bKeepExactPosition = false);
		// Whether the player is being held in place until terrain collision loads below them
		bool IsAwaitingTerrainCollision() const { return m_bAwaitingTerrainCollision || m_bTerrainPlacementPending; }
		// Called once the player's render pose (interpolated between physics steps) has been applied
		void OnRenderPoseUpdated();

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
		friend class Traversal;
		friend class WoodcraftController;

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
		Traversal* m_Traversal = nullptr;
		WoodcraftController* m_Woodcraft = nullptr;
		BuildMode m_BuildMode;

		struct Notification
		{
			std::string text;
			sec timeRemaining;
		};
		std::vector<Notification> m_Notifications;
		i32 m_Index = 0;

		// TODO: Store IDs rather than raw pointer

		real m_MoveFriction = 12.0f;
		real m_Height = 2.0f; // Total capsule height (meters)
		real m_Radius = 0.4f;

		real m_Pitch = 0.0f;
		// Yaw of the view relative to the body while riding a track
		real m_RidingLookYaw = 0.0f;

		TrackBuildingContext m_TrackBuildingContext;

		bool m_bGrounded = false;
		// Result of the last ground probe (which reaches GROUND_PROBE_DISTANCE below the capsule)
		bool m_bGroundHit = false;
		real m_GroundGap = 0.0f;
		glm::vec3 m_GroundNormal = VEC3_UP;
		static constexpr real GROUND_PROBE_DISTANCE = 0.7f;
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

		bool m_bTerrainToolActive = false; // Whether the player is pointing a pickaxe at terrain within reach
		sec m_TerrainToolCooldown = 0.0f;
		i32 m_TerrainFillMaterialIndex = 0;
		real m_TerrainToolReach = 8.0f;
		real m_TerrainDigRadius = 1.6f;

		bool m_bTerrainPlacementPending = true;
		bool m_bTerrainPlacementExact = false;
		bool m_bAwaitingTerrainCollision = false;
		glm::vec3 m_TerrainHoldPos = VEC3_ZERO;
		sec m_AwaitingTerrainCollisionTime = 0.0f;
		sec m_InsideTerrainTime = 0.0f;
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
