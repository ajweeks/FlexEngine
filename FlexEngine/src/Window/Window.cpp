#include "stdafx.h"

#include "Window/Window.h"

#include "Helpers.h"
#include "Logger.h"

namespace flex
{
	Window::Window(const std::string& title, glm::vec2 size, GameContext& gameContext) :
		m_TitleString(title),
		m_Size(size),
		m_ShowFPSInWindowTitle(true),
		m_ShowMSInWindowTitle(true),
		m_GameContextRef(gameContext),
		m_UpdateWindowTitleFrequency(0.0f),
		m_SecondsSinceTitleUpdate(0.0f)
	{
		gameContext.window = this;
	}

	Window::~Window()
	{
	}

	void Window::Update(const GameContext& gameContext)
	{
		m_GameContextRef = gameContext;

		m_SecondsSinceTitleUpdate += gameContext.deltaTime;
		if (m_SecondsSinceTitleUpdate >= m_UpdateWindowTitleFrequency)
		{
			m_SecondsSinceTitleUpdate = 0.0f;
			SetWindowTitle(GenerateWindowTitle(gameContext.deltaTime));
		}

		if (m_CursorMode == CursorMode::HIDDEN)
		{
			const glm::vec2i windowSize = gameContext.window->GetSize();
			const glm::vec2 oldMousePos = gameContext.inputManager->GetMousePosition();
			glm::vec2 newMousePos = oldMousePos;
			if (oldMousePos.x < 0)
			{
				newMousePos.x = windowSize.x + oldMousePos.x;
			}
			else if (oldMousePos.x >= windowSize.x)
			{
				newMousePos.x = 0;
			}

			if (oldMousePos.y < 0)
			{
				newMousePos.y = windowSize.y + oldMousePos.y;
			}
			else if (oldMousePos.y >= windowSize.y)
			{
				newMousePos.y = 0;
			}

			gameContext.inputManager->SetMousePosition(newMousePos);
			SetMousePosition(newMousePos);
		}
	}

	glm::vec2i Window::GetSize() const
	{
		return m_Size;
	}

	bool Window::HasFocus() const
	{
		return m_HasFocus;
	}

	void Window::SetTitleString(const std::string& title)
	{
		m_TitleString = title;
	}

	std::string Window::GenerateWindowTitle(float dt)
	{
		std::string result = m_TitleString;
		if (m_ShowMSInWindowTitle) result += "   " + FloatToString(dt, 3) + " ms";
		if (m_ShowFPSInWindowTitle) result += +" | " + FloatToString(1.0f / dt, 0) + " FPS ";
		return result;
	}

	void Window::SetShowFPSInTitleBar(bool showFPS)
	{
		m_ShowFPSInWindowTitle = showFPS;
	}

	void Window::SetShowMSInTitleBar(bool showMS)
	{
		m_ShowMSInWindowTitle = showMS;
	}

	void Window::SetUpdateWindowTitleFrequency(float updateFrequencyinSeconds)
	{
		m_UpdateWindowTitleFrequency = updateFrequencyinSeconds;
	}

	std::string Window::GetTitle() const
	{
		return m_TitleString;
	}

	void Window::SetCursorMode(CursorMode mode)
	{
		m_CursorMode = mode;
	}


	// Callbacks
	void Window::KeyCallback(InputManager::KeyCode keycode, InputManager::Action action, int mods)
	{
		m_GameContextRef.inputManager->KeyCallback(keycode, action, mods);
	}

	void Window::MouseButtonCallback(InputManager::MouseButton mouseButton, InputManager::Action action, int mods)
	{
		m_GameContextRef.inputManager->MouseButtonCallback(m_GameContextRef, mouseButton, action, mods);
	}

	void Window::WindowFocusCallback(int focused)
	{
		m_HasFocus = focused != 0;
	}

	void Window::CursorPosCallback(double x, double y)
	{
		m_GameContextRef.inputManager->CursorPosCallback(x, y);
	}

	void Window::ScrollCallback(double xoffset, double yoffset)
	{
		m_GameContextRef.inputManager->ScrollCallback(xoffset, yoffset);
	}

	void Window::WindowSizeCallback(int width, int height)
	{
		SetSize(width, height);
	}
} // namespace flex
