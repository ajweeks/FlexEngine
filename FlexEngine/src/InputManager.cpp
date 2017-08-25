#include "stdafx.h"

#include "InputManager.h"

#include <assert.h>

#include <imgui.h>

#include "Logger.h"
#include "Window/Window.h"

namespace flex
{
	InputManager::InputManager()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.KeyMap[ImGuiKey_Tab] = (int)KeyCode::KEY_TAB;
		io.KeyMap[ImGuiKey_LeftArrow] = (int)KeyCode::KEY_LEFT;
		io.KeyMap[ImGuiKey_RightArrow] = (int)KeyCode::KEY_RIGHT;
		io.KeyMap[ImGuiKey_UpArrow] = (int)KeyCode::KEY_UP;
		io.KeyMap[ImGuiKey_DownArrow] = (int)KeyCode::KEY_DOWN;
		io.KeyMap[ImGuiKey_PageUp] = (int)KeyCode::KEY_PAGE_UP;
		io.KeyMap[ImGuiKey_PageDown] = (int)KeyCode::KEY_PAGE_DOWN;
		io.KeyMap[ImGuiKey_Home] = (int)KeyCode::KEY_HOME;
		io.KeyMap[ImGuiKey_End] = (int)KeyCode::KEY_END;
		io.KeyMap[ImGuiKey_Delete] = (int)KeyCode::KEY_DELETE;
		io.KeyMap[ImGuiKey_Backspace] = (int)KeyCode::KEY_BACKSPACE;
		io.KeyMap[ImGuiKey_Enter] = (int)KeyCode::KEY_ENTER;
		io.KeyMap[ImGuiKey_Escape] = (int)KeyCode::KEY_ESCAPE;
		io.KeyMap[ImGuiKey_A] = (int)KeyCode::KEY_A;
		io.KeyMap[ImGuiKey_C] = (int)KeyCode::KEY_C;
		io.KeyMap[ImGuiKey_V] = (int)KeyCode::KEY_V;
		io.KeyMap[ImGuiKey_X] = (int)KeyCode::KEY_X;
		io.KeyMap[ImGuiKey_Y] = (int)KeyCode::KEY_Y;
		io.KeyMap[ImGuiKey_Z] = (int)KeyCode::KEY_Z;

		// TODO: Figure out how to use RESOURCE_LOCATION constant here
		io.IniFilename = "FlexEngine/resources/imgui.ini";
	}

	InputManager::~InputManager()
	{
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

	int InputManager::GetKeyDown(KeyCode keyCode) const
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
		m_MousePosition = glm::vec2((float)x, (float)y);

		ImGuiIO& io = ImGui::GetIO();
		io.MousePos = m_MousePosition;
	}

	void InputManager::MouseButtonCallback(const GameContext& gameContext, MouseButton button, Action action, int mods)
	{
		UNREFERENCED_PARAMETER(gameContext);
		UNREFERENCED_PARAMETER(mods);

		assert((int)button < MOUSE_BUTTON_COUNT);

		if (action == Action::PRESS)
		{
			++m_MouseButtons[(int)button].down;

			//if (button == MouseButton::LEFT)
			//{
			//	gameContext.window->SetCursorMode(Window::CursorMode::HIDDEN);
			//}
		}
		else
		{
			m_MouseButtons[(int)button].down = 0;

			//if (button == MouseButton::LEFT)
			//{
			//	gameContext.window->SetCursorMode(Window::CursorMode::NORMAL);
			//}
		}

		ImGuiIO& io = ImGui::GetIO();
		io.MouseDown[(int)button] = m_MouseButtons[(int)button].down > 0;
		io.MouseClicked[(int)button] = m_MouseButtons[(int)button].down == 1;
	}

	void InputManager::ScrollCallback(double xOffset, double yOffset)
	{
		m_ScrollXOffset = (float)xOffset;
		m_ScrollYOffset = (float)yOffset;

		ImGuiIO& io = ImGui::GetIO();
		io.MouseWheel = m_ScrollYOffset;
	}

	void InputManager::KeyCallback(KeyCode keycode, Action action, int mods)
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
		io.KeysDown[(int)keycode] = m_Keys[keycode].down > 0;

		io.KeyCtrl = GetKeyDown(KeyCode::KEY_LEFT_CONTROL) || GetKeyDown(KeyCode::KEY_RIGHT_CONTROL);
		io.KeyShift = GetKeyDown(KeyCode::KEY_LEFT_SHIFT) || GetKeyDown(KeyCode::KEY_RIGHT_SHIFT);
		io.KeyAlt = GetKeyDown(KeyCode::KEY_LEFT_ALT) || GetKeyDown(KeyCode::KEY_RIGHT_ALT);
		io.KeySuper = GetKeyDown(KeyCode::KEY_LEFT_SUPER) || GetKeyDown(KeyCode::KEY_RIGHT_SUPER);
	}

	void InputManager::CharCallback(unsigned int character)
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

	int InputManager::GetMouseButtonDown(MouseButton button) const
	{
		assert((int)button >= 0 && (int)button <= MOUSE_BUTTON_COUNT - 1);

		return m_MouseButtons[(int)button].down;
	}

	bool InputManager::GetMouseButtonClicked(MouseButton button) const
	{
		assert((int)button >= 0 && (int)button <= MOUSE_BUTTON_COUNT - 1);

		return m_MouseButtons[(int)button].down == 1;
	}

	float InputManager::GetVerticalScrollDistance() const
	{
		return m_ScrollYOffset;
	}

	void InputManager::ClearAllInputs(const GameContext& gameContext)
	{
		ClearMouseInput(gameContext);
		ClearKeyboadInput(gameContext);
	}

	void InputManager::ClearMouseInput(const GameContext& gameContext)
	{
		m_MousePosition = {};
		m_PrevMousePosition = m_MousePosition;

		for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i)
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
