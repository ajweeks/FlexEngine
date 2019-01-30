#pragma once

#include "RollingAverage.hpp"

namespace flex
{
	enum class KeyAction
	{
		PRESS,
		RELEASE,
		REPEAT,

		_NONE
	};

	static constexpr const char* KeyActionStrings[] =
	{
		"Press",
		"Release",
		"Repeat",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(KeyActionStrings) == (u32)KeyAction::_NONE + 1, "KeyActionStrings length must match KeyAction enum");

	enum class KeyCode
	{
		KEY_SPACE,
		KEY_APOSTROPHE,
		KEY_COMMA,
		KEY_MINUS,
		KEY_PERIOD,
		KEY_SLASH,
		KEY_0,
		KEY_1,
		KEY_2,
		KEY_3,
		KEY_4,
		KEY_5,
		KEY_6,
		KEY_7,
		KEY_8,
		KEY_9,
		KEY_SEMICOLON,
		KEY_EQUAL,
		KEY_A,
		KEY_B,
		KEY_C,
		KEY_D,
		KEY_E,
		KEY_F,
		KEY_G,
		KEY_H,
		KEY_I,
		KEY_J,
		KEY_K,
		KEY_L,
		KEY_M,
		KEY_N,
		KEY_O,
		KEY_P,
		KEY_Q,
		KEY_R,
		KEY_S,
		KEY_T,
		KEY_U,
		KEY_V,
		KEY_W,
		KEY_X,
		KEY_Y,
		KEY_Z,
		KEY_LEFT_BRACKET,
		KEY_BACKSLASH,
		KEY_RIGHT_BRACKET,
		KEY_GRAVE_ACCENT,
		KEY_WORLD_1,
		KEY_WORLD_2,

		KEY_ESCAPE,
		KEY_ENTER,
		KEY_TAB,
		KEY_BACKSPACE,
		KEY_INSERT,
		KEY_DELETE,
		KEY_RIGHT,
		KEY_LEFT,
		KEY_DOWN,
		KEY_UP,
		KEY_PAGE_UP,
		KEY_PAGE_DOWN,
		KEY_HOME,
		KEY_END,
		KEY_CAPS_LOCK,
		KEY_SCROLL_LOCK,
		KEY_NUM_LOCK,
		KEY_PRINT_SCREEN,
		KEY_PAUSE,
		KEY_F1,
		KEY_F2,
		KEY_F3,
		KEY_F4,
		KEY_F5,
		KEY_F6,
		KEY_F7,
		KEY_F8,
		KEY_F9,
		KEY_F10,
		KEY_F11,
		KEY_F12,
		KEY_F13,
		KEY_F14,
		KEY_F15,
		KEY_F16,
		KEY_F17,
		KEY_F18,
		KEY_F19,
		KEY_F20,
		KEY_F21,
		KEY_F22,
		KEY_F23,
		KEY_F24,
		KEY_F25,
		KEY_KP_0,
		KEY_KP_1,
		KEY_KP_2,
		KEY_KP_3,
		KEY_KP_4,
		KEY_KP_5,
		KEY_KP_6,
		KEY_KP_7,
		KEY_KP_8,
		KEY_KP_9,
		KEY_KP_DECIMAL,
		KEY_KP_DIVIDE,
		KEY_KP_MULTIPLY,
		KEY_KP_SUBTRACT,
		KEY_KP_ADD,
		KEY_KP_ENTER,
		KEY_KP_EQUAL,
		KEY_LEFT_SHIFT,
		KEY_LEFT_CONTROL,
		KEY_LEFT_ALT,
		KEY_LEFT_SUPER,
		KEY_RIGHT_SHIFT,
		KEY_RIGHT_CONTROL,
		KEY_RIGHT_ALT,
		KEY_RIGHT_SUPER,
		KEY_MENU,

		_NONE
	};

	static constexpr const char* KeyCodeStrings[] =
	{
		"Space",
		"'",
		",",
		"-",
		".",
		"/",
		"0",
		"1",
		"2",
		"3",
		"4",
		"5",
		"6",
		"7",
		"8",
		"9",
		";",
		"=",
		"A",
		"B",
		"C",
		"D",
		"E",
		"F",
		"G",
		"H",
		"I",
		"J",
		"K",
		"L",
		"M",
		"N",
		"O",
		"P",
		"Q",
		"R",
		"S",
		"T",
		"U",
		"V",
		"W",
		"X",
		"Y",
		"Z",
		"[",
		"\\",
		"]",
		"`",
		"World 1",
		"World 2",

		"ESC",
		"Enter",
		"Tab",
		"Backspace",
		"Insert",
		"Delete",
		"Right",
		"Left",
		"Down",
		"Up",
		"Page up",
		"Page down",
		"Home",
		"End",
		"Caps lock",
		"Scroll lock",
		"Num lock",
		"Print screen",
		"Pause",
		"F1",
		"F2",
		"F3",
		"F4",
		"F5",
		"F6",
		"F7",
		"F8",
		"F9",
		"F10",
		"F11",
		"F12",
		"F13",
		"F14",
		"F15",
		"F16",
		"F17",
		"F18",
		"F19",
		"F20",
		"F21",
		"F22",
		"F23",
		"F24",
		"F25",
		"Pad 0",
		"Pad 1",
		"Pad 2",
		"Pad 3",
		"Pad 4",
		"Pad 5",
		"Pad 6",
		"Pad 7",
		"Pad 8",
		"Pad 9",
		"Pad .",
		"Pad /",
		"Pad *",
		"Pad -",
		"Pad +",
		"Pad Enter",
		"Pad Equal",
		"L Shift",
		"L Ctrl",
		"L Alt",
		"L Super",
		"R Shift",
		"R Ctrl",
		"R Alt",
		"R Super",
		"Menu",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(KeyCodeStrings) == (u32)KeyCode::_NONE + 1, "Key code strings length must match KeyCode enum");

	enum class InputModifier
	{
		SHIFT = (1 << 0),
		CONTROL = (1 << 1),
		ALT = (1 << 2),
		SUPER = (1 << 3),

		_NONE = 0,
	};

	static constexpr const char* InputModifierStrings[] =
	{
		"Shift",
		"Control",
		"Alt",
		"Super",

		"NONE"
	};

	enum class MouseButton
	{
		MOUSE_BUTTON_1,
		MOUSE_BUTTON_2,
		MOUSE_BUTTON_3,
		MOUSE_BUTTON_4,
		MOUSE_BUTTON_5,
		MOUSE_BUTTON_6,
		MOUSE_BUTTON_7,
		MOUSE_BUTTON_8,

		LEFT = MOUSE_BUTTON_1,
		RIGHT = MOUSE_BUTTON_2,
		MIDDLE = MOUSE_BUTTON_3,

		_NONE = MOUSE_BUTTON_8 + 1
	};

	static constexpr const char* MouseButtonStrings[] =
	{
		"LMB",
		"RMB",
		"MMB",
		"Button 4",
		"Button 5",
		"Button 6",
		"Button 7",
		"Button 8",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(MouseButtonStrings) == (u32)MouseButton::_NONE + 1, "MouseButtonStrings length must match MouseButton enum");

	enum class GamepadButton
	{
		A = 0,
		B = 1,
		X = 2,
		Y = 3,
		LEFT_BUMPER = 4,
		RIGHT_BUMPER = 5,
		BACK = 6,
		START = 7,
		// 8 ?
		LEFT_STICK_DOWN = 9,
		RIGHT_STICK_DOWN = 10,
		D_PAD_UP = 11,
		D_PAD_RIGHT = 12,
		D_PAD_DOWN = 13,
		D_PAD_LEFT = 14,

		_NONE
	};

	// TODO: Support naming for other common gamepads
	static constexpr const char* GamepadButtonStrings[] =
	{
		"A",
		"B",
		"X",
		"Y",
		"L Bumper",
		"R Bumper",
		"Back",
		"Start",
		"Invalid", // TODO: Find out what gamepads use this index
		"L Stick",
		"R Stick",
		"Pad Up",
		"Pad Right",
		"Pad Down",
		"Pad Left",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(GamepadButtonStrings) == (u32)GamepadButton::_NONE + 1, "GamepadButtonStrings length must match GamepadButton enum");

	enum class GamepadAxis
	{
		LEFT_STICK_X = 0,
		LEFT_STICK_Y = 1,
		RIGHT_STICK_X = 2,
		RIGHT_STICK_Y = 3,
		LEFT_TRIGGER = 4,
		RIGHT_TRIGGER = 5,

		_NONE
	};

	static constexpr const char* GamepadAxisStrings[] =
	{
		"L Stick X",
		"L Stick Y",
		"R Stick X",
		"R Stick Y",
		"L Trigger",
		"R Trigger",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(GamepadAxisStrings) == (u32)GamepadAxis::_NONE + 1, "GamepadAxisStrings length must match GamepadAxis enum");

	enum class MouseAxis
	{
		X,
		Y,
		SCROLL_X,
		SCROLL_Y,

		_NONE
	};

	static constexpr const char* MouseAxisStrings[] =
	{
		"Axis X",
		"Axis Y",
		"Scroll X",
		"Scroll Y",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(MouseAxisStrings) == (u32)MouseAxis::_NONE + 1, "MouseAxisStrings length must match MouseAxis enum");

	enum class Action
	{
		MOVE_LEFT,
		MOVE_RIGHT,
		MOVE_FORWARD,
		MOVE_BACKWARD,
		LOOK_UP,
		LOOK_DOWN,
		LOOK_LEFT,
		LOOK_RIGHT,
		INTERACT,
		PLACE_ITEM,
		PAUSE,
		ZOOM_IN,
		ZOOM_OUT,
		TOGGLE_TABLET,
		PLACE_NODE,
		COMPLETE_TRACK,

		TAKE_SCREENSHOT,

		// Editor
		EDITOR_RENAME_SELECTED,
		EDITOR_SELECT_TRANSLATE_GIZMO,
		EDITOR_SELECT_ROTATE_GIZMO,
		EDITOR_SELECT_SCALE_GIZMO,

		// Debug
		DBG_SWITCH_TO_NEXT_CAM,
		DBG_SWITCH_TO_PREV_CAM,
		DBG_ENTER_NEXT_SCENE,
		DBG_ENTER_PREV_SCENE,

		_NONE,
		COUNT = _NONE
	};

	static constexpr const char* ActionStrings[] =
	{
		"Move left",
		"Move right",
		"Move forward",
		"Move backward",
		"Look up",
		"Look down",
		"Look left",
		"Look right",
		"Interact",
		"Place item",
		"Pause",
		"Zoom in",
		"Zoom out",
		"Toggle tablet",
		"Place node",
		"Complete track",

		"Take screenshot",

		// Editor
		"Editor Rename selected",
		"Editor Select translate gizmo",
		"Editor Select rotate gizmo",
		"Editor Select scale gizmo",

		// Debug
		"DBG Switch to next cam",
		"DBG Switch to prev cam",
		"DBG Enter next scene",
		"DBG Enter prev scene",

		"None"
	};

	static_assert(ARRAY_LENGTH(ActionStrings) == (u32)Action::_NONE + 1, "ActionStrings length must match Action enum");

	struct InputBinding
	{
		KeyCode keyCode = KeyCode::_NONE;
		MouseButton mouseButton = MouseButton::_NONE;
		MouseAxis mouseAxis = MouseAxis::_NONE;
		GamepadButton gamepadButton = GamepadButton::_NONE;
		GamepadAxis gamepadAxis = GamepadAxis::_NONE;
		bool bNegative = false;
	};

	struct Key
	{
		i32 pDown = 0;
		i32 down = 0; // A count of how many frames this key has been down for (0 means not down)
	};

	struct MouseDrag
	{
		glm::vec2 startLocation;
		glm::vec2 endLocation;
	};

	struct GamepadState
	{
		// Bitfield used to store gamepad button states for each player
		// 0 = up, 1 = down (See GamepadButton enum)
		u32 buttonStates = 0;
		u32 buttonsPressed = 0;
		u32 buttonsReleased = 0;

		// LEFT_STICK_X, LEFT_STICK_Y, RIGHT_STICK_X, RIGHT_STICK_Y, LEFT_TRIGGER, RIGHT_TRIGGER
		real axes[6];
		RollingAverage<real> averageRotationSpeeds;
		i32 framesToAverageOver = 10;
		real pJoystickX = 0.0f;
		real pJoystickY = 0.0f;
		i32 previousQuadrant = -1;
	};
} // namespace flex
