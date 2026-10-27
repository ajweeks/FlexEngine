#pragma once

#include "InputTypes.hpp"

// Keys & mouse buttons the game checks for directly, rather than through an Action binding (those are in
// config/input_bindings.json). Gameplay code should use these names instead of KeyCodes so the Controls screen
// (Game::DrawControls), which lists both, stays correct.
namespace flex
{
	namespace GameKeys
	{
		const KeyCode SPRINT = KeyCode::KEY_LEFT_SHIFT;
		const KeyCode CLIMB = KeyCode::KEY_LEFT_SHIFT; // Held next to a steep wall
		const KeyCode CROUCH = KeyCode::KEY_LEFT_CONTROL; // Slides & skis when moving fast, wingsuit while gliding
		const KeyCode GRAPPLING_HOOK = KeyCode::KEY_F; // Fires, or releases if it's out
		const KeyCode MAP = KeyCode::KEY_M;
		const KeyCode MAP_RECENTER = KeyCode::KEY_C;
		const KeyCode BUILD_MODE = KeyCode::KEY_B;
		const KeyCode BUILD_ROTATE = KeyCode::KEY_R;
		const KeyCode BUILD_MATERIAL = KeyCode::KEY_N;
		const KeyCode TERRAIN_MATERIAL = KeyCode::KEY_R; // With the shovel (Shift cycles backwards)
		const KeyCode CAMPFIRE = KeyCode::KEY_H;
		const KeyCode LIGHT_CAMPFIRE = KeyCode::KEY_E; // Held

		// The held item's actions (dig, chop, place terrain)
		const MouseButton USE_ITEM = MouseButton::LEFT;
		const MouseButton USE_ITEM_SECONDARY = MouseButton::RIGHT;
		const MouseButton TERRAIN_FLATTEN = MouseButton::MIDDLE;
	} // namespace GameKeys
} // namespace flex
