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
	Window::Window(const std::string& title) :
		m_TitleString(title),
		m_CurrentWindowMode(WindowMode::WINDOWED),
		m_LastWindowedSize(glm::vec2i(0)),
		m_LastWindowedPos(glm::vec2i(0))
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
		std::string result = m_TitleString;
		result += " | " + g_SceneManager->CurrentScene()->GetName();
		if (m_bShowMSInWindowTitle)
		{
			result += " | " + FloatToString(g_DeltaTime * 1000.0f, 2) + "ms";
		}
		if (m_bShowFPSInWindowTitle)
		{
			result += " - " + FloatToString(1.0f / g_DeltaTime, 0) + " FPS ";
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
		return WindowModeStrings[(i32)mode];
	}

	WindowMode Window::StrToWindowMode(const char* modeStr)
	{
		for (i32 i = 0; i < (i32)WindowMode::_NONE; ++i)
		{
			if (strcmp(WindowModeStrings[i], modeStr) == 0)
			{
				return (WindowMode)i;
			}
		}

		PrintError("Unhandled window mode passed to StrToWindowMode: %s, returning WindowMode::WINDOWED\n", modeStr);

		return WindowMode::WINDOWED;
	}

	void Window::KeyCallback(KeyCode keycode, KeyAction action, i32 mods)
	{
		g_InputManager->KeyCallback(keycode, action, mods);
	}

	void Window::CharCallback(u32 character)
	{
		g_InputManager->CharCallback(character);
	}

	void Window::MouseButtonCallback(MouseButton mouseButton, KeyAction action, i32 mods)
	{
		g_InputManager->MouseButtonCallback(mouseButton, action, mods);
	}

	void Window::WindowFocusCallback(i32 focused)
	{
		m_bHasFocus = (focused != 0);
		g_InputManager->OnWindowFocusChanged(m_bHasFocus);
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

		if (ImGui::GetCurrentContext() != nullptr)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2((real)width, (real)height);
		}

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
		if (FileExists(WINDOW_CONFIG_LOCATION))
		{
			JSONObject rootObject = {};

			if (JSONParser::ParseFromFile(WINDOW_CONFIG_LOCATION, rootObject))
			{
				rootObject.SetBoolChecked("move console to other monitor on bootup", m_bMoveConsoleToOtherMonitor);
				rootObject.SetBoolChecked("auto restore state", m_bAutoRestoreStateOnBootup);

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

					rootObject.SetBoolChecked("maximized", m_bMaximized);

					std::string windowModeStr;
					if (rootObject.SetStringChecked("window mode", windowModeStr))
					{
						m_CurrentWindowMode = StrToWindowMode(windowModeStr.c_str());
					}
				}

				bool bVSyncEnabled;
				if (rootObject.SetBoolChecked("v-sync", bVSyncEnabled))
				{
					SetVSyncEnabled(bVSyncEnabled);
				}

				return true;
			}
			else
			{
				PrintError("Failed to parse window settings config file %s\n\terror: %s\n", WINDOW_CONFIG_LOCATION, JSONParser::GetErrorString());
			}
		}

		return false;
	}

	void Window::SaveToConfig()
	{
		JSONObject rootObject = {};

		rootObject.fields.emplace_back("move console to other monitor on bootup", JSONValue(m_bMoveConsoleToOtherMonitor));
		rootObject.fields.emplace_back("auto restore state", JSONValue(m_bAutoRestoreStateOnBootup));
		rootObject.fields.emplace_back("initial window position", JSONValue(VecToString((glm::vec2)m_Position, 0)));
		rootObject.fields.emplace_back("initial window size", JSONValue(VecToString((glm::vec2)m_Size, 0)));
		rootObject.fields.emplace_back("maximized", JSONValue(m_bMaximized));
		const char* windowModeStr = Window::WindowModeToStr(GetWindowMode());
		rootObject.fields.emplace_back("window mode", JSONValue(windowModeStr));
		rootObject.fields.emplace_back("v-sync", JSONValue(m_bVSyncEnabled));
		std::string fileContents = rootObject.Print(0);

		if (!WriteFile(WINDOW_CONFIG_LOCATION, fileContents, false))
		{
			PrintError("Failed to write window settings config file\n");
		}
	}

	void Window::DrawImGuiObjects()
	{
		if (ImGui::TreeNode("Window settings"))
		{
			if (ImGui::Checkbox("Auto restore state", &m_bAutoRestoreStateOnBootup))
			{
				g_Renderer->SaveSettingsToDisk(true);
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

	bool Window::GetVSyncEnabled() const
	{
		return m_bVSyncEnabled;
	}

	void Window::SetVSyncEnabled(bool bEnabled)
	{
		if (m_bVSyncEnabled != bEnabled)
		{
			m_bVSyncEnabled = bEnabled;
			if (g_Renderer)
			{
				g_Renderer->SetVSyncEnabled(bEnabled);
			}
		}
	}

	CursorMode Window::GetCursorMode() const
	{
		return m_CursorMode;
	}

} // namespace flex
