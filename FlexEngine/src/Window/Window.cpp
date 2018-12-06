#include "stdafx.hpp"

#include "Window/Window.hpp"

#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/SceneManager.hpp"
#include "Time.hpp"
#include "Window/Monitor.hpp"

namespace flex
{
	std::string Window::s_ConfigFilePath = RESOURCE_LOCATION + "config/window-settings.ini";

	Window::Window(const std::string& title) :
		m_TitleString(title),
		m_CurrentWindowMode(WindowMode::WINDOWED)
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
			const glm::vec2i windowSize = GetSize();
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
		return m_bHasFocus;
	}

	std::string Window::GenerateWindowTitle()
	{
		ImGuiIO& io = ImGui::GetIO();
		std::string result = m_TitleString;
		result += " | " + g_SceneManager->CurrentScene()->GetName();
		if (m_bShowMSInWindowTitle)
		{
			result += " | " + Time::MillisecondsToString(g_DeltaTime, 2);
		}
		if (m_bShowFPSInWindowTitle)
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

	WindowMode Window::GetWindowMode()
	{
		return m_CurrentWindowMode;
	}

	const char* Window::WindowModeToStr(WindowMode mode)
	{
		assert(((i32)mode) >= 0);
		assert(((i32)mode) < ARRAY_SIZE(WindowModeStrs));

		return WindowModeStrs[(i32)mode];
	}

	WindowMode Window::StrToWindowMode(const char* modeStr)
	{
		for (i32 i = 0; i < ARRAY_SIZE(WindowModeStrs); ++i)
		{
			if (strcmp(WindowModeStrs[i], modeStr) == 0)
			{
				return (WindowMode)i;
			}
		}

		PrintError("Unhandled window mode passed to StrToWindowMode: %s, returning WindowMode::WINDOWED\n", modeStr);

		return WindowMode::WINDOWED;
	}

	// Callbacks
	void Window::KeyCallback(Input::KeyCode keycode, Input::KeyAction action, i32 mods)
	{
		g_InputManager->KeyCallback(keycode, action, mods);
	}

	void Window::CharCallback(u32 character)
	{
		g_InputManager->CharCallback(character);
	}

	void Window::MouseButtonCallback(Input::MouseButton mouseButton, Input::KeyAction action, i32 mods)
	{
		g_InputManager->MouseButtonCallback(mouseButton, action, mods);
	}

	void Window::WindowFocusCallback(i32 focused)
	{
		m_bHasFocus = (focused != 0);
	}

	void Window::CursorPosCallback(double x, double y)
	{
		g_InputManager->CursorPosCallback(x, y);
	}

	void Window::ScrollCallback(double xoffset, double yoffset)
	{
		g_InputManager->ScrollCallback(xoffset, yoffset);
	}

	void Window::WindowSizeCallback(i32 width, i32 height, bool bMaximized, bool bIconified)
	{
		m_bMaximized = bMaximized;
		m_bIconified = bIconified;

		OnSizeChanged(width, height);

		if (m_CurrentWindowMode == WindowMode::WINDOWED)
		{
			m_LastWindowedSize = glm::vec2i(width, height);
		}
	}

	void Window::WindowPosCallback(i32 newX, i32 newY)
	{
		OnPositionChanged(newX, newY);

		if (m_CurrentWindowMode == WindowMode::WINDOWED)
		{
			m_LastWindowedPos = m_Position;
		}
	}

	void Window::FrameBufferSizeCallback(i32 width, i32 height)
	{
		SetFrameBufferSize(width, height);
	}

	bool Window::IsMaximized() const
	{
		return m_bMaximized;
	}

	bool Window::IsIconified() const
	{
		return m_bIconified;
	}

	bool Window::InitFromConfig()
	{
		if (FileExists(s_ConfigFilePath))
		{
			JSONObject rootObject = {};

			if (JSONParser::Parse(s_ConfigFilePath, rootObject))
			{
				bool bMoveConsole;
				if (rootObject.SetBoolChecked("move console to other monitor on bootup", bMoveConsole))
				{
					m_bMoveConsoleToOtherMonitor = bMoveConsole;
				}

				bool bAutoRestore;
				if (rootObject.SetBoolChecked("auto restore state", bAutoRestore))
				{
					m_bAutoRestoreStateOnBootup = bAutoRestore;
				}

				if (m_bAutoRestoreStateOnBootup)
				{
					glm::vec2 initialWindowPos;
					if (rootObject.SetVec2Checked("initial window position", initialWindowPos))
					{
						m_Position = (glm::vec2i)initialWindowPos;
					}

					glm::vec2 initialWindowSize;
					if (rootObject.SetVec2Checked("initial window size", initialWindowSize))
					{
						m_Size = (glm::vec2i)initialWindowSize;
					}

					bool bMaximized;
					if (rootObject.SetBoolChecked("maximized", bMaximized))
					{
						m_bMaximized = bMaximized;
					}

					std::string windowModeStr;
					if (rootObject.SetStringChecked("window mode", windowModeStr))
					{
						m_CurrentWindowMode = StrToWindowMode(windowModeStr.c_str());
					}
				}

				return true;
			}
			else
			{
				PrintError("Failed to parse window settings config file\n");
			}
		}

		return false;
	}

	void Window::SaveToConfig()
	{
		JSONObject rootObject = {};

		rootObject.fields.emplace_back("move console to other monitor on bootup", JSONValue(m_bMoveConsoleToOtherMonitor));
		rootObject.fields.emplace_back("auto restore state", JSONValue(m_bAutoRestoreStateOnBootup));
		rootObject.fields.emplace_back("initial window position", JSONValue(Vec2ToString((glm::vec2)m_Position, 0)));
		rootObject.fields.emplace_back("initial window size", JSONValue(Vec2ToString((glm::vec2)m_Size, 0)));
		rootObject.fields.emplace_back("maximized", JSONValue(m_bMaximized));
		const char* windowModeStr = Window::WindowModeToStr(GetWindowMode());
		rootObject.fields.emplace_back("window mode", JSONValue(windowModeStr));
		std::string fileContents = rootObject.Print(0);

		if (!WriteFile(s_ConfigFilePath, fileContents, false))
		{
			PrintError("Failed to write window settings config file\n");
		}
	}

	void Window::DrawImGuiObjects()
	{
		static const char* windowSettingsStr = "Window settings";
		if (ImGui::TreeNode(windowSettingsStr))
		{
			if (ImGui::Checkbox("Auto restore state", &m_bAutoRestoreStateOnBootup))
			{
				g_Renderer->SaveSettingsToDisk(false, true);
			}

			if (ImGui::DragInt2("Position", &m_Position.x, 1.0f))
			{
				SetPosition(m_Position.x, m_Position.y);
			}

			if (ImGui::Button("Center"))
			{
				SetPosition(g_Monitor->width / 2 - m_Size.x / 2,
					g_Monitor->height / 2 - m_Size.y / 2);
			}

			ImGui::NewLine();

			if (ImGui::DragInt2("Size", &m_Size.x, 1.0f, 100, 3840))
			{
				SetSize(m_Size.x, m_Size.y);
			}

			bool bWindowWasMaximized = IsMaximized();
			if (bWindowWasMaximized)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			}
			if (ImGui::Button("Maximize"))
			{
				Maximize();
			}
			if (bWindowWasMaximized)
			{
				ImGui::PopStyleColor();
			}

			ImGui::SameLine();

			if (ImGui::Button("Iconify"))
			{
				Iconify();
			}

			if (ImGui::Button("1920x1080"))
			{
				SetSize(1920, 1080);
			}

			ImGui::SameLine();

			if (ImGui::Button("1600x900"))
			{
				SetSize(1600, 900);
			}

			ImGui::SameLine();

			if (ImGui::Button("1280x720"))
			{
				SetSize(1280, 720);
			}

			ImGui::SameLine();

			if (ImGui::Button("800x600"))
			{
				SetSize(800, 600);
			}

			ImGui::TreePop();
		}
	}
} // namespace flex
