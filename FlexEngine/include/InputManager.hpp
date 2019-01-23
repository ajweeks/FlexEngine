#pragma once

#include "InputEnums.hpp"

namespace flex
{
	class InputManager
	{
	public:
		InputManager();
		~InputManager();

		void Initialize();

		void Update();
		void PostUpdate();

		void UpdateGamepadState(i32 gamepadIndex, real axes[6], u8 buttons[15]);
		GamepadState& GetGamepadState(i32 gamepadIndex);

		i32 GetKeyDown(KeyCode keyCode, bool bIgnoreImGui = false) const;
		bool GetKeyPressed(KeyCode keyCode, bool bIgnoreImGui = false) const;

		bool IsGamepadButtonDown(i32 gamepadIndex, GamepadButton button);
		bool IsGamepadButtonPressed(i32 gamepadIndex, GamepadButton button);
		bool IsGamepadButtonReleased(i32 gamepadIndex, GamepadButton button);

		real GetGamepadAxisValue(i32 gamepadIndex, GamepadAxis axis);
		// Axis-equivalent to button "press"
		bool HasGamepadAxisValueJustPassedThreshold(i32 gamepadIndex, GamepadAxis axis, real threshold);

		void CursorPosCallback(double x, double y);
		void MouseButtonCallback(MouseButton mouseButton, KeyAction action, i32 mods);
		void ScrollCallback(double xOffset, double yOffset);
		void KeyCallback(KeyCode keycode, KeyAction action, i32 mods);
		void CharCallback(u32 character);

		bool DidMouseWrap() const;

		void SetMousePosition(glm::vec2 mousePos, bool bUpdatePreviousPos = true);
		glm::vec2 GetMousePosition() const;
		void ClearMouseMovement();
		glm::vec2 GetMouseMovement() const;
		void ClearMouseButton(MouseButton mouseButton);
		bool IsAnyMouseButtonDown(bool bIgnoreImGui = false) const;
		bool IsMouseButtonDown(MouseButton mouseButton) const;
		bool IsMouseButtonPressed(MouseButton mouseButton) const;
		bool IsMouseButtonReleased(MouseButton mouseButton) const;
		real GetVerticalScrollDistance() const;
		void ClearVerticalScrollDistance();

		// posNorm: normalized position of center of the rect [-1, 1] (y = 1 at top of screen)
		// sizeNorm: normalized size of the rect [0, 1]
		bool IsMouseHoveringRect(const glm::vec2& posNorm, const glm::vec2& sizeNorm);

		glm::vec2 GetMouseDragDistance(MouseButton mouseButton);
		void ClearMouseDragDistance(MouseButton mouseButton);

		void ClearAllInputs();
		void ClearMouseInput();
		void ClearKeyboadInput();
		void ClearGampadInput(i32 gamepadIndex);

		static i32 s_JoystickDisconnected;

		bool bPlayerUsingKeyboard[2];

		std::vector<Binding> g_InputBindings;

	private:
		void HandleRadialDeadZone(real* x, real* y);
		void LoadInputBindingsFromFile();

		std::map<KeyCode, Key> m_Keys;

		static const i32 GAMEPAD_BUTTON_COUNT = (i32)GamepadButton::_COUNT;
		static const i32 MOUSE_BUTTON_COUNT = (i32)MouseButton::_NONE;
		u32 m_MouseButtonStates;
		u32 m_MouseButtonsPressed;
		u32 m_MouseButtonsReleased;
		MouseDrag m_MouseButtonDrags[MOUSE_BUTTON_COUNT];
		glm::vec2 m_MousePosition = { 0, 0 };
		glm::vec2 m_PrevMousePosition = { 0, 0 };
		real m_ScrollXOffset = 0;
		real m_ScrollYOffset = 0;

		bool m_bMouseWrapped = false;

		GamepadState m_pGamepadStates[2];
		GamepadState m_GamepadStates[2];

		static const real MAX_JOYSTICK_ROTATION_SPEED;
	};
} // namespace flex
