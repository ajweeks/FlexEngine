#include "stdafx.hpp"
#if COMPILE_OPEN_GL || COMPILE_VULKAN

#include "Window/GLFWWindowWrapper.hpp"

#include "FlexEngine.hpp"
#include "Graphics/GL/GLHelpers.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Window/Monitor.hpp"

namespace flex
{
	std::array<bool, MAX_JOYSTICK_COUNT> g_JoysticksConnected;

	GLFWWindowWrapper::GLFWWindowWrapper(std::string title) :
		Window(title)
	{
		m_LastNonFullscreenWindowMode = WindowMode::WINDOWED;

		m_WindowIcons.push_back(LoadGLFWimage(RESOURCE_LOCATION + "icons/flex-logo-03_128.png", 4));
		m_WindowIcons.push_back(LoadGLFWimage(RESOURCE_LOCATION + "icons/flex-logo-03_64.png", 4));
		m_WindowIcons.push_back(LoadGLFWimage(RESOURCE_LOCATION + "icons/flex-logo-03_48.png", 4));
		m_WindowIcons.push_back(LoadGLFWimage(RESOURCE_LOCATION + "icons/flex-logo-03_32.png", 4));
		m_WindowIcons.push_back(LoadGLFWimage(RESOURCE_LOCATION + "icons/flex-logo-03_16.png", 4));
	}

	GLFWWindowWrapper::~GLFWWindowWrapper()
	{
	}

	void GLFWWindowWrapper::Initialize()
	{
		glfwSetErrorCallback(GLFWErrorCallback);

		if (!glfwInit())
		{
			PrintError("Failed to initialize glfw! Exiting...\n");
			exit(EXIT_FAILURE);
		}

		i32 numJoysticksConnected = 0;
		for (i32 i = 0; i < MAX_JOYSTICK_COUNT; ++i)
		{
			g_JoysticksConnected[i] = (glfwJoystickPresent(i) == GLFW_TRUE);
			if (g_JoysticksConnected[i])
			{
				++numJoysticksConnected;
			}
		}

		if (numJoysticksConnected > 0)
		{
			Print("%i joysticks connected on bootup\n", numJoysticksConnected);
		}

		// TODO: Look into supporting system-DPI awareness
		//SetProcessDPIAware();
	}

	void GLFWWindowWrapper::PostInitialize()
	{
		// TODO: Set window location/size based on previous session (load from disk)
		glfwGetWindowSize(m_Window, &m_LastWindowedSize.x, &m_LastWindowedSize.y);
		glfwGetWindowPos(m_Window, &m_LastWindowedPos.x, &m_LastWindowedPos.y);
	}

	void GLFWWindowWrapper::Destroy()
	{
		for (GLFWimage& icon : m_WindowIcons)
		{
			SafeDelete(icon.pixels);
		}
		m_WindowIcons.clear();

		if (m_Window)
		{
			m_Window = nullptr;
		}
	}

	void GLFWWindowWrapper::RetrieveMonitorInfo()
	{
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		if (!monitor)
		{
			PrintError("Failed to find primary monitor!\n");
			return;
		}

		const GLFWvidmode* vidMode = glfwGetVideoMode(monitor);
		if (!vidMode)
		{
			PrintError("Failed to get monitor's video mode!\n");
			return;
		}

		assert(g_Monitor); // Monitor must be created before calling RetrieveMonitorInfo!
		g_Monitor->width = vidMode->width;
		g_Monitor->height = vidMode->height;
		g_Monitor->redBits = vidMode->redBits;
		g_Monitor->greenBits = vidMode->greenBits;
		g_Monitor->blueBits = vidMode->blueBits;
		g_Monitor->refreshRate = vidMode->refreshRate;
		
		// 25.4mm = 1 inch
		i32 widthMM, heightMM;
		glfwGetMonitorPhysicalSize(monitor, &widthMM, &heightMM);
		g_Monitor->DPI = glm::vec2(vidMode->width / (widthMM / 25.4f),
											vidMode->height / (heightMM / 25.4f));

		glfwGetMonitorContentScale(monitor, &g_Monitor->contentScaleX, &g_Monitor->contentScaleY);
	}

	void GLFWWindowWrapper::SetUpCallbacks()
	{
		if (!m_Window)
		{
			PrintError("SetUpCallbacks was called before m_Window was set!\n");
			return;
		}

		glfwSetKeyCallback(m_Window, GLFWKeyCallback);
		glfwSetCharCallback(m_Window, GLFWCharCallback);
		glfwSetMouseButtonCallback(m_Window, GLFWMouseButtonCallback);
		glfwSetCursorPosCallback(m_Window, GLFWCursorPosCallback);
		glfwSetScrollCallback(m_Window, GLFWScrollCallback);
		glfwSetWindowSizeCallback(m_Window, GLFWWindowSizeCallback);
		glfwSetFramebufferSizeCallback(m_Window, GLFWFramebufferSizeCallback);
		glfwSetWindowFocusCallback(m_Window, GLFWWindowFocusCallback);
		glfwSetWindowPosCallback(m_Window, GLFWWindowPosCallback);
		glfwSetJoystickCallback(GLFWJoystickCallback);
	}

	void GLFWWindowWrapper::SetFrameBufferSize(i32 width, i32 height)
	{
		m_FrameBufferSize = glm::vec2i(width, height);
		m_Size = m_FrameBufferSize;
		
		if (g_Renderer)
		{
			g_Renderer->OnWindowSizeChanged(width, height);
		}
	}

	void GLFWWindowWrapper::SetSize(i32 width, i32 height)
	{
		glfwSetWindowSize(m_Window, width, height);
		
		OnSizeChanged(width, height);
	}

	void GLFWWindowWrapper::OnSizeChanged(i32 width, i32 height)
	{
		m_Size = glm::vec2i(width, height);
		m_FrameBufferSize = m_Size;
		if (m_CurrentWindowMode == WindowMode::WINDOWED)
		{
			m_LastWindowedSize = m_Size;
		}

		if (g_Renderer)
		{
			g_Renderer->OnWindowSizeChanged(width, height);
		}
	}

	void GLFWWindowWrapper::SetPosition(i32 newX, i32 newY)
	{
		if (m_Window)
		{
			glfwSetWindowPos(m_Window, newX, newY);
		}
		else
		{
			m_StartingPosition = { newX, newY };
		}

		OnPositionChanged(newX, newY);
	}

	void GLFWWindowWrapper::OnPositionChanged(i32 newX, i32 newY)
	{
		m_Position = { newX, newY };

		if (m_CurrentWindowMode == WindowMode::WINDOWED)
		{
			m_LastWindowedPos = m_Position;
		}
	}

	void GLFWWindowWrapper::PollEvents()
	{
		glfwPollEvents();
	}

	void GLFWWindowWrapper::SetCursorPos(const glm::vec2& newCursorPos)
	{
		glfwSetCursorPos(m_Window, newCursorPos.x, newCursorPos.y);
	}

	void GLFWWindowWrapper::SetCursorMode(CursorMode mode)
	{
		Window::SetCursorMode(mode);

		i32 glfwCursorMode = 0;

		switch (mode)
		{
		case CursorMode::NORMAL: glfwCursorMode = GLFW_CURSOR_NORMAL; break;
		case CursorMode::HIDDEN: glfwCursorMode = GLFW_CURSOR_HIDDEN; break;
		case CursorMode::DISABLED: glfwCursorMode = GLFW_CURSOR_DISABLED; break;
		default: PrintError("Unhandled cursor mode passed to GLFWWindowWrapper::SetCursorMode: %i\n", (i32)mode); break;
		}

		glfwSetInputMode(m_Window, GLFW_CURSOR, glfwCursorMode);
	}

	void GLFWWindowWrapper::SetWindowMode(WindowMode mode, bool force)
	{
		if (force || m_CurrentWindowMode != mode)
		{
			m_CurrentWindowMode = mode;

			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			if (!monitor)
			{
				PrintError("Failed to find primary monitor! Can't set window mode\n");
				return;
			}

			const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
			if (!videoMode)
			{
				PrintError("Failed to get monitor's video mode! Can't set window mode\n");
				return;
			}

			switch (mode)
			{
			case WindowMode::FULLSCREEN:
			{
				glfwSetWindowMonitor(m_Window, monitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
			} break;
			case WindowMode::WINDOWED_FULLSCREEN:
			{
				glfwSetWindowMonitor(m_Window, monitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
				m_LastNonFullscreenWindowMode = WindowMode::WINDOWED_FULLSCREEN;
			} break;
			case WindowMode::WINDOWED:
			{
				assert(m_LastWindowedSize.x != 0 && m_LastWindowedSize.y != 0);

				if (m_LastWindowedPos.y == 0)
				{
					// When in windowed mode a y position of 0 means the title bar isn't
					// visible. This will occur if the app launched in fullscreen since
					// the last y position to never have been set to a valid value.
					m_LastWindowedPos.y = 40;
				}

				glfwSetWindowMonitor(m_Window, nullptr, m_LastWindowedPos.x, m_LastWindowedPos.y, m_LastWindowedSize.x, m_LastWindowedSize.y, videoMode->refreshRate);
				m_LastNonFullscreenWindowMode = WindowMode::WINDOWED;
			} break;
			}
		}
	}

	void GLFWWindowWrapper::ToggleFullscreen(bool force)
	{
		if (m_CurrentWindowMode == WindowMode::FULLSCREEN)
		{
			assert(m_LastNonFullscreenWindowMode == WindowMode::WINDOWED || m_LastNonFullscreenWindowMode == WindowMode::WINDOWED_FULLSCREEN);

			SetWindowMode(m_LastNonFullscreenWindowMode, force);
		}
		else
		{
			SetWindowMode(WindowMode::FULLSCREEN, force);
		}
	}

	void GLFWWindowWrapper::Maximize()
	{
		glfwMaximizeWindow(m_Window);
	}

	void GLFWWindowWrapper::Iconify()
	{
		glfwIconifyWindow(m_Window);
	}

	void GLFWWindowWrapper::Update()
	{
		Window::Update();

		if (glfwWindowShouldClose(m_Window))
		{
			g_EngineInstance->Stop();
			return;
		}

		GLFWgamepadstate gamepad0State;
		static bool prevP0JoystickPresent = false;
		if (glfwGetGamepadState(0, &gamepad0State) == GLFW_TRUE)
		{
			g_InputManager->UpdateGamepadState(0, gamepad0State.axes, gamepad0State.buttons);
			prevP0JoystickPresent = true;
		}
		else
		{
			if (prevP0JoystickPresent)
			{
				g_InputManager->ClearGampadInput(0);
				prevP0JoystickPresent = false;
			}
		}

		GLFWgamepadstate gamepad1State;
		static bool prevP1JoystickPresent = false;
		if (glfwGetGamepadState(1, &gamepad1State) == GLFW_TRUE)
		{
			g_InputManager->UpdateGamepadState(1, gamepad1State.axes, gamepad1State.buttons);
			prevP1JoystickPresent = true;
		}
		else
		{
			if (prevP1JoystickPresent)
			{
				g_InputManager->ClearGampadInput(1);
				prevP1JoystickPresent = false;
			}
		}


		// ImGUI
		ImGuiIO& io = ImGui::GetIO();

		// Hide OS mouse cursor if ImGui is drawing it
		glfwSetInputMode(m_Window, GLFW_CURSOR, io.MouseDrawCursor ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
	}

	GLFWwindow* GLFWWindowWrapper::GetWindow() const
	{
		return m_Window;
	}

	const char* GLFWWindowWrapper::GetClipboardText()
	{
		return glfwGetClipboardString(m_Window);
	}

	void GLFWWindowWrapper::SetClipboardText(const char* text)
	{
		glfwSetClipboardString(m_Window, text);
	}

	void GLFWWindowWrapper::SetWindowTitle(const std::string& title)
	{
		glfwSetWindowTitle(m_Window, title.c_str());
	}

	void GLFWWindowWrapper::SetMousePosition(glm::vec2 mousePosition)
	{
		glfwSetCursorPos(m_Window, (double)mousePosition.x, (double)mousePosition.y);
	}

	void GLFWWindowWrapper::MoveConsole()
	{
		HWND hWnd = GetConsoleWindow();
		// TODO: Set these based on display resolution
		i32 consoleWidth = 800;
		i32 consoleHeight = 800;

		// The following four variables store the bounding rectangle of all monitors
		i32 virtualScreenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
		//i32 virtualScreenTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
		i32 virtualScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		//i32 virtualScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

		i32 monitorWidth = GetSystemMetrics(SM_CXSCREEN);
		//i32 monitorHeight = GetSystemMetrics(SM_CYSCREEN);

		// If another monitor is present, move the console to it
		if (virtualScreenWidth > monitorWidth)
		{
			i32 newX;
			i32 newY = 10;

			if (virtualScreenLeft < 0)
			{
				// The other monitor is to the left of the main one
				newX = -(consoleWidth + 10);
			}
			else
			{
				// The other monitor is to the right of the main one
				newX = virtualScreenWidth - monitorWidth + 10;
			}

			MoveWindow(hWnd, newX, newY, consoleWidth, consoleHeight, TRUE);

			// Call again to set size correctly (based on other monitor's DPI)
			MoveWindow(hWnd, newX, newY, consoleWidth, consoleHeight, TRUE);
		}
		else // There's only one monitor, move the console to the top left corner
		{
			RECT rect;
			GetWindowRect(hWnd, &rect);
			if (rect.top != 0)
			{
				// A negative value is needed to line the console up to the left side of my monitor
				MoveWindow(hWnd, -7, 0, consoleWidth, consoleHeight, TRUE);
			}
		}
	}

	void GLFWErrorCallback(i32 error, const char* description)
	{
		PrintError("GLFW Error: %i: %s\n", error, description);
	}

	void GLFWKeyCallback(GLFWwindow* glfwWindow, i32 key, i32 scancode, i32 action, i32 mods)
	{
		UNREFERENCED_PARAMETER(scancode);

		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		const InputManager::Action inputAction = GLFWActionToInputManagerAction(action);
		const InputManager::KeyCode inputKey = GLFWKeyToInputManagerKey(key);
		const i32 inputMods = GLFWModsToInputManagerMods(mods);

		window->KeyCallback(inputKey, inputAction, inputMods);
	}

	void GLFWCharCallback(GLFWwindow* glfwWindow, u32 character)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		window->CharCallback(character);
	}

	void GLFWMouseButtonCallback(GLFWwindow* glfwWindow, i32 button, i32 action, i32 mods)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		const InputManager::Action inputAction = GLFWActionToInputManagerAction(action);
		const i32 inputMods = GLFWModsToInputManagerMods(mods);
		const InputManager::MouseButton mouseButton = GLFWButtonToInputManagerMouseButton(button);

		window->MouseButtonCallback(mouseButton, inputAction, inputMods);
	}

	void GLFWWindowFocusCallback(GLFWwindow* glfwWindow, i32 focused)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));

		//if (window->GetFullscreenMode() != Window::FullscreenMode::WINDOWED)
		//{
		//	if (focused)
		//	{
		//		glfwRestoreWindow(glfwWindow);
		//		Print("found\n");
		//	}
		//	else
		//	{
		//		glfwIconifyWindow(glfwWindow);
		//		Print("lost\n");
		//	}
		//}

		window->WindowFocusCallback(focused);
	}

	void GLFWWindowPosCallback(GLFWwindow* glfwWindow, i32 newX, i32 newY)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		window->WindowPosCallback(newX, newY);
	}

	void GLFWCursorPosCallback(GLFWwindow* glfwWindow, double x, double y)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		window->CursorPosCallback(x, y);
	}

	void GLFWScrollCallback(GLFWwindow* glfwWindow, double xoffset, double yoffset)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		window->ScrollCallback(xoffset, yoffset);
	}

	void GLFWWindowSizeCallback(GLFWwindow* glfwWindow, i32 width, i32 height)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		bool bMaximized = (glfwGetWindowAttrib(glfwWindow, GLFW_MAXIMIZED) == GLFW_TRUE);
		bool bIconified = (glfwGetWindowAttrib(glfwWindow, GLFW_ICONIFIED) == GLFW_TRUE);
		window->WindowSizeCallback(width, height, bMaximized, bIconified);
	}

	void GLFWFramebufferSizeCallback(GLFWwindow* glfwWindow, i32 width, i32 height)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		window->FrameBufferSizeCallback(width, height);
	}

	void GLFWJoystickCallback(i32 JID, i32 event)
	{
		if (JID > MAX_JOYSTICK_COUNT)
		{
			PrintWarn("Unhandled joystick connection event, JID out of range: %i\n", JID);
			return;
		}

		if (event == GLFW_CONNECTED)
		{
			Print("Joystick %i connected\n", JID);
		}
		else if (event == GLFW_DISCONNECTED)
		{
			Print("Joystick %i disconnected\n", JID);
		}

		g_JoysticksConnected[JID] = (event == GLFW_CONNECTED);
	}

	InputManager::Action GLFWActionToInputManagerAction(i32 glfwAction)
	{
		InputManager::Action inputAction = InputManager::Action::_NONE;

		switch (glfwAction)
		{
		case GLFW_PRESS: inputAction = InputManager::Action::PRESS; break;
		case GLFW_REPEAT: inputAction = InputManager::Action::REPEAT; break;
		case GLFW_RELEASE: inputAction = InputManager::Action::RELEASE; break;
		case -1: break; // We don't care about events GLFW can't handle
		default: PrintError("Unhandled glfw action passed to GLFWActionToInputManagerAction in GLFWWIndowWrapper: %i\n", 
							glfwAction);
		}

		return inputAction;
	}

	InputManager::KeyCode GLFWKeyToInputManagerKey(i32 glfwKey)
	{
		InputManager::KeyCode inputKey = InputManager::KeyCode::_NONE;

		switch (glfwKey)
		{
		case GLFW_KEY_SPACE: inputKey = InputManager::KeyCode::KEY_SPACE; break;
		case GLFW_KEY_APOSTROPHE: inputKey = InputManager::KeyCode::KEY_APOSTROPHE; break;
		case GLFW_KEY_COMMA: inputKey = InputManager::KeyCode::KEY_COMMA; break;
		case GLFW_KEY_MINUS: inputKey = InputManager::KeyCode::KEY_MINUS; break;
		case GLFW_KEY_PERIOD: inputKey = InputManager::KeyCode::KEY_PERIOD; break;
		case GLFW_KEY_SLASH: inputKey = InputManager::KeyCode::KEY_SLASH; break;
		case GLFW_KEY_0: inputKey = InputManager::KeyCode::KEY_0; break;
		case GLFW_KEY_1: inputKey = InputManager::KeyCode::KEY_1; break;
		case GLFW_KEY_2: inputKey = InputManager::KeyCode::KEY_2; break;
		case GLFW_KEY_3: inputKey = InputManager::KeyCode::KEY_3; break;
		case GLFW_KEY_4: inputKey = InputManager::KeyCode::KEY_4; break;
		case GLFW_KEY_5: inputKey = InputManager::KeyCode::KEY_5; break;
		case GLFW_KEY_6: inputKey = InputManager::KeyCode::KEY_6; break;
		case GLFW_KEY_7: inputKey = InputManager::KeyCode::KEY_7; break;
		case GLFW_KEY_8: inputKey = InputManager::KeyCode::KEY_8; break;
		case GLFW_KEY_9: inputKey = InputManager::KeyCode::KEY_9; break;
		case GLFW_KEY_SEMICOLON: inputKey = InputManager::KeyCode::KEY_SEMICOLON; break;
		case GLFW_KEY_EQUAL: inputKey = InputManager::KeyCode::KEY_EQUAL; break;
		case GLFW_KEY_A: inputKey = InputManager::KeyCode::KEY_A; break;
		case GLFW_KEY_B: inputKey = InputManager::KeyCode::KEY_B; break;
		case GLFW_KEY_C: inputKey = InputManager::KeyCode::KEY_C; break;
		case GLFW_KEY_D: inputKey = InputManager::KeyCode::KEY_D; break;
		case GLFW_KEY_E: inputKey = InputManager::KeyCode::KEY_E; break;
		case GLFW_KEY_F: inputKey = InputManager::KeyCode::KEY_F; break;
		case GLFW_KEY_G: inputKey = InputManager::KeyCode::KEY_G; break;
		case GLFW_KEY_H: inputKey = InputManager::KeyCode::KEY_H; break;
		case GLFW_KEY_I: inputKey = InputManager::KeyCode::KEY_I; break;
		case GLFW_KEY_J: inputKey = InputManager::KeyCode::KEY_J; break;
		case GLFW_KEY_K: inputKey = InputManager::KeyCode::KEY_K; break;
		case GLFW_KEY_L: inputKey = InputManager::KeyCode::KEY_L; break;
		case GLFW_KEY_M: inputKey = InputManager::KeyCode::KEY_M; break;
		case GLFW_KEY_N: inputKey = InputManager::KeyCode::KEY_N; break;
		case GLFW_KEY_O: inputKey = InputManager::KeyCode::KEY_O; break;
		case GLFW_KEY_P: inputKey = InputManager::KeyCode::KEY_P; break;
		case GLFW_KEY_Q: inputKey = InputManager::KeyCode::KEY_Q; break;
		case GLFW_KEY_R: inputKey = InputManager::KeyCode::KEY_R; break;
		case GLFW_KEY_S: inputKey = InputManager::KeyCode::KEY_S; break;
		case GLFW_KEY_T: inputKey = InputManager::KeyCode::KEY_T; break;
		case GLFW_KEY_U: inputKey = InputManager::KeyCode::KEY_U; break;
		case GLFW_KEY_V: inputKey = InputManager::KeyCode::KEY_V; break;
		case GLFW_KEY_W: inputKey = InputManager::KeyCode::KEY_W; break;
		case GLFW_KEY_X: inputKey = InputManager::KeyCode::KEY_X; break;
		case GLFW_KEY_Y: inputKey = InputManager::KeyCode::KEY_Y; break;
		case GLFW_KEY_Z: inputKey = InputManager::KeyCode::KEY_Z; break;
		case GLFW_KEY_LEFT_BRACKET: inputKey = InputManager::KeyCode::KEY_LEFT_BRACKET; break;
		case GLFW_KEY_BACKSLASH: inputKey = InputManager::KeyCode::KEY_BACKSLASH; break;
		case GLFW_KEY_RIGHT_BRACKET: inputKey = InputManager::KeyCode::KEY_RIGHT_BRACKET; break;
		case GLFW_KEY_GRAVE_ACCENT: inputKey = InputManager::KeyCode::KEY_GRAVE_ACCENT; break;
		case GLFW_KEY_WORLD_1: inputKey = InputManager::KeyCode::KEY_WORLD_1; break;
		case GLFW_KEY_WORLD_2: inputKey = InputManager::KeyCode::KEY_WORLD_2; break;
		case GLFW_KEY_ESCAPE: inputKey = InputManager::KeyCode::KEY_ESCAPE; break;
		case GLFW_KEY_ENTER: inputKey = InputManager::KeyCode::KEY_ENTER; break;
		case GLFW_KEY_TAB: inputKey = InputManager::KeyCode::KEY_TAB; break;
		case GLFW_KEY_BACKSPACE: inputKey = InputManager::KeyCode::KEY_BACKSPACE; break;
		case GLFW_KEY_INSERT: inputKey = InputManager::KeyCode::KEY_INSERT; break;
		case GLFW_KEY_DELETE: inputKey = InputManager::KeyCode::KEY_DELETE; break;
		case GLFW_KEY_RIGHT: inputKey = InputManager::KeyCode::KEY_RIGHT; break;
		case GLFW_KEY_LEFT: inputKey = InputManager::KeyCode::KEY_LEFT; break;
		case GLFW_KEY_DOWN: inputKey = InputManager::KeyCode::KEY_DOWN; break;
		case GLFW_KEY_UP: inputKey = InputManager::KeyCode::KEY_UP; break;
		case GLFW_KEY_PAGE_UP: inputKey = InputManager::KeyCode::KEY_PAGE_UP; break;
		case GLFW_KEY_PAGE_DOWN: inputKey = InputManager::KeyCode::KEY_PAGE_DOWN; break;
		case GLFW_KEY_HOME: inputKey = InputManager::KeyCode::KEY_HOME; break;
		case GLFW_KEY_END: inputKey = InputManager::KeyCode::KEY_END; break;
		case GLFW_KEY_CAPS_LOCK: inputKey = InputManager::KeyCode::KEY_CAPS_LOCK; break;
		case GLFW_KEY_SCROLL_LOCK: inputKey = InputManager::KeyCode::KEY_SCROLL_LOCK; break;
		case GLFW_KEY_NUM_LOCK: inputKey = InputManager::KeyCode::KEY_NUM_LOCK; break;
		case GLFW_KEY_PRINT_SCREEN: inputKey = InputManager::KeyCode::KEY_PRINT_SCREEN; break;
		case GLFW_KEY_PAUSE: inputKey = InputManager::KeyCode::KEY_PAUSE; break;
		case GLFW_KEY_F1: inputKey = InputManager::KeyCode::KEY_F1; break;
		case GLFW_KEY_F2: inputKey = InputManager::KeyCode::KEY_F2; break;
		case GLFW_KEY_F3: inputKey = InputManager::KeyCode::KEY_F3; break;
		case GLFW_KEY_F4: inputKey = InputManager::KeyCode::KEY_F4; break;
		case GLFW_KEY_F5: inputKey = InputManager::KeyCode::KEY_F5; break;
		case GLFW_KEY_F6: inputKey = InputManager::KeyCode::KEY_F6; break;
		case GLFW_KEY_F7: inputKey = InputManager::KeyCode::KEY_F7; break;
		case GLFW_KEY_F8: inputKey = InputManager::KeyCode::KEY_F8; break;
		case GLFW_KEY_F9: inputKey = InputManager::KeyCode::KEY_F9; break;
		case GLFW_KEY_F10: inputKey = InputManager::KeyCode::KEY_F10; break;
		case GLFW_KEY_F11: inputKey = InputManager::KeyCode::KEY_F11; break;
		case GLFW_KEY_F12: inputKey = InputManager::KeyCode::KEY_F12; break;
		case GLFW_KEY_F13: inputKey = InputManager::KeyCode::KEY_F13; break;
		case GLFW_KEY_F14: inputKey = InputManager::KeyCode::KEY_F14; break;
		case GLFW_KEY_F15: inputKey = InputManager::KeyCode::KEY_F15; break;
		case GLFW_KEY_F16: inputKey = InputManager::KeyCode::KEY_F16; break;
		case GLFW_KEY_F17: inputKey = InputManager::KeyCode::KEY_F17; break;
		case GLFW_KEY_F18: inputKey = InputManager::KeyCode::KEY_F18; break;
		case GLFW_KEY_F19: inputKey = InputManager::KeyCode::KEY_F19; break;
		case GLFW_KEY_F20: inputKey = InputManager::KeyCode::KEY_F20; break;
		case GLFW_KEY_F21: inputKey = InputManager::KeyCode::KEY_F21; break;
		case GLFW_KEY_F22: inputKey = InputManager::KeyCode::KEY_F22; break;
		case GLFW_KEY_F23: inputKey = InputManager::KeyCode::KEY_F23; break;
		case GLFW_KEY_F24: inputKey = InputManager::KeyCode::KEY_F24; break;
		case GLFW_KEY_F25: inputKey = InputManager::KeyCode::KEY_F25; break;
		case GLFW_KEY_KP_0: inputKey = InputManager::KeyCode::KEY_KP_0; break;
		case GLFW_KEY_KP_1: inputKey = InputManager::KeyCode::KEY_KP_1; break;
		case GLFW_KEY_KP_2: inputKey = InputManager::KeyCode::KEY_KP_2; break;
		case GLFW_KEY_KP_3: inputKey = InputManager::KeyCode::KEY_KP_3; break;
		case GLFW_KEY_KP_4: inputKey = InputManager::KeyCode::KEY_KP_4; break;
		case GLFW_KEY_KP_5: inputKey = InputManager::KeyCode::KEY_KP_5; break;
		case GLFW_KEY_KP_6: inputKey = InputManager::KeyCode::KEY_KP_6; break;
		case GLFW_KEY_KP_7: inputKey = InputManager::KeyCode::KEY_KP_7; break;
		case GLFW_KEY_KP_8: inputKey = InputManager::KeyCode::KEY_KP_8; break;
		case GLFW_KEY_KP_9: inputKey = InputManager::KeyCode::KEY_KP_9; break;
		case GLFW_KEY_KP_DECIMAL: inputKey = InputManager::KeyCode::KEY_KP_DECIMAL; break;
		case GLFW_KEY_KP_DIVIDE: inputKey = InputManager::KeyCode::KEY_KP_DIVIDE; break;
		case GLFW_KEY_KP_MULTIPLY: inputKey = InputManager::KeyCode::KEY_KP_MULTIPLY; break;
		case GLFW_KEY_KP_SUBTRACT: inputKey = InputManager::KeyCode::KEY_KP_SUBTRACT; break;
		case GLFW_KEY_KP_ADD: inputKey = InputManager::KeyCode::KEY_KP_ADD; break;
		case GLFW_KEY_KP_ENTER: inputKey = InputManager::KeyCode::KEY_KP_ENTER; break;
		case GLFW_KEY_KP_EQUAL: inputKey = InputManager::KeyCode::KEY_KP_EQUAL; break;
		case GLFW_KEY_LEFT_SHIFT: inputKey = InputManager::KeyCode::KEY_LEFT_SHIFT; break;
		case GLFW_KEY_LEFT_CONTROL: inputKey = InputManager::KeyCode::KEY_LEFT_CONTROL; break;
		case GLFW_KEY_LEFT_ALT: inputKey = InputManager::KeyCode::KEY_LEFT_ALT; break;
		case GLFW_KEY_LEFT_SUPER: inputKey = InputManager::KeyCode::KEY_LEFT_SUPER; break;
		case GLFW_KEY_RIGHT_SHIFT: inputKey = InputManager::KeyCode::KEY_RIGHT_SHIFT; break;
		case GLFW_KEY_RIGHT_CONTROL: inputKey = InputManager::KeyCode::KEY_RIGHT_CONTROL; break;
		case GLFW_KEY_RIGHT_ALT: inputKey = InputManager::KeyCode::KEY_RIGHT_ALT; break;
		case GLFW_KEY_RIGHT_SUPER: inputKey = InputManager::KeyCode::KEY_RIGHT_SUPER; break;
		case GLFW_KEY_MENU: inputKey = InputManager::KeyCode::KEY_MENU; break;
		case -1: break; // We don't care about events GLFW can't handle
		default:
			PrintError("Unhandled glfw key passed to GLFWKeyToInputManagerKey in GLFWWIndowWrapper: %i\n",
					   glfwKey);
			break;
		}

		return inputKey;
	}

	i32 GLFWModsToInputManagerMods(i32 glfwMods)
	{
		i32 inputMods = 0;

		if (glfwMods & GLFW_MOD_SHIFT) inputMods |= (i32)InputManager::Mod::SHIFT;
		if (glfwMods & GLFW_MOD_ALT) inputMods |= (i32)InputManager::Mod::ALT;
		if (glfwMods & GLFW_MOD_CONTROL) inputMods |= (i32)InputManager::Mod::CONTROL;
		if (glfwMods & GLFW_MOD_SUPER) inputMods |= (i32)InputManager::Mod::SUPER;

		return inputMods;
	}

	InputManager::MouseButton GLFWButtonToInputManagerMouseButton(i32 glfwButton)
	{
		InputManager::MouseButton inputMouseButton = InputManager::MouseButton::_NONE;

		switch (glfwButton)
		{
		case GLFW_MOUSE_BUTTON_1: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_1; break;
		case GLFW_MOUSE_BUTTON_2: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_2; break;
		case GLFW_MOUSE_BUTTON_3: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_3; break;
		case GLFW_MOUSE_BUTTON_4: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_4; break;
		case GLFW_MOUSE_BUTTON_5: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_5; break;
		case GLFW_MOUSE_BUTTON_6: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_6; break;
		case GLFW_MOUSE_BUTTON_7: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_7; break;
		case GLFW_MOUSE_BUTTON_8: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_8; break;
		case -1: break; // We don't care about events GLFW can't handle
		default: PrintError("Unhandled glfw button passed to GLFWButtonToInputManagerMouseButton in GLFWWIndowWrapper: %i\n",
							glfwButton); break;
		}

		return inputMouseButton;
	}
} // namespace flex

#endif // COMPILE_OPEN_GL || COMPILE_VULKAN
