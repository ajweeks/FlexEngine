#include "stdafx.hpp"

#include "Inventory.hpp"

namespace flex
{
	bool SerializeInventory(GameObjectStack* inventory, u32 inventorySize, std::vector<JSONObject>& outObjList)
	{
		outObjList.reserve(inventorySize);
		for (u32 slotIdx = 0; slotIdx < inventorySize; ++slotIdx)
		{
			const GameObjectStack& stack = inventory[slotIdx];
			if (stack.count > 0)
			{
				JSONObject slot = {};
				stack.SerializeToJSON(slot);
				slot.fields.emplace_back("index", JSONValue(slotIdx));

				outObjList.emplace_back(slot);
			}
		}
		return true;
	}

	bool ParseInventory(GameObjectStack* inventory, u32 inventorySize, const std::vector<JSONObject>& slots)
	{
		u32 len = glm::min(inventorySize, (u32)slots.size());
		for (u32 i = 0; i < len; ++i)
		{
			inventory[i].ParseFromJSON(slots[i]);
		}
		return true;
	}
} // namespace flex
