#pragma once

#include "InputTypes.hpp"

// Keys & mouse buttons the game checks for directly, rather than through an Action binding (those are in
// config/input_bindings.json). Gameplay code should use these names instead of KeyCodes so the Controls screen
// (Game::DrawControls), which lists both, stays correct.
//
// These are variables rather than constants so the player can rebind them on the Controls screen (they convert to
// KeyCode/MouseButton, so call sites don't change). Game saves the changes in its settings. To add one, define it
// here, add it to GameKeys::Registry() (its id is what's saved, so don't rename it) & add a row to Game::DrawControls.
namespace flex
{
	namespace GameKeys
	{
		struct Key
		{
			constexpr Key(KeyCode code) : code(code) {}
			operator KeyCode() const { return code; }
			KeyCode code;
		};

		struct Mouse
		{
			constexpr Mouse(MouseButton button) : button(button) {}
			operator MouseButton() const { return button; }
			MouseButton button;
		};

		// (Variables shared by every file, in the C++14 way: a reference to a static in an inline function)
#define GAME_KEY(NAME, DEFAULT) inline Key& NAME##_Storage() { static Key key(DEFAULT); return key; } static Key& NAME = NAME##_Storage()
#define GAME_MOUSE(NAME, DEFAULT) inline Mouse& NAME##_Storage() { static Mouse mouse(DEFAULT); return mouse; } static Mouse& NAME = NAME##_Storage()

		GAME_KEY(SPRINT, KeyCode::KEY_LEFT_SHIFT);
		GAME_KEY(CLIMB, KeyCode::KEY_LEFT_SHIFT); // Held next to a steep wall
		GAME_KEY(CROUCH, KeyCode::KEY_LEFT_CONTROL); // Slides & skis when moving fast, wingsuit while gliding
		GAME_KEY(CRAWL, KeyCode::KEY_LEFT_CONTROL); // Held on the ground: slower & lower, & ledges can't be walked off
		GAME_KEY(GRAPPLING_HOOK, KeyCode::KEY_F); // Fires, or releases if it's out
		GAME_KEY(MAP, KeyCode::KEY_M);
		GAME_KEY(MAP_RECENTER, KeyCode::KEY_C);
		GAME_KEY(WAYPOINT, KeyCode::KEY_Y); // Places a waypoint (up to 9) where the player stands, or at the cursor on the map. Again on a waypoint removes it
		GAME_KEY(BUILD_MODE, KeyCode::KEY_B);
		GAME_KEY(BUILD_ROTATE, KeyCode::KEY_R);
		GAME_KEY(BUILD_MATERIAL, KeyCode::KEY_N);
		GAME_KEY(TERRAIN_MATERIAL, KeyCode::KEY_R); // With the shovel (Shift cycles backwards)
		GAME_KEY(CAMPFIRE, KeyCode::KEY_H);
		GAME_KEY(LIGHT_CAMPFIRE, KeyCode::KEY_E); // Held

		// The held item's actions (dig, chop, place terrain)
		GAME_MOUSE(USE_ITEM, MouseButton::LEFT);
		GAME_MOUSE(USE_ITEM_SECONDARY, MouseButton::RIGHT);
		GAME_MOUSE(TERRAIN_FLATTEN, MouseButton::MIDDLE);
		GAME_MOUSE(MAP_WAYPOINT, MouseButton::RIGHT); // On the map screen: place or remove a waypoint at the cursor

		// What the Controls screen can rebind. Entries with the same non-zero shareTag are meant to overlap (they're
		// used in different situations), so they aren't conflicts.
		struct Entry
		{
			const char* id; // Saved in the settings
			Key* key; // One of key & mouse is set
			Mouse* mouse;
			KeyCode defaultKey;
			MouseButton defaultMouse;
			i32 shareTag;
		};

		inline const std::vector<Entry>& Registry()
		{
			static const std::vector<Entry> entries =
			{
				{ "sprint", &SPRINT, nullptr, SPRINT.code, MouseButton::_NONE, 1 },
				{ "climb", &CLIMB, nullptr, CLIMB.code, MouseButton::_NONE, 1 },
				{ "crouch", &CROUCH, nullptr, CROUCH.code, MouseButton::_NONE, 5 },
				{ "crawl", &CRAWL, nullptr, CRAWL.code, MouseButton::_NONE, 5 },
				{ "grappling hook", &GRAPPLING_HOOK, nullptr, GRAPPLING_HOOK.code, MouseButton::_NONE, 0 },
				{ "map", &MAP, nullptr, MAP.code, MouseButton::_NONE, 0 },
				{ "map recenter", &MAP_RECENTER, nullptr, MAP_RECENTER.code, MouseButton::_NONE, 0 },
				{ "waypoint", &WAYPOINT, nullptr, WAYPOINT.code, MouseButton::_NONE, 0 },
				{ "build mode", &BUILD_MODE, nullptr, BUILD_MODE.code, MouseButton::_NONE, 0 },
				{ "build rotate", &BUILD_ROTATE, nullptr, BUILD_ROTATE.code, MouseButton::_NONE, 2 },
				{ "build material", &BUILD_MATERIAL, nullptr, BUILD_MATERIAL.code, MouseButton::_NONE, 0 },
				{ "terrain material", &TERRAIN_MATERIAL, nullptr, TERRAIN_MATERIAL.code, MouseButton::_NONE, 2 },
				{ "campfire", &CAMPFIRE, nullptr, CAMPFIRE.code, MouseButton::_NONE, 0 },
				{ "light campfire", &LIGHT_CAMPFIRE, nullptr, LIGHT_CAMPFIRE.code, MouseButton::_NONE, 3 },
				{ "use item", nullptr, &USE_ITEM, KeyCode::_NONE, USE_ITEM.button, 0 },
				{ "use item secondary", nullptr, &USE_ITEM_SECONDARY, KeyCode::_NONE, USE_ITEM_SECONDARY.button, 4 },
				{ "terrain flatten", nullptr, &TERRAIN_FLATTEN, KeyCode::_NONE, TERRAIN_FLATTEN.button, 0 },
				{ "map waypoint", nullptr, &MAP_WAYPOINT, KeyCode::_NONE, MAP_WAYPOINT.button, 4 },
			};
			return entries;
		}
	} // namespace GameKeys
} // namespace flex
