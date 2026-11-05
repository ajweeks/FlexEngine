#include "stdafx.hpp"

#include "Player.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btManifoldResult.h>

#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include <queue>

#include "Crafting.hpp"
#include "Inventory.hpp"
#include "JSONParser.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/Animals.hpp" // For Animals::IsNamingDialogOpen
#include "Scene/Objects/Industry.hpp"
#include "Scene/Objects/Lights.hpp" // For FireFlicker & SpotLight
#include "Graphics/Renderer.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Platform/Platform.hpp"
#include "InputManager.hpp"
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
				// Only the items which were actually moved leave the source stack
				sourceStack->count -= (countToMove - (i32)itemsNotMoved);
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
		const i32 remainder = (i32)AddToInventory(droppedItem->prefabID, droppedItem->stackSize, userData);
		if (remainder == droppedItem->stackSize)
		{
			// Nothing fit (inventory full), leave the item lying there rather than destroying it
			return;
		}

		droppedItem->OnPickedUp();

		if (remainder > 0)
		{
			// Partial pickup, the rest stays on the ground
			droppedItem->stackSize = remainder;
			return;
		}

		g_SceneManager->CurrentScene()->RemoveObject(droppedItem, true);
	}

	u32 Player::AddToInventory(const PrefabID& prefabID, i32 count)
	{
		return AddToInventory(prefabID, count, {});
	}

	void Player::AddToInventoryOrDrop(const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData, const glm::vec3& dropPosWS)
	{
		const i32 remainder = (i32)AddToInventory(prefabID, count, userData);
		if (remainder > 0)
		{
			// No room left: drop the rest rather than losing it
			g_SceneManager->CurrentScene()->CreateDroppedItem(prefabID, remainder, dropPosWS, VEC3_ZERO);
		}
	}

	u32 Player::AddToInventory(const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData)
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
				return 0;
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
				return 0;
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
				return 0;
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
				return 0;
			}
		}
		return (u32)count;
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

		if (m_Crafting != nullptr)
		{
			m_Crafting->Parse(inventoryObj);
		}

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

				const i32 slotCount = (slotListIndex == 0) ? (i32)m_Inventory.size() : (slotListIndex == 1) ? (i32)m_QuickAccessInventory.size() : (i32)m_WearablesInventory.size();
				bool bIndexValid = index >= 0 && index < slotCount;
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

		if (m_Crafting != nullptr)
		{
			m_Crafting->Serialize(inventoryObj);
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
		if (m_Crafting != nullptr)
		{
			m_Crafting->Reset();
		}
	}

	void Player::Die(const glm::vec3& deathPos)
	{
		BaseScene* scene = g_SceneManager->CurrentScene();
		if (m_RidingVehicleID.IsValid())
		{
			SetRidingVehicle(nullptr);
		}
		u32 stackIndex = 0;
		auto Drop = [&](GameObjectStack& stack)
		{
			if (stack.count > 0 && stack.prefabID.IsValid() && scene != nullptr)
			{
				// Spread out in a ring so the stacks don't sit on top of each other
				const real angle = (real)stackIndex++ * 2.39996f;
				const real radius = 0.4f + 0.12f * glm::sqrt((real)stackIndex);
				const glm::vec3 pos = deathPos + glm::vec3(std::cos(angle) * radius, 1.0f, std::sin(angle) * radius);
				if (DroppedItem* item = scene->CreateDroppedItem(stack.prefabID, stack.count, pos, VEC3_ZERO))
				{
					item->pickupDelay = 4.0f;
					item->bSaved = true;
				}
			}
		};
		for (GameObjectStack& stack : m_WearablesInventory)
		{
			if (stack.count > 0)
			{
				OnWearableUnequipped(&stack);
			}
			Drop(stack);
		}
		for (GameObjectStack& stack : m_QuickAccessInventory)
		{
			Drop(stack);
		}
		for (GameObjectStack& stack : m_Inventory)
		{
			Drop(stack);
		}
		ClearInventory();
		Print("Player died at (%.1f, %.1f, %.1f), dropped %u stacks\n", deathPos.x, deathPos.y, deathPos.z, stackIndex);
		Reset();
	}

	void Player::AddStarterKit()
	{
		for (const char* toolName : { "pickaxe", "axe", "shovel", "bed", "lasso", "torch" })
		{
			const PrefabID toolID = g_ResourceManager->GetPrefabID(toolName);
			if (toolID.IsValid())
			{
				AddToInventory(toolID, toolName[0] == 't' ? 4 : 1);
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

	void Player::ReleaseHeldTorchLight()
	{
		if (m_HeldTorchLightID != InvalidPointLightID)
		{
			g_Renderer->RemovePointLight(m_HeldTorchLightID);
			m_HeldTorchLightID = InvalidPointLightID;
		}
	}

	void Player::UpdateItemLights()
	{
		// FLEX_TEST_ITEM_LIGHTS=<any>: once playing, at midnight: holds a torch while wearing a headlamp, then only the headlamp,
		// then neither (items_*.png screenshots), then places the bed from the quick access bar with the place button & checks it
		static const bool s_bTest = getenv("FLEX_TEST_ITEM_LIGHTS") != nullptr;
		if (s_bTest && m_bPossessed && m_bGrounded && g_UnpausedDeltaTime > 0.0f)
		{
			static sec s_Time = 0.0f;
			static i32 s_Step = 0;
			static i32 s_BedCount = 0;
			s_Time += g_UnpausedDeltaTime;
			BaseScene* scene = g_SceneManager->CurrentScene();
			auto FindSlot = [this](PrefabID prefabID)
			{
				for (i32 i = 0; i < (i32)m_QuickAccessInventory.size(); ++i)
				{
					if (prefabID.IsValid() ? (m_QuickAccessInventory[i].prefabID == prefabID && m_QuickAccessInventory[i].count > 0) : m_QuickAccessInventory[i].count == 0)
					{
						return i;
					}
				}
				return -1;
			};
			auto Shot = [](const char* name)
			{
				const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
				Platform::CreateDirectoryRecursive(directory);
				g_Renderer->RequestScreenshot(directory + "items_" + name + ".png");
			};
			auto PlaceButton = [](bool bDown)
			{
				const InputBinding& binding = g_InputManager->GetActionBinding(Action::PLACE_ITEM);
				g_InputManager->SetIgnoreWindowFocus(true);
				const bool bBlocked = g_InputManager->IsDeviceInputBlocked();
				g_InputManager->SetDeviceInputBlocked(false);
				if (binding.keyCode != KeyCode::_NONE)
				{
					g_InputManager->KeyCallback(binding.keyCode, bDown ? KeyAction::KEY_PRESS : KeyAction::KEY_RELEASE, 0);
				}
				else
				{
					g_InputManager->MouseButtonCallback(binding.mouseButton, bDown ? KeyAction::KEY_PRESS : KeyAction::KEY_RELEASE, 0);
				}
				g_InputManager->SetDeviceInputBlocked(bBlocked);
			};
			const PrefabID headlampID = g_ResourceManager->GetPrefabID("headlamp");
			if (s_Step == 0 && s_Time > 4.0f)
			{
				scene->SetTimeOfDay(0.5f);
				m_SelectedQuickAccessItemSlot = glm::max(FindSlot(g_ResourceManager->GetPrefabID("torch")), 0);
				if (headlampID.IsValid() && m_WearablesInventory[0].count == 0)
				{
					m_WearablesInventory[0] = GameObjectStack(headlampID, 1);
					OnWearableEquipped(&m_WearablesInventory[0]);
				}
				// (Positive pitch looks down)
				SetPitch(0.2f);
				s_Step = 1;
			}
			else if (s_Step == 1 && s_Time > 7.0f)
			{
				Print("Item lights test: torch light %s, %d spot light(s)\n", m_HeldTorchLightID != InvalidPointLightID ? "on" : "OFF", g_Renderer->GetNumSpotLights());
				Shot("torch_headlamp");
				s_Step = 2;
			}
			else if (s_Step == 2 && s_Time > 8.0f)
			{
				m_SelectedQuickAccessItemSlot = glm::max(FindSlot(InvalidPrefabID), 0);
				s_Step = 3;
			}
			else if (s_Step == 3 && s_Time > 10.0f)
			{
				Shot("headlamp");
				s_Step = 4;
			}
			else if (s_Step == 4 && s_Time > 10.5f)
			{
				if (m_WearablesInventory[0].count > 0)
				{
					OnWearableUnequipped(&m_WearablesInventory[0]);
					m_WearablesInventory[0].Clear();
				}
				s_Step = 5;
			}
			else if (s_Step == 5 && s_Time > 12.0f)
			{
				Shot("dark");
				m_SelectedQuickAccessItemSlot = glm::max(FindSlot(g_ResourceManager->GetPrefabID("bed")), 0);
				SetPitch(0.35f);
				s_BedCount = (i32)scene->GetObjectsOfType<Bed>(BedSID).size();
				s_Step = 6;
			}
			else if (s_Step == 6 && s_Time > 13.0f)
			{
				// Held, as it is before the place button is pressed
				Shot("bed_held");
				PlaceButton(true);
				s_Step = 7;
			}
			else if (s_Step == 7 && s_Time > 14.5f)
			{
				Shot("bed_preview");
				s_Step = 8;
			}
			else if (s_Step == 8 && s_Time > 15.0f)
			{
				PlaceButton(false);
				s_Step = 9;
			}
			else if (s_Step == 9 && s_Time > 16.0f)
			{
				std::vector<Bed*> beds = scene->GetObjectsOfType<Bed>(BedSID);
				const bool bPlaced = (i32)beds.size() == s_BedCount + 1;
				real groundGap = -1.0f;
				if (bPlaced)
				{
					const glm::vec3 bedPos = beds.back()->GetTransform()->GetWorldPosition();
					if (TerrainGenerator* terrain = TerrainGenerator::GetActive())
					{
						groundGap = bedPos.y - terrain->SampleHeight(glm::vec2(bedPos.x, bedPos.z));
					}
				}
				Print("Item lights test: bed %s (feet %.2fm above the ground)\n", bPlaced ? "placed" : "NOT placed", groundGap);
				const bool bOnGround = glm::abs(groundGap) <= 0.3f;
				Print("Item lights test: %s\n", bPlaced && bOnGround && m_HeldTorchLightID == InvalidPointLightID ? "PASS" : "FAIL");
				Shot("bed_placed");
				s_Step = 10;
			}
		}

		// Held torch: a point light at its flame. It doesn't cast shadows (it moves every frame, so its faces would be
		// re-rendered constantly, & the player & the torch itself would shadow it): an unshadowed light beats none.
		bool bTorchLit = false;
		if (m_ActiveItemIndex != -1)
		{
			const ActiveItem& activeItem = m_ActiveItemCache[m_ActiveItemIndex];
			if (activeItem.m_ItemProxyObject != nullptr && activeItem.m_ItemProxyObject->IsVisible() &&
				activeItem.m_SourcePrefabID == g_ResourceManager->GetPrefabID("torch"))
			{
				Transform* proxyTransform = activeItem.m_ItemProxyObject->GetTransform();
				PointLightData data = {};
				data.pos = proxyTransform->GetWorldPosition() +
					proxyTransform->GetWorldRotation() * (glm::vec3(0.0f, TORCH_ITEM_FLAME_HEIGHT, 0.0f) * proxyTransform->GetWorldScale()) +
					// A little above the flame (like placed torches), so the handle right below isn't blown out
					VEC3_UP * 0.15f;
				data.enabled = 1;
				// (Like placed torches, see BuildPiece::UpdateTorchLights)
				data.colour = glm::vec3(1.0f, 0.8f, 0.6f);
				// (Dimmer than placed torches: it's always right next to the camera, where it blew everything in view out)
				data.brightness = 7.0f * FireFlicker(AnimationTime(), 3.7f);
				if (m_HeldTorchLightID == InvalidPointLightID)
				{
					m_HeldTorchLightID = g_Renderer->RegisterPointLight(&data);
				}
				else
				{
					g_Renderer->UpdatePointLightData(m_HeldTorchLightID, &data);
				}
				if (m_HeldTorchLightID != InvalidPointLightID)
				{
					g_Renderer->SetPointLightCastsShadow(m_HeldTorchLightID, false);
					bTorchLit = true;
				}
			}
		}
		if (!bTorchLit)
		{
			ReleaseHeldTorchLight();
		}

		// Worn headlamp: on the forehead, pointing where the player looks (in first & third person), with a shadow
		// (not of the player, whose head the lamp is inside)
		const glm::quat lookRot = GetLookRotation();
		const glm::vec3 mountPos = GetEyePosition() + VEC3_UP * 0.1f + GetLookDirection() * 0.12f;
		for (GameObject* child : m_Children)
		{
			if (child->GetTypeID() != HeadLampSID)
			{
				continue;
			}
			child->GetTransform()->SetWorldPosition(mountPos, false);
			child->GetTransform()->SetWorldRotation(lookRot, true);
			for (GameObject* lampChild : child->GetChildren())
			{
				if (lampChild->GetTypeID() != SpotLightSID)
				{
					continue;
				}
				SpotLight* spotLight = static_cast<SpotLight*>(lampChild);
				if (spotLight->spotLightID == InvalidSpotLightID || spotLight->data.enabled == 0)
				{
					continue;
				}
				// (Updated now rather than in SpotLight::Update, which ran before the player moved this frame)
				spotLight->data.pos = spotLight->GetTransform()->GetWorldPosition();
				spotLight->data.dir = spotLight->GetTransform()->GetWorldRotation() * -VEC3_FORWARD;
				g_Renderer->UpdateSpotLightData(spotLight->spotLightID, &spotLight->data);
				g_Renderer->SetSpotLightCastsShadow(spotLight->spotLightID, true, this);
			}
		}
	}

	bool Player::IsAnyInventoryShowing() const
	{
		// (Naming a tamed animal also shows the cursor & takes the keyboard)
		return m_bInventoryShowing || m_bMinerInventoryShowing || Animals::IsNamingDialogOpen() ||
			(m_Crafting != nullptr && m_Crafting->IsPanelOpen());
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
