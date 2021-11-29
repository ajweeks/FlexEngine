#pragma once

#include <array>
#include <vector>

#include "Types.hpp"
#include "JSONTypes.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	template<i32 Len>
	bool SerializeInventory(const std::array<GameObjectStack, Len>& inventory, std::vector<JSONObject>& outObjList)
	{
		outObjList.reserve(inventory.size());
		for (i32 slotIdx = 0; slotIdx < (i32)inventory.size(); ++slotIdx)
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

	template<i32 Len>
	bool ParseInventory(std::array<GameObjectStack, Len>& inventory, const std::vector<JSONObject>& slots)
	{
		i32 len = glm::min((i32)inventory.size(), (i32)slots.size());
		for (i32 i = 0; i < len; ++i)
		{
			inventory[i].ParseFromJSON(slots[i]);
		}
		return true;
	}
} // namespace flex
