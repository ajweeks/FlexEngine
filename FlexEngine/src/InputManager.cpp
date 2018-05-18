#include "stdafx.hpp"

#include "InputManager.hpp"

#include <assert.h>

#include "imgui.h"

#include "Logger.hpp"
#include "Window/Window.hpp"

namespace flex
{
	InputManager::InputManager()
	{
	}

	InputManager::~InputManager()
	{
	}

	void InputManager::Initialize(const GameContext& gameContext)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.KeyMap[ImGuiKey_Tab] = (i32)KeyCode::KEY_TAB;
		io.KeyMap[ImGuiKey_LeftArrow] = (i32)KeyCode::KEY_LEFT;
		io.KeyMap[ImGuiKey_RightArrow] = (i32)KeyCode::KEY_RIGHT;
		io.KeyMap[ImGuiKey_UpArrow] = (i32)KeyCode::KEY_UP;
		io.KeyMap[ImGuiKey_DownArrow] = (i32)KeyCode::KEY_DOWN;
		io.KeyMap[ImGuiKey_PageUp] = (i32)KeyCode::KEY_PAGE_UP;
		io.KeyMap[ImGuiKey_PageDown] = (i32)KeyCode::KEY_PAGE_DOWN;
		io.KeyMap[ImGuiKey_Home] = (i32)KeyCode::KEY_HOME;
		io.KeyMap[ImGuiKey_End] = (i32)KeyCode::KEY_END;
		io.KeyMap[ImGuiKey_Insert] = (i32)KeyCode::KEY_INSERT;
		io.KeyMap[ImGuiKey_Delete] = (i32)KeyCode::KEY_DELETE;
		io.KeyMap[ImGuiKey_Backspace] = (i32)KeyCode::KEY_BACKSPACE;
		io.KeyMap[ImGuiKey_Space] = (i32)KeyCode::KEY_SPACE;
		io.KeyMap[ImGuiKey_Enter] = (i32)KeyCode::KEY_ENTER;
		io.KeyMap[ImGuiKey_Escape] = (i32)KeyCode::KEY_ESCAPE;
		io.KeyMap[ImGuiKey_A] = (i32)KeyCode::KEY_A; // for text edit CTRL+A: select all
		io.KeyMap[ImGuiKey_C] = (i32)KeyCode::KEY_C; // for text edit CTRL+C: copy
		io.KeyMap[ImGuiKey_V] = (i32)KeyCode::KEY_V; // for text edit CTRL+V: paste
		io.KeyMap[ImGuiKey_X] = (i32)KeyCode::KEY_X; // for text edit CTRL+X: cut
		io.KeyMap[ImGuiKey_Y] = (i32)KeyCode::KEY_Y; // for text edit CTRL+Y: redo
		io.KeyMap[ImGuiKey_Z] = (i32)KeyCode::KEY_Z; // for text edit CTRL+Z: undo

		m_ImGuiIniFilepathStr = RESOURCE_LOCATION + "imgui.ini";
		io.IniFilename  = m_ImGuiIniFilepathStr.c_str();

		ClearAllInputs(gameContext);
	}

	void InputManager::Update()
	{
		for (auto iter = m_Keys.begin(); iter != m_Keys.end(); ++iter)
		{
			if (iter->second.down > 0)
			{
				++iter->second.down;
			}
		}

		for (size_t i = 0; i < MOUSE_BUTTON_COUNT; ++i)
		{
			if (m_MouseButtons[i].down > 0)
			{
				++m_MouseButtons[i].down;
				m_MouseButtonDrags[i].endLocation = m_MousePosition;
			}
		}
	}

	void InputManager::PostUpdate()
	{
		m_PrevMousePosition = m_MousePosition;
		m_ScrollXOffset = 0.0f;
		m_ScrollYOffset = 0.0f;
	}

	void InputManager::UpdateGamepadState(i32 gamepadIndex, real axes[6], u8 buttons[15])
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		for (i32 i = 0; i < 6; ++i)
		{
			m_GampadAxes[gamepadIndex][i] = axes[i];
		}

		// Only handle left & right sticks (last two axes are triggers, which don't have radial dead zones
		HandleRadialDeadZone(&m_GampadAxes[gamepadIndex][0], &m_GampadAxes[gamepadIndex][1]);
		HandleRadialDeadZone(&m_GampadAxes[gamepadIndex][2], &m_GampadAxes[gamepadIndex][3]);

		u32 pStates = m_GamepadButtonStates[gamepadIndex];

		m_GamepadButtonStates[gamepadIndex] = 0;
		for (i32 i = 0; i < 15; ++i)
		{
			m_GamepadButtonStates[gamepadIndex] = m_GamepadButtonStates[gamepadIndex] | (buttons[i] << i);
		}

		u32 changedButtons = m_GamepadButtonStates[gamepadIndex] ^ pStates;
		m_GamepadButtonsPressed[gamepadIndex] = changedButtons & m_GamepadButtonStates[gamepadIndex];
		m_GamepadButtonsReleased[gamepadIndex] = changedButtons & (~m_GamepadButtonStates[gamepadIndex]);
	}

	// http://www.third-helix.com/2013/04/12/doing-thumbstick-dead-zones-right.html
	void InputManager::HandleRadialDeadZone(real* x, real* y)
	{
		real deadzone = 0.25f;

		glm::vec2 stick(*x, *y);
		real stickMagnitude = glm::length(stick);
		if (stickMagnitude < deadzone)
		{
			*x = 0.0f;
			*y = 0.0f;
		}
		else
		{
			glm::vec2 stickDir = (stick / stickMagnitude);
			glm::vec2 stickScaled = stickDir * ((stickMagnitude - deadzone) / (1.0f - deadzone));

			*x = stickScaled.x;
			*y = stickScaled.y;
		}
	}

	i32 InputManager::GetKeyDown(KeyCode keyCode, bool ignoreImGui) const
	{
		if (!ignoreImGui && ImGui::GetIO().WantCaptureKeyboard)
		{
			return 0;
		}

		auto value = m_Keys.find(keyCode);
		if (value != m_Keys.end())
		{
			return m_Keys.at(keyCode).down;
		}

		return 0;
	}

	bool InputManager::GetKeyPressed(KeyCode keyCode) const
	{
		return GetKeyDown(keyCode) == 1;
	}

	bool InputManager::IsGamepadButtonDown(i32 gamepadIndex, GamepadButton button)
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		bool down = m_GamepadButtonStates[gamepadIndex] & (1 << (i32)button);
		return down;
	}

	bool InputManager::IsGamepadButtonPressed(i32 gamepadIndex, GamepadButton button)
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		bool pressed = m_GamepadButtonsPressed[gamepadIndex] & (1 << (i32)button);
		return pressed;
	}

	bool InputManager::IsGamepadButtonReleased(i32 gamepadIndex, GamepadButton button)
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		bool released = m_GamepadButtonsReleased[gamepadIndex] & (1 << (i32)button);
		return released;
	}

	real InputManager::GetGamepadAxisValue(i32 gamepadIndex, GamepadAxis axis)
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);
	
		real axisValue = m_GampadAxes[gamepadIndex][(i32)axis];
		return axisValue;
	}

	void InputManager::CursorPosCallback(double x, double y)
	{
		m_MousePosition = glm::vec2((real)x, (real)y);

		ImGuiIO& io = ImGui::GetIO();
		io.MousePos = m_MousePosition;
	}

	void InputManager::MouseButtonCallback(const GameContext& gameContext, MouseButton mouseButton, Action action, i32 mods)
	{
		UNREFERENCED_PARAMETER(gameContext);
		UNREFERENCED_PARAMETER(mods);

		assert((i32)mouseButton < MOUSE_BUTTON_COUNT);

		if (action == Action::PRESS)
		{
			++m_MouseButtons[(i32)mouseButton].down;
			m_MouseButtonDrags[(i32)mouseButton].startLocation = m_MousePosition;
			m_MouseButtonDrags[(i32)mouseButton].endLocation = m_MousePosition;
			//if (button == MouseButton::LEFT)
			//{
			//	gameContext.window->SetCursorMode(Window::CursorMode::HIDDEN);
			//}
		}
		else if (action == Action::RELEASE)
		{
			m_MouseButtons[(i32)mouseButton].down = 0;
			m_MouseButtonDrags[(i32)mouseButton].startLocation = m_MousePosition;
			m_MouseButtonDrags[(i32)mouseButton].endLocation = m_MousePosition;

			//if (button == MouseButton::LEFT)
			//{
			//	gameContext.window->SetCursorMode(Window::CursorMode::NORMAL);
			//}
		}

		ImGuiIO& io = ImGui::GetIO();
		io.MouseDown[(i32)mouseButton] = m_MouseButtons[(i32)mouseButton].down > 0;
		io.MouseClicked[(i32)mouseButton] = m_MouseButtons[(i32)mouseButton].down == 1;
	}

	void InputManager::ScrollCallback(double xOffset, double yOffset)
	{
		m_ScrollXOffset = (real)xOffset;
		m_ScrollYOffset = (real)yOffset;

		ImGuiIO& io = ImGui::GetIO();
		io.MouseWheel = m_ScrollYOffset;
	}

	void InputManager::KeyCallback(KeyCode keycode, Action action, i32 mods)
	{
		UNREFERENCED_PARAMETER(mods);

		if (action == Action::PRESS)
		{
			m_Keys[keycode].down = 1;
		}
		else if (action == Action::REPEAT)
		{
			// Ignore repeat events (we're already counting how long the key is down for
		}
		else if (action == Action::RELEASE)
		{
			m_Keys[keycode].down = 0;
		}

		ImGuiIO& io = ImGui::GetIO();
		io.KeysDown[(i32)keycode] = m_Keys[keycode].down > 0;

		io.KeyCtrl = GetKeyDown(KeyCode::KEY_LEFT_CONTROL, true) || GetKeyDown(KeyCode::KEY_RIGHT_CONTROL, true);
		io.KeyShift = GetKeyDown(KeyCode::KEY_LEFT_SHIFT, true) || GetKeyDown(KeyCode::KEY_RIGHT_SHIFT, true);
		io.KeyAlt = GetKeyDown(KeyCode::KEY_LEFT_ALT, true) || GetKeyDown(KeyCode::KEY_RIGHT_ALT, true);
		io.KeySuper = GetKeyDown(KeyCode::KEY_LEFT_SUPER, true) || GetKeyDown(KeyCode::KEY_RIGHT_SUPER, true);
	}

	void InputManager::CharCallback(u32 character)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddInputCharacter((ImWchar)character);
	}

	glm::vec2 InputManager::GetMousePosition() const
	{
		return m_MousePosition;
	}

	void InputManager::SetMousePosition(glm::vec2 mousePos, bool updatePreviousPos)
	{
		m_MousePosition = mousePos;

		if (updatePreviousPos)
		{
			m_PrevMousePosition = m_MousePosition;
		}
	}

	glm::vec2 InputManager::GetMouseMovement() const
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return glm::vec2(0, 0);
		}

		return m_MousePosition - m_PrevMousePosition;
	}

	i32 InputManager::GetMouseButtonDown(MouseButton mouseButton) const
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return 0;
		}

		assert((i32)mouseButton >= 0 && (i32)mouseButton <= MOUSE_BUTTON_COUNT - 1);

		return m_MouseButtons[(i32)mouseButton].down;
	}

	bool InputManager::GetMouseButtonClicked(MouseButton mouseButton) const
	{
		return (GetMouseButtonDown(mouseButton) == 1);
	}

	real InputManager::GetVerticalScrollDistance() const
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return 0.0f;
		}

		return m_ScrollYOffset;
	}

	glm::vec2 InputManager::GetMouseDragDistance(MouseButton mouseButton)
	{
		assert((i32)mouseButton >= 0 && (i32)mouseButton <= MOUSE_BUTTON_COUNT - 1);

		return (m_MouseButtonDrags[(i32)mouseButton].endLocation - m_MouseButtonDrags[(i32)mouseButton].startLocation);
	}

	void InputManager::ClearAllInputs(const GameContext& gameContext)
	{
		ClearMouseInput(gameContext);
		ClearKeyboadInput(gameContext);
		ClearGampadInput(0);
		ClearGampadInput(1);
	}

	void InputManager::ClearMouseInput(const GameContext& gameContext)
	{
		m_PrevMousePosition = m_MousePosition;
		m_ScrollXOffset = 0.0f;
		m_ScrollYOffset = 0.0f;

		for (i32 i = 0; i < MOUSE_BUTTON_COUNT; ++i)
		{
			m_MouseButtons[i].down = 0;
		}

		gameContext.window->SetCursorMode(Window::CursorMode::NORMAL);


		ImGuiIO& io = ImGui::GetIO();
		io.MousePos = m_MousePosition;
		io.MousePosPrev = m_PrevMousePosition;
		io.MouseWheel = m_ScrollYOffset;
	}

	void InputManager::ClearKeyboadInput(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);

		for (auto iter = m_Keys.begin(); iter != m_Keys.end(); ++iter)
		{
			iter->second.down = 0;
		}

		for (size_t i = 0; i < MOUSE_BUTTON_COUNT; ++i)
		{
			m_MouseButtons[i].down = 0;
		}
	}

	void InputManager::ClearGampadInput(i32 gamepadIndex)
	{
		for (i32 j = 0; j < 6; ++j)
		{
			m_GampadAxes[gamepadIndex][j] = 0;
		}

		m_GamepadButtonStates[gamepadIndex] = 0;
		m_GamepadButtonsPressed[gamepadIndex] = 0;
		m_GamepadButtonsReleased[gamepadIndex] = 0;
	}
} // namespace flex
