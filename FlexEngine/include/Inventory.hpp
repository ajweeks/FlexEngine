#pragma once

#include <array>
#include <vector>

#include "Types.hpp"
#include "JSONTypes.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	template<u32 Len>
	bool SerializeInventory(const std::array<GameObjectStack, Len>& inventory, std::vector<JSONObject>& outObjList)
	{
		outObjList.reserve(Len);
		for (u32 slotIdx = 0; slotIdx < Len; ++slotIdx)
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

	template<u32 Len>
	bool ParseInventory(std::array<GameObjectStack, Len>& inventory, const std::vector<JSONObject>& slots)
	{
		u32 len = glm::min(Len, (u32)slots.size());
		for (u32 i = 0; i < len; ++i)
		{
			inventory[i].ParseFromJSON(slots[i]);
		}
		return true;
	}
} // namespace flex
