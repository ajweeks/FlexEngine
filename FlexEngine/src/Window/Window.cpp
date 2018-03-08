#include "stdafx.hpp"

#include "Window/Window.hpp"

#include "imgui.h"

#include "Helpers.hpp"
#include "Logger.hpp"
#include "Time.hpp"

namespace flex
{
	Window::Window(const std::string& title, glm::vec2 size, glm::vec2 startingPos, GameContext& gameContext) :
		m_TitleString(title),
		m_Size(size),
		m_StartingPosition(startingPos),
		m_Position(startingPos),
		m_FrameBufferSize(size),
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


	glm::vec2i Window::GetPosition() const
	{
		return m_Position;
	}	
	
	glm::vec2i Window::GetFrameBufferSize() const
	{
		return m_FrameBufferSize;
	}

	bool Window::HasFocus() const
	{
		return m_HasFocus;
	}

	std::string Window::GenerateWindowTitle(real dt)
	{
		ImGuiIO& io = ImGui::GetIO();
		std::string result = m_TitleString;
		if (m_ShowMSInWindowTitle) result += "   " + Time::MillisecondsToString(dt, 2);
		if (m_ShowFPSInWindowTitle) result += +" | " + FloatToString(io.Framerate, 0) + " FPS "; // Use ImGui's more stable FPS rolling average
		//if (m_ShowFPSInWindowTitle) result += +" | " + FloatToString(1.0f / dt, 0) + " FPS ";

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

	void Window::SetUpdateWindowTitleFrequency(real updateFrequencyinSeconds)
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
	void Window::KeyCallback(InputManager::KeyCode keycode, InputManager::Action action, i32 mods)
	{
		m_GameContextRef.inputManager->KeyCallback(keycode, action, mods);
	}

	void Window::CharCallback(u32 character)
	{
		m_GameContextRef.inputManager->CharCallback(character);
	}

	void Window::MouseButtonCallback(InputManager::MouseButton mouseButton, InputManager::Action action, i32 mods)
	{
		m_GameContextRef.inputManager->MouseButtonCallback(m_GameContextRef, mouseButton, action, mods);
	}

	void Window::WindowFocusCallback(i32 focused)
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

	void Window::WindowSizeCallback(i32 width, i32 height)
	{
		SetSize(width, height);
	}

	void Window::WindowPosCallback(i32 newX, i32 newY)
	{
		m_Position = { newX, newY };
	}

	void Window::FrameBufferSizeCallback(i32 width, i32 height)
	{
		SetFrameBufferSize(width, height);
	}
} // namespace flex
