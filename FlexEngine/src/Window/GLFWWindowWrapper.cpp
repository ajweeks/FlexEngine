#include "stdafx.hpp"
#if COMPILE_OPEN_GL || COMPILE_VULKAN

#include "Window/GLFWWindowWrapper.hpp"

#include "FlexEngine.hpp"
#include "Graphics/GL/GLHelpers.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Window/Monitor.hpp"

namespace flex
{
	std::array<bool, MAX_JOYSTICK_COUNT> g_JoysticksConnected;

	GLFWWindowWrapper::GLFWWindowWrapper(const std::string& title) :
		Window(title)
	{
		m_LastNonFullscreenWindowMode = WindowMode::WINDOWED;

		m_WindowIcons.push_back(LoadGLFWimage(RESOURCE_LOCATION  "icons/flex-logo-03_128.png", 4));
		m_WindowIcons.push_back(LoadGLFWimage(RESOURCE_LOCATION  "icons/flex-logo-03_64.png", 4));
		m_WindowIcons.push_back(LoadGLFWimage(RESOURCE_LOCATION  "icons/flex-logo-03_48.png", 4));
		m_WindowIcons.push_back(LoadGLFWimage(RESOURCE_LOCATION  "icons/flex-logo-03_32.png", 4));
		m_WindowIcons.push_back(LoadGLFWimage(RESOURCE_LOCATION  "icons/flex-logo-03_16.png", 4));
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

	void GLFWWindowWrapper::Create(const glm::vec2i& size, const glm::vec2i& pos)
	{
		if (m_bMoveConsoleToOtherMonitor)
		{
			MoveConsole();
		}

		InitFromConfig();

		// Only use parameters if values weren't set through config file
		if (m_Size.x == 0)
		{
			m_Size = size;
			m_Position = pos;
		}

		m_FrameBufferSize = m_Size;
		m_LastWindowedSize = m_Size;
		m_StartingPosition = m_Position;
		m_LastWindowedPos = m_Position;

#if _DEBUG
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif // _DEBUG

		// Don't hide window when losing focus in Windowed Fullscreen
		glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

		if (m_bMaximized)
		{
			glfwWindowHint(GLFW_MAXIMIZED, GL_TRUE);
		}

		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		GLFWmonitor* monitor = NULL;
		if (m_CurrentWindowMode == WindowMode::FULLSCREEN)
		{
			monitor = glfwGetPrimaryMonitor();
		}

		m_Window = glfwCreateWindow(m_Size.x, m_Size.y, m_TitleString.c_str(), monitor, NULL);
		if (!m_Window)
		{
			PrintError("Failed to create glfw Window! Exiting...\n");
			glfwTerminate();
			// TODO: Try creating a window manually here
			exit(EXIT_FAILURE);
		}

		glfwSetWindowUserPointer(m_Window, this);

		SetUpCallbacks();

		i32 monitorCount;
		GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

		// If previously the window was on an additional monitor that is no longer present,
		// move the window to the primary monitor
		if (monitorCount == 1)
		{
			const GLFWvidmode* vidMode = glfwGetVideoMode(monitors[0]);
			i32 monitorWidth = vidMode->width;
			i32 monitorHeight = vidMode->height;

			if (m_StartingPosition.x < 0)
			{
				m_StartingPosition.x = 100;
			}
			else if (m_StartingPosition.x > monitorWidth)
			{
				m_StartingPosition.x = 100;
			}
			if (m_StartingPosition.y < 0)
			{
				m_StartingPosition.y = 100;
			}
			else if (m_StartingPosition.y > monitorHeight)
			{
				m_StartingPosition.y = 100;
			}
		}

		glfwSetWindowPos(m_Window, m_StartingPosition.x, m_StartingPosition.y);

		glfwFocusWindow(m_Window);
		m_bHasFocus = true;

		glfwMakeContextCurrent(m_Window);

		gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

#if _DEBUG
		if (glDebugMessageCallback)
		{
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			glDebugMessageCallback(glDebugOutput, nullptr);
			GLuint unusedIds = 0;
			glDebugMessageControl(GL_DONT_CARE,
								  GL_DONT_CARE,
								  GL_DONT_CARE,
								  0,
								  &unusedIds,
								  true);
		}
#endif // _DEBUG

		if (GLAD_GL_KHR_debug)
		{
			g_EngineInstance->bHasGLDebugExtension = true;
		}

		Print("OpenGL loaded\n");
		Print("Vendor:   %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
		Print("Renderer: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
		Print("Version:  %s\n\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

		if (!m_WindowIcons.empty() && m_WindowIcons[0].pixels)
		{
			glfwSetWindowIcon(m_Window, m_WindowIcons.size(), m_WindowIcons.data());
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
		glfwSetMonitorCallback(GLFWMointorCallback);
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
				newX = -(consoleWidth + 67);
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
		const Input::KeyAction inputAction = GLFWActionToInputManagerAction(action);
		const Input::KeyCode inputKey = GLFWKeyToInputManagerKey(key);
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
		const Input::KeyAction inputAction = GLFWActionToInputManagerAction(action);
		const i32 inputMods = GLFWModsToInputManagerMods(mods);
		const Input::MouseButton mouseButton = GLFWButtonToInputManagerMouseButton(button);

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

	void GLFWMointorCallback(GLFWmonitor* monitor, int event)
	{
		i32 w, h;
		glfwGetMonitorPhysicalSize(monitor, &w, &h);
		if (event == GLFW_CONNECTED)
		{
			Print("Monitor connected: %s, %dmm x %dmm\n", glfwGetMonitorName(monitor), w, h);
		}
		else
		{
			Print("Monitor disconnected: %s, %dmm x %dmm\n", glfwGetMonitorName(monitor), w, h);
		}
	}

	Input::KeyAction GLFWActionToInputManagerAction(i32 glfwAction)
	{
		Input::KeyAction inputAction = Input::KeyAction::_NONE;

		switch (glfwAction)
		{
		case GLFW_PRESS: inputAction = Input::KeyAction::PRESS; break;
		case GLFW_REPEAT: inputAction = Input::KeyAction::REPEAT; break;
		case GLFW_RELEASE: inputAction = Input::KeyAction::RELEASE; break;
		case -1: break; // We don't care about events GLFW can't handle
		default: PrintError("Unhandled glfw action passed to GLFWActionToInputManagerAction in GLFWWIndowWrapper: %i\n",
							glfwAction);
		}

		return inputAction;
	}

	Input::KeyCode GLFWKeyToInputManagerKey(i32 glfwKey)
	{
		Input::KeyCode inputKey = Input::KeyCode::_NONE;

		switch (glfwKey)
		{
		case GLFW_KEY_SPACE: inputKey = Input::KeyCode::KEY_SPACE; break;
		case GLFW_KEY_APOSTROPHE: inputKey = Input::KeyCode::KEY_APOSTROPHE; break;
		case GLFW_KEY_COMMA: inputKey = Input::KeyCode::KEY_COMMA; break;
		case GLFW_KEY_MINUS: inputKey = Input::KeyCode::KEY_MINUS; break;
		case GLFW_KEY_PERIOD: inputKey = Input::KeyCode::KEY_PERIOD; break;
		case GLFW_KEY_SLASH: inputKey = Input::KeyCode::KEY_SLASH; break;
		case GLFW_KEY_0: inputKey = Input::KeyCode::KEY_0; break;
		case GLFW_KEY_1: inputKey = Input::KeyCode::KEY_1; break;
		case GLFW_KEY_2: inputKey = Input::KeyCode::KEY_2; break;
		case GLFW_KEY_3: inputKey = Input::KeyCode::KEY_3; break;
		case GLFW_KEY_4: inputKey = Input::KeyCode::KEY_4; break;
		case GLFW_KEY_5: inputKey = Input::KeyCode::KEY_5; break;
		case GLFW_KEY_6: inputKey = Input::KeyCode::KEY_6; break;
		case GLFW_KEY_7: inputKey = Input::KeyCode::KEY_7; break;
		case GLFW_KEY_8: inputKey = Input::KeyCode::KEY_8; break;
		case GLFW_KEY_9: inputKey = Input::KeyCode::KEY_9; break;
		case GLFW_KEY_SEMICOLON: inputKey = Input::KeyCode::KEY_SEMICOLON; break;
		case GLFW_KEY_EQUAL: inputKey = Input::KeyCode::KEY_EQUAL; break;
		case GLFW_KEY_A: inputKey = Input::KeyCode::KEY_A; break;
		case GLFW_KEY_B: inputKey = Input::KeyCode::KEY_B; break;
		case GLFW_KEY_C: inputKey = Input::KeyCode::KEY_C; break;
		case GLFW_KEY_D: inputKey = Input::KeyCode::KEY_D; break;
		case GLFW_KEY_E: inputKey = Input::KeyCode::KEY_E; break;
		case GLFW_KEY_F: inputKey = Input::KeyCode::KEY_F; break;
		case GLFW_KEY_G: inputKey = Input::KeyCode::KEY_G; break;
		case GLFW_KEY_H: inputKey = Input::KeyCode::KEY_H; break;
		case GLFW_KEY_I: inputKey = Input::KeyCode::KEY_I; break;
		case GLFW_KEY_J: inputKey = Input::KeyCode::KEY_J; break;
		case GLFW_KEY_K: inputKey = Input::KeyCode::KEY_K; break;
		case GLFW_KEY_L: inputKey = Input::KeyCode::KEY_L; break;
		case GLFW_KEY_M: inputKey = Input::KeyCode::KEY_M; break;
		case GLFW_KEY_N: inputKey = Input::KeyCode::KEY_N; break;
		case GLFW_KEY_O: inputKey = Input::KeyCode::KEY_O; break;
		case GLFW_KEY_P: inputKey = Input::KeyCode::KEY_P; break;
		case GLFW_KEY_Q: inputKey = Input::KeyCode::KEY_Q; break;
		case GLFW_KEY_R: inputKey = Input::KeyCode::KEY_R; break;
		case GLFW_KEY_S: inputKey = Input::KeyCode::KEY_S; break;
		case GLFW_KEY_T: inputKey = Input::KeyCode::KEY_T; break;
		case GLFW_KEY_U: inputKey = Input::KeyCode::KEY_U; break;
		case GLFW_KEY_V: inputKey = Input::KeyCode::KEY_V; break;
		case GLFW_KEY_W: inputKey = Input::KeyCode::KEY_W; break;
		case GLFW_KEY_X: inputKey = Input::KeyCode::KEY_X; break;
		case GLFW_KEY_Y: inputKey = Input::KeyCode::KEY_Y; break;
		case GLFW_KEY_Z: inputKey = Input::KeyCode::KEY_Z; break;
		case GLFW_KEY_LEFT_BRACKET: inputKey = Input::KeyCode::KEY_LEFT_BRACKET; break;
		case GLFW_KEY_BACKSLASH: inputKey = Input::KeyCode::KEY_BACKSLASH; break;
		case GLFW_KEY_RIGHT_BRACKET: inputKey = Input::KeyCode::KEY_RIGHT_BRACKET; break;
		case GLFW_KEY_GRAVE_ACCENT: inputKey = Input::KeyCode::KEY_GRAVE_ACCENT; break;
		case GLFW_KEY_WORLD_1: inputKey = Input::KeyCode::KEY_WORLD_1; break;
		case GLFW_KEY_WORLD_2: inputKey = Input::KeyCode::KEY_WORLD_2; break;
		case GLFW_KEY_ESCAPE: inputKey = Input::KeyCode::KEY_ESCAPE; break;
		case GLFW_KEY_ENTER: inputKey = Input::KeyCode::KEY_ENTER; break;
		case GLFW_KEY_TAB: inputKey = Input::KeyCode::KEY_TAB; break;
		case GLFW_KEY_BACKSPACE: inputKey = Input::KeyCode::KEY_BACKSPACE; break;
		case GLFW_KEY_INSERT: inputKey = Input::KeyCode::KEY_INSERT; break;
		case GLFW_KEY_DELETE: inputKey = Input::KeyCode::KEY_DELETE; break;
		case GLFW_KEY_RIGHT: inputKey = Input::KeyCode::KEY_RIGHT; break;
		case GLFW_KEY_LEFT: inputKey = Input::KeyCode::KEY_LEFT; break;
		case GLFW_KEY_DOWN: inputKey = Input::KeyCode::KEY_DOWN; break;
		case GLFW_KEY_UP: inputKey = Input::KeyCode::KEY_UP; break;
		case GLFW_KEY_PAGE_UP: inputKey = Input::KeyCode::KEY_PAGE_UP; break;
		case GLFW_KEY_PAGE_DOWN: inputKey = Input::KeyCode::KEY_PAGE_DOWN; break;
		case GLFW_KEY_HOME: inputKey = Input::KeyCode::KEY_HOME; break;
		case GLFW_KEY_END: inputKey = Input::KeyCode::KEY_END; break;
		case GLFW_KEY_CAPS_LOCK: inputKey = Input::KeyCode::KEY_CAPS_LOCK; break;
		case GLFW_KEY_SCROLL_LOCK: inputKey = Input::KeyCode::KEY_SCROLL_LOCK; break;
		case GLFW_KEY_NUM_LOCK: inputKey = Input::KeyCode::KEY_NUM_LOCK; break;
		case GLFW_KEY_PRINT_SCREEN: inputKey = Input::KeyCode::KEY_PRINT_SCREEN; break;
		case GLFW_KEY_PAUSE: inputKey = Input::KeyCode::KEY_PAUSE; break;
		case GLFW_KEY_F1: inputKey = Input::KeyCode::KEY_F1; break;
		case GLFW_KEY_F2: inputKey = Input::KeyCode::KEY_F2; break;
		case GLFW_KEY_F3: inputKey = Input::KeyCode::KEY_F3; break;
		case GLFW_KEY_F4: inputKey = Input::KeyCode::KEY_F4; break;
		case GLFW_KEY_F5: inputKey = Input::KeyCode::KEY_F5; break;
		case GLFW_KEY_F6: inputKey = Input::KeyCode::KEY_F6; break;
		case GLFW_KEY_F7: inputKey = Input::KeyCode::KEY_F7; break;
		case GLFW_KEY_F8: inputKey = Input::KeyCode::KEY_F8; break;
		case GLFW_KEY_F9: inputKey = Input::KeyCode::KEY_F9; break;
		case GLFW_KEY_F10: inputKey = Input::KeyCode::KEY_F10; break;
		case GLFW_KEY_F11: inputKey = Input::KeyCode::KEY_F11; break;
		case GLFW_KEY_F12: inputKey = Input::KeyCode::KEY_F12; break;
		case GLFW_KEY_F13: inputKey = Input::KeyCode::KEY_F13; break;
		case GLFW_KEY_F14: inputKey = Input::KeyCode::KEY_F14; break;
		case GLFW_KEY_F15: inputKey = Input::KeyCode::KEY_F15; break;
		case GLFW_KEY_F16: inputKey = Input::KeyCode::KEY_F16; break;
		case GLFW_KEY_F17: inputKey = Input::KeyCode::KEY_F17; break;
		case GLFW_KEY_F18: inputKey = Input::KeyCode::KEY_F18; break;
		case GLFW_KEY_F19: inputKey = Input::KeyCode::KEY_F19; break;
		case GLFW_KEY_F20: inputKey = Input::KeyCode::KEY_F20; break;
		case GLFW_KEY_F21: inputKey = Input::KeyCode::KEY_F21; break;
		case GLFW_KEY_F22: inputKey = Input::KeyCode::KEY_F22; break;
		case GLFW_KEY_F23: inputKey = Input::KeyCode::KEY_F23; break;
		case GLFW_KEY_F24: inputKey = Input::KeyCode::KEY_F24; break;
		case GLFW_KEY_F25: inputKey = Input::KeyCode::KEY_F25; break;
		case GLFW_KEY_KP_0: inputKey = Input::KeyCode::KEY_KP_0; break;
		case GLFW_KEY_KP_1: inputKey = Input::KeyCode::KEY_KP_1; break;
		case GLFW_KEY_KP_2: inputKey = Input::KeyCode::KEY_KP_2; break;
		case GLFW_KEY_KP_3: inputKey = Input::KeyCode::KEY_KP_3; break;
		case GLFW_KEY_KP_4: inputKey = Input::KeyCode::KEY_KP_4; break;
		case GLFW_KEY_KP_5: inputKey = Input::KeyCode::KEY_KP_5; break;
		case GLFW_KEY_KP_6: inputKey = Input::KeyCode::KEY_KP_6; break;
		case GLFW_KEY_KP_7: inputKey = Input::KeyCode::KEY_KP_7; break;
		case GLFW_KEY_KP_8: inputKey = Input::KeyCode::KEY_KP_8; break;
		case GLFW_KEY_KP_9: inputKey = Input::KeyCode::KEY_KP_9; break;
		case GLFW_KEY_KP_DECIMAL: inputKey = Input::KeyCode::KEY_KP_DECIMAL; break;
		case GLFW_KEY_KP_DIVIDE: inputKey = Input::KeyCode::KEY_KP_DIVIDE; break;
		case GLFW_KEY_KP_MULTIPLY: inputKey = Input::KeyCode::KEY_KP_MULTIPLY; break;
		case GLFW_KEY_KP_SUBTRACT: inputKey = Input::KeyCode::KEY_KP_SUBTRACT; break;
		case GLFW_KEY_KP_ADD: inputKey = Input::KeyCode::KEY_KP_ADD; break;
		case GLFW_KEY_KP_ENTER: inputKey = Input::KeyCode::KEY_KP_ENTER; break;
		case GLFW_KEY_KP_EQUAL: inputKey = Input::KeyCode::KEY_KP_EQUAL; break;
		case GLFW_KEY_LEFT_SHIFT: inputKey = Input::KeyCode::KEY_LEFT_SHIFT; break;
		case GLFW_KEY_LEFT_CONTROL: inputKey = Input::KeyCode::KEY_LEFT_CONTROL; break;
		case GLFW_KEY_LEFT_ALT: inputKey = Input::KeyCode::KEY_LEFT_ALT; break;
		case GLFW_KEY_LEFT_SUPER: inputKey = Input::KeyCode::KEY_LEFT_SUPER; break;
		case GLFW_KEY_RIGHT_SHIFT: inputKey = Input::KeyCode::KEY_RIGHT_SHIFT; break;
		case GLFW_KEY_RIGHT_CONTROL: inputKey = Input::KeyCode::KEY_RIGHT_CONTROL; break;
		case GLFW_KEY_RIGHT_ALT: inputKey = Input::KeyCode::KEY_RIGHT_ALT; break;
		case GLFW_KEY_RIGHT_SUPER: inputKey = Input::KeyCode::KEY_RIGHT_SUPER; break;
		case GLFW_KEY_MENU: inputKey = Input::KeyCode::KEY_MENU; break;
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

		if (glfwMods & GLFW_MOD_SHIFT) inputMods |= (i32)Input::Mod::SHIFT;
		if (glfwMods & GLFW_MOD_ALT) inputMods |= (i32)Input::Mod::ALT;
		if (glfwMods & GLFW_MOD_CONTROL) inputMods |= (i32)Input::Mod::CONTROL;
		if (glfwMods & GLFW_MOD_SUPER) inputMods |= (i32)Input::Mod::SUPER;

		return inputMods;
	}

	Input::MouseButton GLFWButtonToInputManagerMouseButton(i32 glfwButton)
	{
		Input::MouseButton inputMouseButton = Input::MouseButton::_NONE;

		switch (glfwButton)
		{
		case GLFW_MOUSE_BUTTON_1: inputMouseButton = Input::MouseButton::MOUSE_BUTTON_1; break;
		case GLFW_MOUSE_BUTTON_2: inputMouseButton = Input::MouseButton::MOUSE_BUTTON_2; break;
		case GLFW_MOUSE_BUTTON_3: inputMouseButton = Input::MouseButton::MOUSE_BUTTON_3; break;
		case GLFW_MOUSE_BUTTON_4: inputMouseButton = Input::MouseButton::MOUSE_BUTTON_4; break;
		case GLFW_MOUSE_BUTTON_5: inputMouseButton = Input::MouseButton::MOUSE_BUTTON_5; break;
		case GLFW_MOUSE_BUTTON_6: inputMouseButton = Input::MouseButton::MOUSE_BUTTON_6; break;
		case GLFW_MOUSE_BUTTON_7: inputMouseButton = Input::MouseButton::MOUSE_BUTTON_7; break;
		case GLFW_MOUSE_BUTTON_8: inputMouseButton = Input::MouseButton::MOUSE_BUTTON_8; break;
		case -1: break; // We don't care about events GLFW can't handle
		default: PrintError("Unhandled glfw button passed to GLFWButtonToInputManagerMouseButton in GLFWWIndowWrapper: %i\n",
							glfwButton); break;
		}

		return inputMouseButton;
	}

	void WINAPI glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
							  const GLchar* message, const void* userParam)
	{
		UNREFERENCED_PARAMETER(userParam);
		UNREFERENCED_PARAMETER(length);

		// Ignore insignificant error/warning codes
		if (id == 131169 || id == 131185 || id == 131218 || id == 131204)
		{
			return;
		}

		PrintError("---------------\n\t");
		PrintError("GL Debug message (%i): %s\n", id, message);

		switch (source)
		{
		case GL_DEBUG_SOURCE_API:             PrintError("Source: API"); break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   PrintError("Source: Window System"); break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: PrintError("Source: Shader Compiler"); break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:     PrintError("Source: Third Party"); break;
		case GL_DEBUG_SOURCE_APPLICATION:     PrintError("Source: Application"); break;
		case GL_DEBUG_SOURCE_OTHER:           PrintError("Source: Other"); break;
		}
		PrintError("\n\t");

		switch (type)
		{
		case GL_DEBUG_TYPE_ERROR:               PrintError("Type: Error"); break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: PrintError("Type: Deprecated Behaviour"); break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  PrintError("Type: Undefined Behaviour"); break;
		case GL_DEBUG_TYPE_PORTABILITY:         PrintError("Type: Portability"); break;
		case GL_DEBUG_TYPE_PERFORMANCE:         PrintError("Type: Performance"); break;
		case GL_DEBUG_TYPE_MARKER:              PrintError("Type: Marker"); break;
		case GL_DEBUG_TYPE_PUSH_GROUP:          PrintError("Type: Push Group"); break;
		case GL_DEBUG_TYPE_POP_GROUP:           PrintError("Type: Pop Group"); break;
		case GL_DEBUG_TYPE_OTHER:               PrintError("Type: Other"); break;
		}
		PrintError("\n\t");

		switch (severity)
		{
		case GL_DEBUG_SEVERITY_HIGH:         PrintError("Severity: high"); break;
		case GL_DEBUG_SEVERITY_MEDIUM:       PrintError("Severity: medium"); break;
		case GL_DEBUG_SEVERITY_LOW:          PrintError("Severity: low"); break;
		case GL_DEBUG_SEVERITY_NOTIFICATION: PrintError("Severity: notification"); break;
		}
		PrintError("\n---------------\n");
	}
} // namespace flex

#endif // COMPILE_OPEN_GL || COMPILE_VULKAN
