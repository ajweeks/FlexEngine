#include "stdafx.hpp"
#if COMPILE_OPEN_GL || COMPILE_VULKAN

#include "Window/GLFWWindowWrapper.hpp"

#include "Graphics/GL/GLHelpers.hpp"
#include "InputManager.hpp"
#include "Logger.hpp"
#include "FlexEngine.hpp"

namespace flex
{
	GLFWWindowWrapper::GLFWWindowWrapper(std::string title, glm::vec2i size, GameContext& gameContext) :
		Window(title, size, gameContext)
	{
		const bool moveConsoleToExtraMonitor = true;

		if (moveConsoleToExtraMonitor)
		{
			// The following four variables store the bounding rectangle of all monitors
			int virtualScreenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
			int virtualScreenTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
			int virtualScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
			int virtualScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

			int monitorWidth = GetSystemMetrics(SM_CXSCREEN);
			int monitorHeight = GetSystemMetrics(SM_CYSCREEN);

			// If another monitor is present, move the console to it
			if (virtualScreenWidth > monitorWidth)
			{
				int consoleWidth = (int)(700);
				int consoleHeight = (int)(800);
				int newX;
				int newY = 10;
				
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

				HWND hWnd = GetConsoleWindow();

				MoveWindow(hWnd, newX, newY, consoleWidth, consoleHeight, TRUE);
				
				// Call again to set size correctly (based on other monitor's DPI)
				MoveWindow(hWnd, newX, newY, consoleWidth, consoleHeight, TRUE);
			}
		}
	}

	GLFWWindowWrapper::~GLFWWindowWrapper()
	{
		if (m_Window)
		{
			// Not needed: (?)
			//glfwDestroyWindow(m_Window);
			m_Window = nullptr;
		}
	}

	float GLFWWindowWrapper::GetTime()
	{
		return (float)glfwGetTime();
	}

	void GLFWWindowWrapper::PollEvents()
	{
		glfwPollEvents();
	}

	void GLFWWindowWrapper::SetCursorMode(CursorMode mode)
	{
		Window::SetCursorMode(mode);

		int glfwCursorMode = 0;

		switch (mode)
		{
		case Window::CursorMode::NORMAL: glfwCursorMode = GLFW_CURSOR_NORMAL; break;
		case Window::CursorMode::HIDDEN: glfwCursorMode = GLFW_CURSOR_HIDDEN; break;
		case Window::CursorMode::DISABLED: glfwCursorMode = GLFW_CURSOR_DISABLED; break;
		default: Logger::LogError("Unhandled cursor mode passed to GLFWWindowWrapper::SetCursorMode: " + std::to_string((int)mode)); break;
		}

		glfwSetInputMode(m_Window, GLFW_CURSOR, glfwCursorMode);
	}

	void GLFWWindowWrapper::Update(const GameContext& gameContext)
	{
		Window::Update(gameContext);

		if (glfwWindowShouldClose(m_Window))
		{
			gameContext.engineInstance->Stop();
			return;
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

	void GLFWErrorCallback(int error, const char* description)
	{
		Logger::LogError("GLFW Error: " + std::to_string(error) + ": " + std::string(description));
	}

	void GLFWKeyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods)
	{
		UNREFERENCED_PARAMETER(scancode);

		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		const InputManager::Action inputAction = GLFWActionToInputManagerAction(action);
		const InputManager::KeyCode inputKey = GLFWKeyToInputManagerKey(key);
		const int inputMods = GLFWModsToInputManagerMods(mods);

		window->KeyCallback(inputKey, inputAction, inputMods);
	}

	void GLFWCharCallback(GLFWwindow* glfwWindow, unsigned int character)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		window->CharCallback(character);
	}

	void GLFWMouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		const InputManager::Action inputAction = GLFWActionToInputManagerAction(action);
		const int inputMods = GLFWModsToInputManagerMods(mods);
		const InputManager::MouseButton mouseButton = GLFWButtonToInputManagerMouseButton(button);

		window->MouseButtonCallback(mouseButton, inputAction, inputMods);
	}

	void GLFWWindowFocusCallback(GLFWwindow* glfwWindow, int focused)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		window->WindowFocusCallback(focused);
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

	void GLFWWindowSizeCallback(GLFWwindow* glfwWindow, int width, int height)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		window->WindowSizeCallback(width, height);
	}

	void GLFWFramebufferSizeCallback(GLFWwindow* glfwWindow, int width, int height)
	{
		Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
		window->FrameBufferSizeCallback(width, height);
	}

	InputManager::Action GLFWActionToInputManagerAction(int glfwAction)
	{
		InputManager::Action inputAction = InputManager::Action::_NONE;

		switch (glfwAction)
		{
		case GLFW_PRESS: inputAction = InputManager::Action::PRESS; break;
		case GLFW_REPEAT: inputAction = InputManager::Action::REPEAT; break;
		case GLFW_RELEASE: inputAction = InputManager::Action::RELEASE; break;
		case -1: break; // We don't care about events GLFW can't handle
		default: Logger::LogError("Unhandled glfw action passed to GLFWActionToInputManagerAction in GLFWWIndowWrapper: " + std::to_string(glfwAction));
		}

		return inputAction;
	}

	InputManager::KeyCode GLFWKeyToInputManagerKey(int glfwKey)
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
			Logger::LogError("Unhandled glfw key passed to GLFWKeyToInputManagerKey in GLFWWIndowWrapper: " + std::to_string(glfwKey));
			break;
		}

		return inputKey;
	}

	int GLFWModsToInputManagerMods(int glfwMods)
	{
		int inputMods = 0;

		if (glfwMods & GLFW_MOD_SHIFT) inputMods |= (int)InputManager::Mod::SHIFT;
		if (glfwMods & GLFW_MOD_ALT) inputMods |= (int)InputManager::Mod::ALT;
		if (glfwMods & GLFW_MOD_CONTROL) inputMods |= (int)InputManager::Mod::CONTROL;
		if (glfwMods & GLFW_MOD_SUPER) inputMods |= (int)InputManager::Mod::SUPER;

		return inputMods;
	}

	InputManager::MouseButton GLFWButtonToInputManagerMouseButton(int glfwButton)
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
		default: Logger::LogError("Unhandled glfw button passed to GLFWButtonToInputManagerMouseButton in GLFWWIndowWrapper: " + std::to_string(glfwButton)); break;
		}

		return inputMouseButton;
	}
} // namespace flex

#endif // COMPILE_OPEN_GL || COMPILE_VULKAN
