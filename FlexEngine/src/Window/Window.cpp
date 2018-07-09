#include "stdafx.hpp"

#include "Window/Window.hpp"

#pragma warning(push, 0)
#include "imgui.h"
#pragma warning(pop)

#include "Helpers.hpp"
#include "Time.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	Window::Window(const std::string& title) :
		m_TitleString(title),
		m_ShowFPSInWindowTitle(true),
		m_ShowMSInWindowTitle(true),
		m_UpdateWindowTitleFrequency(0.0f),
		m_SecondsSinceTitleUpdate(0.0f),
		m_CurrentFullscreenMode(FullscreenMode::WINDOWED)
	{
		g_Window = this;
	}

	Window::~Window()
	{
	}

	void Window::Update()
	{
		m_SecondsSinceTitleUpdate += g_DeltaTime;
		if (m_SecondsSinceTitleUpdate >= m_UpdateWindowTitleFrequency)
		{
			m_SecondsSinceTitleUpdate = 0.0f;
			SetWindowTitle(GenerateWindowTitle());
		}

		if (m_CursorMode == CursorMode::HIDDEN)
		{
			const glm::vec2i windowSize = g_Window->GetSize();
			const glm::vec2 oldMousePos = g_InputManager->GetMousePosition();
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

			g_InputManager->SetMousePosition(newMousePos);
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

	std::string Window::GenerateWindowTitle()
	{
		ImGuiIO& io = ImGui::GetIO();
		std::string result = m_TitleString;
		result += " | " + g_SceneManager->CurrentScene()->GetName();
		if (m_ShowMSInWindowTitle)
		{
			result += " | " + Time::MillisecondsToString(g_DeltaTime, 2);
		}
		if (m_ShowFPSInWindowTitle)
		{
			result += +" : " + FloatToString(io.Framerate, 0) + " FPS "; // Use ImGui's more stable FPS rolling average
		}
		

		return result;
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

	Window::FullscreenMode Window::GetFullscreenMode()
	{
		return m_CurrentFullscreenMode;
	}

	// Callbacks
	void Window::KeyCallback(InputManager::KeyCode keycode, InputManager::Action action, i32 mods)
	{
		g_InputManager->KeyCallback(keycode, action, mods);
	}

	void Window::CharCallback(u32 character)
	{
		g_InputManager->CharCallback(character);
	}

	void Window::MouseButtonCallback(InputManager::MouseButton mouseButton, InputManager::Action action, i32 mods)
	{
		g_InputManager->MouseButtonCallback(mouseButton, action, mods);
	}

	void Window::WindowFocusCallback(i32 focused)
	{
		m_HasFocus = (focused != 0);
	}

	void Window::CursorPosCallback(double x, double y)
	{
		g_InputManager->CursorPosCallback(x, y);
	}

	void Window::ScrollCallback(double xoffset, double yoffset)
	{
		g_InputManager->ScrollCallback(xoffset, yoffset);
	}

	void Window::WindowSizeCallback(i32 width, i32 height)
	{
		OnSizeChanged(width, height);

		if (m_CurrentFullscreenMode == FullscreenMode::WINDOWED)
		{
			m_LastWindowedSize = glm::vec2i(width, height);
		}
	}

	void Window::WindowPosCallback(i32 newX, i32 newY)
	{
		OnPositionChanged(newX, newY);

		if (m_CurrentFullscreenMode == FullscreenMode::WINDOWED)
		{
			m_LastWindowedPos = m_Position;
		}
	}

	void Window::FrameBufferSizeCallback(i32 width, i32 height)
	{
		SetFrameBufferSize(width, height);
	}
} // namespace flex
