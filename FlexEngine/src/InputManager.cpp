#include "stdafx.h"

#include "InputManager.h"

#include <assert.h>

#include "Logger.h"
#include "Window/Window.h"

namespace flex
{
	InputManager::InputManager()
	{
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
		m_MousePosition = glm::vec2(x, y);
	}

	void InputManager::MouseButtonCallback(GameContext& gameContext, MouseButton button, Action action, int mods)
	{
		UNREFERENCED_PARAMETER(mods);

		assert((int)button < MOUSE_BUTTON_COUNT);

		if (action == Action::PRESS)
		{
			++m_MouseButtons[(int)button].down;

			if (button == MouseButton::LEFT)
			{
				gameContext.window->SetCursorMode(Window::CursorMode::HIDDEN);
			}
		}
		else
		{
			m_MouseButtons[(int)button].down = 0;

			if (button == MouseButton::LEFT)
			{
				gameContext.window->SetCursorMode(Window::CursorMode::NORMAL);
			}
		}
	}

	void InputManager::ScrollCallback(double xOffset, double yOffset)
	{
		m_ScrollXOffset = (float)xOffset;
		m_ScrollYOffset = (float)yOffset;
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
		for (auto iter = m_Keys.begin(); iter != m_Keys.end(); ++iter)
		{
			iter->second.down = 0;
		}

		for (size_t i = 0; i < MOUSE_BUTTON_COUNT; ++i)
		{
			m_MouseButtons[i].down = 0;
		}

		m_MousePosition = {};
		m_PrevMousePosition = m_MousePosition;

		gameContext.window->SetCursorMode(Window::CursorMode::NORMAL);
	}
} // namespace flex
