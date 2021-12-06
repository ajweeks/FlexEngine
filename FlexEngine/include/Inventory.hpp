#pragma once

#include <array>
#include <vector>

#include "Types.hpp"
#include "JSONTypes.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	bool SerializeInventory(GameObjectStack* inventory, u32 inventorySize, std::vector<JSONObject>& outObjList);
	bool ParseInventory(GameObjectStack* inventory, u32 inventorySize, const std::vector<JSONObject>& slots);
} // namespace flex
