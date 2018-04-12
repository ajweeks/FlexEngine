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

	void InputManager::Initialize()
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
		io.KeyMap[ImGuiKey_Delete] = (i32)KeyCode::KEY_DELETE;
		io.KeyMap[ImGuiKey_Backspace] = (i32)KeyCode::KEY_BACKSPACE;
		io.KeyMap[ImGuiKey_Enter] = (i32)KeyCode::KEY_ENTER;
		io.KeyMap[ImGuiKey_Escape] = (i32)KeyCode::KEY_ESCAPE;
		io.KeyMap[ImGuiKey_A] = (i32)KeyCode::KEY_A;
		io.KeyMap[ImGuiKey_C] = (i32)KeyCode::KEY_C;
		io.KeyMap[ImGuiKey_V] = (i32)KeyCode::KEY_V;
		io.KeyMap[ImGuiKey_X] = (i32)KeyCode::KEY_X;
		io.KeyMap[ImGuiKey_Y] = (i32)KeyCode::KEY_Y;
		io.KeyMap[ImGuiKey_Z] = (i32)KeyCode::KEY_Z;

		// TODO: Figure out how to use RESOURCE_LOCATION constant here
		io.IniFilename = "FlexEngine/resources/imgui.ini";
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

	void InputManager::PostImGuiUpdate(const GameContext& gameContext)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (io.WantCaptureMouse)
		{
			ClearMouseInput(gameContext);
		}
		if (io.WantCaptureKeyboard)
		{
			ClearKeyboadInput(gameContext);
		}
	}

	void InputManager::PostUpdate()
	{
		m_PrevMousePosition = m_MousePosition;
		m_ScrollXOffset = 0.0f;
		m_ScrollYOffset = 0.0f;
	}

	i32 InputManager::GetKeyDown(KeyCode keyCode) const
	{
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

		io.KeyCtrl = GetKeyDown(KeyCode::KEY_LEFT_CONTROL) || GetKeyDown(KeyCode::KEY_RIGHT_CONTROL);
		io.KeyShift = GetKeyDown(KeyCode::KEY_LEFT_SHIFT) || GetKeyDown(KeyCode::KEY_RIGHT_SHIFT);
		io.KeyAlt = GetKeyDown(KeyCode::KEY_LEFT_ALT) || GetKeyDown(KeyCode::KEY_RIGHT_ALT);
		io.KeySuper = GetKeyDown(KeyCode::KEY_LEFT_SUPER) || GetKeyDown(KeyCode::KEY_RIGHT_SUPER);
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
		return m_MousePosition - m_PrevMousePosition;
	}

	i32 InputManager::GetMouseButtonDown(MouseButton mouseButton) const
	{
		assert((i32)mouseButton >= 0 && (i32)mouseButton <= MOUSE_BUTTON_COUNT - 1);

		return m_MouseButtons[(i32)mouseButton].down;
	}

	bool InputManager::GetMouseButtonClicked(MouseButton mouseButton) const
	{
		assert((i32)mouseButton >= 0 && (i32)mouseButton <= MOUSE_BUTTON_COUNT - 1);

		return (m_MouseButtons[(i32)mouseButton].down == 1);
	}

	real InputManager::GetVerticalScrollDistance() const
	{
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
} // namespace flex
