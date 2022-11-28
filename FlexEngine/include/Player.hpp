#pragma once

#include "Scene/GameObject.hpp"

#include "ResourceManager.hpp"
#include "Types.hpp" // For TrackState
#include "Track/BezierCurve3D.hpp"
#include "Track/BezierCurveList.hpp"

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

		void Reset();

		glm::vec3 GetLookDirection();

		i32 GetIndex() const;
		real GetHeight() const;
		PlayerController* GetController();

		real GetDistAlongTrack() const;

		void UpdateIsPossessed();

		void ClampPitch();
		void UpdateIsGrounded();

		glm::vec3 GetTrackPlacementReticlePosWS(real snapThreshold = -1.0f, bool bSnapToHandles = false);

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

		u32 MoveToInventory(GameObjectStack* inventory, u32 inventorySize, const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData)
		{
			i32 maxStackSize = g_ResourceManager->GetMaxStackSize(prefabID);

			// Fill up any existing slots
			for (u32 i = 0; i < inventorySize; ++i)
			{
				if (inventory[i].prefabID == prefabID && (inventory[i].count + 1) <= maxStackSize)
				{
					i32 deposit = glm::min((maxStackSize - inventory[i].count), count);
					count -= deposit;
					inventory[i].count += deposit;
					// TODO: Merge user data here
				}

				if (count == 0)
				{
					return 0;
				}
			}

			// Fill empty slots
			for (u32 i = 0; i < inventorySize; ++i)
			{
				if (inventory[i].count == 0)
				{
					i32 deposit = glm::min(maxStackSize, count);
					count -= deposit;
					inventory[i].prefabID = prefabID;
					inventory[i].count = deposit;
					inventory[i].userData = userData;
				}

				if (count == 0)
				{
					return 0;
				}
			}

			return count;
		}

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

		bool IsInventoryShowing() const;

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

		real m_ItemPickupRadius = 4.0f;
		real m_ItemDropPosForwardOffset = 1.5f;
		real m_ItemDropForwardVelocity = 25.0f;

		TrackState m_TrackState;

		static const glm::vec3 HeadlampMountingPos;

		static const u32 WEARABLES_ITEM_COUNT = 3;
		static const u32 QUICK_ACCESS_ITEM_COUNT = 11;
		static const u32 INVENTORY_ITEM_ROW_COUNT = 5;
		static const u32 INVENTORY_ITEM_COL_COUNT = 7;
		static const u32 INVENTORY_ITEM_COUNT = INVENTORY_ITEM_ROW_COUNT * INVENTORY_ITEM_COL_COUNT;

		static const u32 INVENTORY_MIN = 0;
		static const u32 INVENTORY_MAX = 999;
		static const u32 INVENTORY_QUICK_ACCESS_MIN = 1000;
		static const u32 INVENTORY_QUICK_ACCESS_MAX = 1999;
		static const u32 INVENTORY_WEARABLES_MIN = 2000;
		static const u32 INVENTORY_WEARABLES_MAX = 2999;
		static const u32 INVENTORY_MINER_MIN = 3000;
		static const u32 INVENTORY_MINER_MAX = 3999;

		std::array<GameObjectStack, INVENTORY_ITEM_COUNT> m_Inventory;
		std::array<GameObjectStack, QUICK_ACCESS_ITEM_COUNT> m_QuickAccessInventory;
		std::array<GameObjectStack, WEARABLES_ITEM_COUNT> m_WearablesInventory;
		bool bInventoryShowing = false;
		bool bMinerInventoryShowing = false;
		i32 selectedQuickAccessItemSlot = 0;

		GameObjectID heldItemLeftHand = InvalidGameObjectID;
		GameObjectID heldItemRightHand = InvalidGameObjectID;

		GameObjectID ridingVehicleID = InvalidGameObjectID;
		// TODO: Merge these two?
		GameObjectID terminalInteractingWithID = InvalidGameObjectID;
		GameObjectID objectInteractingWithID = InvalidGameObjectID;

		const real m_TurnToFaceDownTrackInvSpeed = 25.0f;
		const real m_FlipTrackDirInvSpeed = 45.0f;

	private:
		friend class PlayerController;

		void CreateDroppedItemFromStack(GameObjectStack* stack);

		AudioSourceID m_SoundPlaceTrackNodeID = InvalidAudioSourceID;
		AudioSourceID m_SoundPlaceFinalTrackNodeID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackAttachID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackDetachID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackSwitchDirID = InvalidAudioSourceID;

		TextureID m_CrosshairTextureID = InvalidTextureID;
	};
} // namespace flex
