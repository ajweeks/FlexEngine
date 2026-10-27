#include "stdafx.hpp"

#include "Player.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btManifoldResult.h>

#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include <queue>

#include "Inventory.hpp"
#include "JSONParser.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/Industry.hpp"
#include "Scene/Objects/Props.hpp"
#include "Scene/SceneManager.hpp"

// Player inventories: quick access, main, wearables & miner inventories, and moving items between them
namespace flex
{
	void Player::DropSelectedItem()
	{
		DropItemStack(GetGameObjectStackIDForQuickAccessInventory(m_SelectedQuickAccessItemSlot), false);
	}

	bool Player::HasFullSelectedInventorySlot()
	{
		return m_QuickAccessInventory[m_SelectedQuickAccessItemSlot].count > 0;
	}

	i32 Player::GetNextFreeInventorySlot()
	{
		for (i32 i = 0; i < (i32)m_Inventory.size(); ++i)
		{
			if (m_Inventory[i].count == 0)
			{
				return i;
			}
		}

		return -1;
	}

	i32 Player::GetNextFreeQuickAccessInventorySlot()
	{
		for (i32 i = 0; i < (i32)m_QuickAccessInventory.size(); ++i)
		{
			if (m_QuickAccessInventory[i].count == 0)
			{
				return i;
			}
		}

		return -1;
	}

	i32 Player::GetNextFreeMinerInventorySlot()
	{
		if (m_NearbyInteractableID.IsValid() && m_NearbyInteractableID.Get()->GetTypeID() == MinerSID)
		{
			Miner* miner = (Miner*)m_NearbyInteractableID.Get();
			return miner->GetNextFreeInventorySlot();
		}
		else
		{
			PrintWarn("Attempted to get free inventory slot from miner inventory when not being interacted with\n");
			return -1;
		}
	}

	GameObjectStack* Player::GetGameObjectStackFromInventory(GameObjectStackID stackID, InventoryType& outInventoryType)
	{
		if ((i32)stackID < INVENTORY_MAX)
		{
			outInventoryType = InventoryType::PLAYER_INVENTORY;
			return &m_Inventory[(i32)stackID - INVENTORY_MIN];
		}
		if ((i32)stackID < INVENTORY_QUICK_ACCESS_MAX)
		{
			outInventoryType = InventoryType::QUICK_ACCESS;
			return &m_QuickAccessInventory[(i32)stackID - INVENTORY_QUICK_ACCESS_MIN];
		}
		if ((i32)stackID < INVENTORY_WEARABLES_MAX)
		{
			outInventoryType = InventoryType::WEARABLES;
			return &m_WearablesInventory[(i32)stackID - INVENTORY_WEARABLES_MIN];
		}
		if ((i32)stackID < INVENTORY_MINER_MAX)
		{
			if (m_NearbyInteractableID.IsValid() && m_NearbyInteractableID.Get()->GetTypeID() == MinerSID)
			{
				Miner* miner = (Miner*)m_NearbyInteractableID.Get();
				outInventoryType = InventoryType::MINER_INVENTORY;
				return miner->GetStackFromInventory((i32)stackID - INVENTORY_MINER_MIN);
			}
			else
			{
				PrintWarn("Attempted to get game object from miner inventory when not being interacted with\n");
				outInventoryType = InventoryType::NONE;
				return nullptr;
			}
		}

		outInventoryType = InventoryType::NONE;
		PrintWarn("Attempted to get item from inventory with invalid stackID: %d!\n", (i32)stackID);
		return nullptr;
	}

	bool Player::MoveItemStack(GameObjectStackID fromID, GameObjectStackID toID)
	{
		InventoryType fromInventoryType, toInventoryType;
		GameObjectStack* fromStack = GetGameObjectStackFromInventory(fromID, fromInventoryType);
		GameObjectStack* toStack = GetGameObjectStackFromInventory(toID, toInventoryType);

		if (fromStack != nullptr && toStack != nullptr && fromStack != toStack)
		{
			bool bFromWearables = fromInventoryType == InventoryType::WEARABLES;
			bool bToWearables = toInventoryType == InventoryType::WEARABLES;

			if (bToWearables)
			{
				GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(fromStack->prefabID);
				if (prefabTemplate->IsWearable())
				{
					if (toStack->count == 0 && fromStack->count == 1)
					{
						toStack->prefabID = fromStack->prefabID;
						toStack->count = fromStack->count;
						toStack->userData = fromStack->userData;
						fromStack->Clear();

						if (!bFromWearables)
						{
							// Only needed if the item wasn't being worn already
							OnWearableEquipped(toStack);
						}

						return true;
					}
				}
			}
			else
			{
				if (toStack->count == 0)
				{
					if (bFromWearables)
					{
						OnWearableUnequipped(fromStack);
					}

					toStack->prefabID = fromStack->prefabID;
					toStack->count = fromStack->count;
					toStack->userData = fromStack->userData;
					fromStack->Clear();
					return true;
				}
				else if (toStack->prefabID == fromStack->prefabID &&
					(u32)(toStack->count + fromStack->count) <= g_ResourceManager->GetMaxStackSize(toStack->prefabID))
				{
					if (bFromWearables)
					{
						OnWearableUnequipped(fromStack);
					}

					toStack->count = toStack->count + fromStack->count;
					fromStack->Clear();
					return true;
				}
			}
		}

		return false;
	}

	bool Player::MoveSingleItemFromStack(GameObjectStackID fromID, GameObjectStackID toID)
	{
		InventoryType fromInventoryType, toInventoryType;
		GameObjectStack* fromStack = GetGameObjectStackFromInventory(fromID, fromInventoryType);
		GameObjectStack* toStack = GetGameObjectStackFromInventory(toID, toInventoryType);

		if (fromStack != nullptr && toStack != nullptr && fromStack != toStack)
		{
			bool bFromWearables = fromInventoryType == InventoryType::WEARABLES;
			bool bToWearables = toInventoryType == InventoryType::WEARABLES;

			if (bToWearables)
			{
				GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(fromStack->prefabID);
				if (prefabTemplate->IsWearable())
				{
					if (toStack->count == 0 && fromStack->count >= 1)
					{
						toStack->prefabID = fromStack->prefabID;
						toStack->count = 1;
						toStack->userData = fromStack->userData;
						--fromStack->count;
						if (fromStack->count == 0)
						{
							fromStack->Clear();
						}

						if (!bFromWearables)
						{
							// Only needed if the item wasn't being worn already
							OnWearableEquipped(toStack);
						}

						return true;
					}
				}
			}
			else
			{
				if (toStack->count == 0)
				{
					if (bFromWearables)
					{
						OnWearableUnequipped(fromStack);
					}

					toStack->prefabID = fromStack->prefabID;
					toStack->count = 1;
					toStack->userData = fromStack->userData;
					--fromStack->count;
					if (fromStack->count == 0)
					{
						fromStack->Clear();
					}
					return true;
				}
				else if (toStack->prefabID == fromStack->prefabID &&
					(u32)(toStack->count + 1) <= g_ResourceManager->GetMaxStackSize(toStack->prefabID))
				{
					if (bFromWearables)
					{
						OnWearableUnequipped(fromStack);
					}

					++toStack->count;
					--fromStack->count;
					if (fromStack->count == 0)
					{
						fromStack->Clear();
					}
					return true;
				}
			}
		}

		return false;
	}

	bool Player::DropItemStack(GameObjectStackID stackID, bool bDestroyItem)
	{
		InventoryType inventoryType;
		GameObjectStack* stack = GetGameObjectStackFromInventory(stackID, inventoryType);

		if (stack != nullptr)
		{
			bool bFromWearables = inventoryType == InventoryType::WEARABLES;

			if (bFromWearables)
			{
				OnWearableUnequipped(stack);
			}

			if (!bDestroyItem)
			{
				CreateDroppedItemFromStack(stack);
			}

			stack->Clear();
			return true;
		}

		return false;
	}

	bool Player::DropSingleItemFromStack(GameObjectStackID stackID, bool bDestroyItem)
	{
		InventoryType inventoryType;
		GameObjectStack* stack = GetGameObjectStackFromInventory(stackID, inventoryType);

		if (stack != nullptr && stack->count > 0)
		{
			bool bFromWearables = inventoryType == InventoryType::WEARABLES;

			if (bFromWearables)
			{
				OnWearableUnequipped(stack);
			}

			if (!bDestroyItem)
			{
				CreateDroppedItemFromStack(stack);
			}

			--stack->count;
			if (stack->count == 0)
			{
				stack->Clear();
			}
			return true;
		}

		return false;
	}

	i32 Player::MoveStackBetweenInventories(GameObjectStackID stackID, InventoryType destInventoryType, i32 countToMove)
	{
		InventoryType sourceInventoryType;
		GameObjectStack* sourceStack = GetGameObjectStackFromInventory(stackID, sourceInventoryType);
		if (sourceInventoryType != destInventoryType && sourceStack != nullptr)
		{
			if (countToMove == -1)
			{
				countToMove = sourceStack->count;
			}
			u32 itemsNotMoved = AddToInventory(sourceStack->prefabID, countToMove, sourceStack->userData, destInventoryType);
			if (itemsNotMoved == 0)
			{
				sourceStack->count -= countToMove;
				if (sourceStack->count == 0)
				{
					sourceStack->Clear();
				}
				return 0;
			}
			else
			{
				sourceStack->count = itemsNotMoved;
				return itemsNotMoved;
			}
		}

		return countToMove;
	}

	void Player::CreateDroppedItemFromStack(GameObjectStack* stack)
	{
		glm::vec3 lookDir = GetLookDirection();
		glm::vec3 dropPos = m_Transform.GetWorldPosition() + lookDir * m_ItemDropPosForwardOffset;
		g_SceneManager->CurrentScene()->CreateDroppedItem(
			stack->prefabID,
			stack->count,
			dropPos,
			lookDir * m_ItemDropForwardVelocity);
	}

	GameObjectStackID Player::GetGameObjectStackIDForInventory(u32 slotIndex)
	{
		if (slotIndex <= INVENTORY_ITEM_COUNT)
		{
			return (GameObjectStackID)(slotIndex + INVENTORY_MIN);
		}
		return InvalidID;
	}

	GameObjectStackID Player::GetGameObjectStackIDForQuickAccessInventory(u32 slotIndex)
	{
		if (slotIndex <= QUICK_ACCESS_ITEM_COUNT)
		{
			return (GameObjectStackID)slotIndex + INVENTORY_QUICK_ACCESS_MIN;
		}
		return InvalidID;
	}

	GameObjectStackID Player::GetGameObjectStackIDForWearablesInventory(u32 slotIndex)
	{
		if (slotIndex <= WEARABLES_ITEM_COUNT)
		{
			return (GameObjectStackID)(slotIndex + INVENTORY_WEARABLES_MIN);
		}
		return InvalidID;
	}

	GameObjectStackID Player::GetGameObjectStackIDForMinerInventory(u32 slotIndex)
	{
		if (slotIndex <= Miner::INVENTORY_SIZE)
		{
			return (GameObjectStackID)(slotIndex + INVENTORY_MINER_MIN);
		}
		return InvalidID;
	}

	void Player::AddToInventory(DroppedItem* droppedItem)
	{
		GameObjectStack::UserData userData = {};
		AddToInventory(droppedItem->prefabID, droppedItem->stackSize, userData);

		droppedItem->OnPickedUp();

		g_SceneManager->CurrentScene()->RemoveObject(droppedItem, true);
	}

	void Player::AddToInventory(const PrefabID& prefabID, i32 count)
	{
		AddToInventory(prefabID, count, {});
	}

	void Player::AddToInventory(const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData)
	{
		i32 maxStackSize = g_ResourceManager->GetMaxStackSize(prefabID);

		// Fill up any existing slots in quick access
		for (GameObjectStack& gameObjectStack : m_QuickAccessInventory)
		{
			if (gameObjectStack.prefabID == prefabID && (gameObjectStack.count + 1) <= maxStackSize)
			{
				i32 deposit = glm::min(((i32)maxStackSize - gameObjectStack.count), count);
				count -= deposit;
				gameObjectStack.count += deposit;
				// TODO: Merge user data here
			}

			if (count == 0)
			{
				return;
			}
		}

		// Fill up any existing slots in main inventory
		for (GameObjectStack& gameObjectStack : m_Inventory)
		{
			if (gameObjectStack.prefabID == prefabID && (gameObjectStack.count + 1) <= maxStackSize)
			{
				i32 deposit = glm::min(((i32)maxStackSize - gameObjectStack.count), count);
				count -= deposit;
				gameObjectStack.count += deposit;
				// TODO: Merge user data here
			}

			if (count == 0)
			{
				return;
			}
		}

		// Fill empty slots in quick access
		for (GameObjectStack& gameObjectStack : m_QuickAccessInventory)
		{
			if (gameObjectStack.count == 0)
			{
				i32 deposit = glm::min((i32)maxStackSize, count);
				count -= deposit;
				gameObjectStack.prefabID = prefabID;
				gameObjectStack.count = deposit;
				gameObjectStack.userData = userData;
			}

			if (count == 0)
			{
				return;
			}
		}

		// Fill empty slots in main inventory
		for (GameObjectStack& gameObjectStack : m_Inventory)
		{
			if (gameObjectStack.count == 0)
			{
				i32 deposit = glm::min((i32)maxStackSize, count);
				count -= deposit;
				gameObjectStack.prefabID = prefabID;
				gameObjectStack.count = deposit;
				gameObjectStack.userData = userData;
			}

			if (count == 0)
			{
				return;
			}
		}
	}

	u32 Player::AddToInventory(const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData, InventoryType inventoryType)
	{
		switch (inventoryType)
		{
		case InventoryType::QUICK_ACCESS:
			return MoveToInventory(&m_QuickAccessInventory[0], (u32)m_QuickAccessInventory.size(), prefabID, count, userData);
		case InventoryType::PLAYER_INVENTORY:
			return MoveToInventory(&m_Inventory[0], (u32)m_Inventory.size(), prefabID, count, userData);
		case InventoryType::WEARABLES:
			return MoveToInventory(&m_WearablesInventory[0], (u32)m_WearablesInventory.size(), prefabID, count, userData);
		case InventoryType::MINER_INVENTORY:
		{
			if (m_NearbyInteractableID.IsValid())
			{
				GameObject* nearbyInteractable = m_NearbyInteractableID.Get();
				if (nearbyInteractable->GetTypeID() == MinerSID)
				{
					Miner* miner = (Miner*)nearbyInteractable;
					return miner->AddToInventory(prefabID, count, userData);
				}
			}
		} break;
		}

		return count;
	}

	u32 Player::MoveToInventory(GameObjectStack* inventory, u32 inventorySize, const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData)
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

	void Player::ClearInventory()
	{
		m_Inventory.fill({});
		m_QuickAccessInventory.fill({});
		m_WearablesInventory.fill({});
	}

	void Player::ParseInventoryFile()
	{
		if (FileExists(USER_INVENTORY_LOCATION))
		{
			std::string fileContents;
			if (ReadFile(USER_INVENTORY_LOCATION, fileContents, false))
			{
				JSONObject inventoryObj;
				if (JSONParser::Parse(fileContents, inventoryObj))
				{
					ParseInventory(inventoryObj);
				}
				else
				{
					PrintError("Failed to parse user inventory file, error: %s\n", JSONParser::GetErrorString());
				}
			}
			else
			{
				PrintError("Failed to read use inventory file at %s\n", USER_INVENTORY_LOCATION);
			}
		}
		// TODO: Serialize parent & wire reference once ObjectIDs are in
	}

	void Player::ParseInventory(const JSONObject& inventoryObj)
	{
		ResetInventory();

		std::vector<JSONObject> slotLists[3];

		inventoryObj.TryGetObjectArray("slots", slotLists[0]);
		inventoryObj.TryGetObjectArray("quick access slots", slotLists[1]);
		inventoryObj.TryGetObjectArray("wearable slots", slotLists[2]);

		for (i32 slotListIndex = 0; slotListIndex < ARRAY_LENGTH(slotLists); ++slotListIndex)
		{
			for (JSONObject& slot : slotLists[slotListIndex])
			{
				i32 index = slot.GetInt("index");
				i32 count = slot.GetInt("count");
				PrefabID prefabID = slot.GetPrefabID("prefab id");
				i32 maxStackSize = (i32)g_ResourceManager->GetMaxStackSize(prefabID);

				bool bIndexValid = index >= 0 && index < INVENTORY_ITEM_COUNT;
				bool bCountValid = count >= 0 && count <= maxStackSize;
				bool bPrefabIDValid = g_ResourceManager->IsPrefabIDValid(prefabID);

				if (bIndexValid && bCountValid && bPrefabIDValid)
				{
					if (slotListIndex == 0)
					{
						m_Inventory[index] = GameObjectStack(prefabID, count);
					}
					else if (slotListIndex == 1)
					{
						m_QuickAccessInventory[index] = GameObjectStack(prefabID, count);
					}
					else if (slotListIndex == 2)
					{
						m_WearablesInventory[index] = GameObjectStack(prefabID, count);
					}
				}
				else
				{
					if (!bIndexValid)
					{
						PrintError("Invalid user inventory index: %i\n", index);
					}
					if (!bCountValid)
					{
						PrintError("Invalid user inventory count: %i\n", count);
					}
					if (!bPrefabIDValid)
					{
						std::string prefabIDStr = prefabID.ToString();
						PrintError("Invalid user inventory prefabID: %s\n", prefabIDStr.c_str());
					}
				}
			}
		}

		for (const GameObjectStack& stack : m_WearablesInventory)
		{
			if (stack.count > 0)
			{
				OnWearableEquipped(&stack);
			}
		}
	}

	JSONObject Player::SerializeInventoryToJSON()
	{
		JSONObject inventoryObj = {};

		auto SerializeSlot = [](i32 slotIdx, GameObjectStack& stack)
		{
			JSONObject slot = {};
			slot.fields.emplace_back("index", JSONValue(slotIdx));
			slot.fields.emplace_back("count", JSONValue(stack.count));
			slot.fields.emplace_back("prefab id", JSONValue(stack.prefabID));

			return slot;
		};

		std::vector<JSONObject> slots;
		if (SerializeInventory((GameObjectStack*)&m_Inventory[0], (u32)m_Inventory.size(), slots))
		{
			inventoryObj.fields.emplace_back("slots", JSONValue(slots));
		}

		std::vector<JSONObject> quickAccessSlots;
		if (SerializeInventory((GameObjectStack*)&m_QuickAccessInventory[0], (u32)m_QuickAccessInventory.size(), quickAccessSlots))
		{
			inventoryObj.fields.emplace_back("quick access slots", JSONValue(quickAccessSlots));
		}

		std::vector<JSONObject> wearablesSlots;
		if (SerializeInventory((GameObjectStack*)&m_WearablesInventory[0], (u32)m_WearablesInventory.size(), wearablesSlots))
		{
			inventoryObj.fields.emplace_back("wearable slots", JSONValue(wearablesSlots));
		}

		return inventoryObj;
	}

	void Player::ResetInventory()
	{
		// Remove anything being worn (e.g. headlamps attached to the player)
		for (const GameObjectStack& stack : m_WearablesInventory)
		{
			if (stack.count > 0)
			{
				OnWearableUnequipped(&stack);
			}
		}
		ClearInventory();
	}

	void Player::AddStarterKit()
	{
		for (const char* toolName : { "pickaxe", "axe", "shovel" })
		{
			const PrefabID toolID = g_ResourceManager->GetPrefabID(toolName);
			if (toolID.IsValid())
			{
				AddToInventory(toolID, 1);
			}
		}
	}

	void Player::SerializeInventoryToFile()
	{
		JSONObject inventoryObj = SerializeInventoryToJSON();

		Platform::CreateDirectoryRecursive(RelativePathToAbsolute(SAVE_FILE_DIRECTORY));

		std::string fileContents = inventoryObj.ToString();
		if (WriteFile(USER_INVENTORY_LOCATION, fileContents, false))
		{
			Print("Saved user inventory to disk.\n");
		}
		else
		{
			PrintError("Failed to write user inventory to file at %s\n", USER_INVENTORY_LOCATION);
		}
	}

	void Player::OnWearableEquipped(GameObjectStack const* wearableStack)
	{
		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(wearableStack->prefabID);
		CHECK(prefabTemplate->IsWearable());

		switch (prefabTemplate->GetTypeID())
		{
		case HeadLampSID:
		{
			GameObject* headLamp = prefabTemplate->CopySelf(this, GameObject::ALL);
			headLamp->GetTransform()->SetLocalPosition(HEADLAMP_MOUNT_POS);
		} break;
		default:
		{
			PrintError("Unhandled wearable equipped\n");
		} break;
		}
	}

	void Player::OnWearableUnequipped(GameObjectStack const* wearableStack)
	{
		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(wearableStack->prefabID);
		CHECK(prefabTemplate->IsWearable());

		switch (prefabTemplate->GetTypeID())
		{
		case HeadLampSID:
		{
			std::vector<HeadLamp*> headlamps;
			GetChildrenOfType<HeadLamp>(HeadLampSID, true, headlamps);
			if (headlamps.size() == 1)
			{
				if (!RemoveChildImmediate(headlamps[0]->ID, true))
				{
					PrintError("Failed to remove headlamp from player\n");
				}
			}
			else
			{
				PrintError("Failed to find headlamp child in player\n");
			}
		} break;
		default:
		{
			PrintError("Unhandled wearable unequipped\n");
		} break;
		}
	}

	bool Player::IsAnyInventoryShowing() const
	{
		return m_bInventoryShowing || m_bMinerInventoryShowing;
	}

	bool Player::IsInventoryShowing() const
	{
		return m_bInventoryShowing;
	}

	bool Player::IsMinerInventoryShowing() const
	{
		return m_bMinerInventoryShowing;
	}

	void Player::SetSelectedQuickAccessItemSlot(i32 selectedQuickAccessItemSlot)
	{
		if (m_SelectedQuickAccessItemSlot == selectedQuickAccessItemSlot)
		{
			return;
		}

		m_SelectedQuickAccessItemSlot = selectedQuickAccessItemSlot;
	}
} // namespace flex
