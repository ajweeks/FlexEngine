#include "stdafx.h"
#if COMPILE_D3D

#include "Window/D3D/D3DWindowWrapper.h"
#include "Logger.h"
#include "GameContext.h"
#include "ShaderUtils.h"
#include "TechDemo.h"
#include "InputManager.h"

#include <Windowsx.h>

#include <locale>
#include <codecvt>

//void D3DErrorCallback(int error, const char* description)
//{
//	Logger::LogError("D3D Error: " + std::string(description));
//}

InputManager::Action D3DWindowWrapper::D3DActionToInputManagerAction(int action)
{
	InputManager::Action inputAction = InputManager::Action::_NONE;

	// TODO: Implement

	switch (action)
	{
	case GLFW_PRESS: inputAction = InputManager::Action::PRESS; break;
	case GLFW_REPEAT: inputAction = InputManager::Action::REPEAT; break;
	case GLFW_RELEASE: inputAction = InputManager::Action::RELEASE; break;
	case -1: break; // We don't care about events win32 can't handle
	default: Logger::LogError("Unhandled action passed to D3DActionToInputManagerAction: " + std::to_string(action));
	}

	return inputAction;
}

InputManager::KeyCode D3DWindowWrapper::D3DKeyToInputManagerKey(int keyCode)
{
	InputManager::KeyCode inputKey = InputManager::KeyCode::_NONE;

	switch (keyCode)
	{
	case VK_SPACE: inputKey = InputManager::KeyCode::KEY_SPACE; break;
	case VK_OEM_7: inputKey = InputManager::KeyCode::KEY_APOSTROPHE; break;
	case VK_OEM_COMMA: inputKey = InputManager::KeyCode::KEY_COMMA; break;
	case VK_OEM_MINUS: inputKey = InputManager::KeyCode::KEY_MINUS; break;
	case VK_OEM_PERIOD: inputKey = InputManager::KeyCode::KEY_PERIOD; break;
	case VK_OEM_2: inputKey = InputManager::KeyCode::KEY_SLASH; break;
	case '0': inputKey = InputManager::KeyCode::KEY_0; break;
	case '1': inputKey = InputManager::KeyCode::KEY_1; break;
	case '2': inputKey = InputManager::KeyCode::KEY_2; break;
	case '3': inputKey = InputManager::KeyCode::KEY_3; break;
	case '4': inputKey = InputManager::KeyCode::KEY_4; break;
	case '5': inputKey = InputManager::KeyCode::KEY_5; break;
	case '6': inputKey = InputManager::KeyCode::KEY_6; break;
	case '7': inputKey = InputManager::KeyCode::KEY_7; break;
	case '8': inputKey = InputManager::KeyCode::KEY_8; break;
	case '9': inputKey = InputManager::KeyCode::KEY_9; break;
	case VK_OEM_1: inputKey = InputManager::KeyCode::KEY_SEMICOLON; break;
	case VK_OEM_PLUS: inputKey = InputManager::KeyCode::KEY_EQUAL; break;
	case 'A': inputKey = InputManager::KeyCode::KEY_A; break;
	case 'B': inputKey = InputManager::KeyCode::KEY_B; break;
	case 'C': inputKey = InputManager::KeyCode::KEY_C; break;
	case 'D': inputKey = InputManager::KeyCode::KEY_D; break;
	case 'E': inputKey = InputManager::KeyCode::KEY_E; break;
	case 'F': inputKey = InputManager::KeyCode::KEY_F; break;
	case 'G': inputKey = InputManager::KeyCode::KEY_G; break;
	case 'H': inputKey = InputManager::KeyCode::KEY_H; break;
	case 'I': inputKey = InputManager::KeyCode::KEY_I; break;
	case 'J': inputKey = InputManager::KeyCode::KEY_J; break;
	case 'K': inputKey = InputManager::KeyCode::KEY_K; break;
	case 'L': inputKey = InputManager::KeyCode::KEY_L; break;
	case 'M': inputKey = InputManager::KeyCode::KEY_M; break;
	case 'N': inputKey = InputManager::KeyCode::KEY_N; break;
	case 'O': inputKey = InputManager::KeyCode::KEY_O; break;
	case 'P': inputKey = InputManager::KeyCode::KEY_P; break;
	case 'Q': inputKey = InputManager::KeyCode::KEY_Q; break;
	case 'R': inputKey = InputManager::KeyCode::KEY_R; break;
	case 'S': inputKey = InputManager::KeyCode::KEY_S; break;
	case 'T': inputKey = InputManager::KeyCode::KEY_T; break;
	case 'U': inputKey = InputManager::KeyCode::KEY_U; break;
	case 'V': inputKey = InputManager::KeyCode::KEY_V; break;
	case 'W': inputKey = InputManager::KeyCode::KEY_W; break;
	case 'X': inputKey = InputManager::KeyCode::KEY_X; break;
	case 'Y': inputKey = InputManager::KeyCode::KEY_Y; break;
	case 'Z': inputKey = InputManager::KeyCode::KEY_Z; break;
	case VK_OEM_4: inputKey = InputManager::KeyCode::KEY_LEFT_BRACKET; break;
	case VK_OEM_5: inputKey = InputManager::KeyCode::KEY_BACKSLASH; break;
	case VK_OEM_6: inputKey = InputManager::KeyCode::KEY_RIGHT_BRACKET; break;
	case VK_OEM_3: inputKey = InputManager::KeyCode::KEY_GRAVE_ACCENT; break;
	case VK_OEM_8: inputKey = InputManager::KeyCode::KEY_WORLD_1; break;
	//case VK_WORLD_2: inputKey = InputManager::KeyCode::KEY_WORLD_2; break;
	case VK_ESCAPE: inputKey = InputManager::KeyCode::KEY_ESCAPE; break;
	case VK_RETURN: inputKey = InputManager::KeyCode::KEY_ENTER; break;
	case VK_TAB: inputKey = InputManager::KeyCode::KEY_TAB; break;
	case VK_BACK: inputKey = InputManager::KeyCode::KEY_BACKSPACE; break;
	case VK_INSERT: inputKey = InputManager::KeyCode::KEY_INSERT; break;
	case VK_DELETE: inputKey = InputManager::KeyCode::KEY_DELETE; break;
	case VK_RIGHT: inputKey = InputManager::KeyCode::KEY_RIGHT; break;
	case VK_LEFT: inputKey = InputManager::KeyCode::KEY_LEFT; break;
	case VK_DOWN: inputKey = InputManager::KeyCode::KEY_DOWN; break;
	case VK_UP: inputKey = InputManager::KeyCode::KEY_UP; break;
	case VK_PRIOR: inputKey = InputManager::KeyCode::KEY_PAGE_UP; break;
	case VK_NEXT: inputKey = InputManager::KeyCode::KEY_PAGE_DOWN; break;
	case VK_HOME: inputKey = InputManager::KeyCode::KEY_HOME; break;
	case VK_END: inputKey = InputManager::KeyCode::KEY_END; break;
	case VK_CAPITAL: inputKey = InputManager::KeyCode::KEY_CAPS_LOCK; break;
	case VK_SCROLL: inputKey = InputManager::KeyCode::KEY_SCROLL_LOCK; break;
	case VK_NUMLOCK: inputKey = InputManager::KeyCode::KEY_NUM_LOCK; break;
	case VK_SNAPSHOT: inputKey = InputManager::KeyCode::KEY_PRINT_SCREEN; break;
	case VK_PAUSE: inputKey = InputManager::KeyCode::KEY_PAUSE; break;
	case VK_F1: inputKey = InputManager::KeyCode::KEY_F1; break;
	case VK_F2: inputKey = InputManager::KeyCode::KEY_F2; break;
	case VK_F3: inputKey = InputManager::KeyCode::KEY_F3; break;
	case VK_F4: inputKey = InputManager::KeyCode::KEY_F4; break;
	case VK_F5: inputKey = InputManager::KeyCode::KEY_F5; break;
	case VK_F6: inputKey = InputManager::KeyCode::KEY_F6; break;
	case VK_F7: inputKey = InputManager::KeyCode::KEY_F7; break;
	case VK_F8: inputKey = InputManager::KeyCode::KEY_F8; break;
	case VK_F9: inputKey = InputManager::KeyCode::KEY_F9; break;
	case VK_F10: inputKey = InputManager::KeyCode::KEY_F10; break;
	case VK_F11: inputKey = InputManager::KeyCode::KEY_F11; break;
	case VK_F12: inputKey = InputManager::KeyCode::KEY_F12; break;
	case VK_F13: inputKey = InputManager::KeyCode::KEY_F13; break;
	case VK_F14: inputKey = InputManager::KeyCode::KEY_F14; break;
	case VK_F15: inputKey = InputManager::KeyCode::KEY_F15; break;
	case VK_F16: inputKey = InputManager::KeyCode::KEY_F16; break;
	case VK_F17: inputKey = InputManager::KeyCode::KEY_F17; break;
	case VK_F18: inputKey = InputManager::KeyCode::KEY_F18; break;
	case VK_F19: inputKey = InputManager::KeyCode::KEY_F19; break;
	case VK_F20: inputKey = InputManager::KeyCode::KEY_F20; break;
	case VK_F21: inputKey = InputManager::KeyCode::KEY_F21; break;
	case VK_F22: inputKey = InputManager::KeyCode::KEY_F22; break;
	case VK_F23: inputKey = InputManager::KeyCode::KEY_F23; break;
	case VK_F24: inputKey = InputManager::KeyCode::KEY_F24; break;
	//case VK_F25: inputKey = InputManager::KeyCode::KEY_F25; break;
	case VK_NUMPAD0: inputKey = InputManager::KeyCode::KEY_KP_0; break;
	case VK_NUMPAD1: inputKey = InputManager::KeyCode::KEY_KP_1; break;
	case VK_NUMPAD2: inputKey = InputManager::KeyCode::KEY_KP_2; break;
	case VK_NUMPAD3: inputKey = InputManager::KeyCode::KEY_KP_3; break;
	case VK_NUMPAD4: inputKey = InputManager::KeyCode::KEY_KP_4; break;
	case VK_NUMPAD5: inputKey = InputManager::KeyCode::KEY_KP_5; break;
	case VK_NUMPAD6: inputKey = InputManager::KeyCode::KEY_KP_6; break;
	case VK_NUMPAD7: inputKey = InputManager::KeyCode::KEY_KP_7; break;
	case VK_NUMPAD8: inputKey = InputManager::KeyCode::KEY_KP_8; break;
	case VK_NUMPAD9: inputKey = InputManager::KeyCode::KEY_KP_9; break;
	case VK_DECIMAL: inputKey = InputManager::KeyCode::KEY_KP_DECIMAL; break;
	case VK_DIVIDE: inputKey = InputManager::KeyCode::KEY_KP_DIVIDE; break;
	case VK_MULTIPLY: inputKey = InputManager::KeyCode::KEY_KP_MULTIPLY; break;
	case VK_SUBTRACT: inputKey = InputManager::KeyCode::KEY_KP_SUBTRACT; break;
	case VK_ADD: inputKey = InputManager::KeyCode::KEY_KP_ADD; break;
	//case VK_KP_ENTER: inputKey = InputManager::KeyCode::KEY_KP_ENTER; break;
	//case VK_KP_EQUAL: inputKey = InputManager::KeyCode::KEY_KP_EQUAL; break;
	case VK_SHIFT: // Fallthrough
	case VK_LSHIFT: inputKey = InputManager::KeyCode::KEY_LEFT_SHIFT; break;
	case VK_CONTROL: // Fallthrough
	case VK_LCONTROL: inputKey = InputManager::KeyCode::KEY_LEFT_CONTROL; break;
	case VK_LMENU: inputKey = InputManager::KeyCode::KEY_LEFT_ALT; break;
	case VK_LWIN: inputKey = InputManager::KeyCode::KEY_LEFT_SUPER; break;
	case VK_RSHIFT: inputKey = InputManager::KeyCode::KEY_RIGHT_SHIFT; break;
	case VK_RCONTROL: inputKey = InputManager::KeyCode::KEY_RIGHT_CONTROL; break;
	case VK_RMENU: inputKey = InputManager::KeyCode::KEY_RIGHT_ALT; break;
	case VK_RWIN: inputKey = InputManager::KeyCode::KEY_RIGHT_SUPER; break;
	case VK_APPS: // Fallthrough (context menu)
	case VK_MENU: inputKey = InputManager::KeyCode::KEY_MENU; break;
	case -1: break; // We don't care about events GLFW can't handle
	default:
		Logger::LogError("Unhandled keycode passed to D3DKeyToInputManagerKey: " + std::to_string(keyCode));
		break;
	}

	return inputKey;
}

int D3DWindowWrapper::D3DModsToInputManagerMods(int windowsMods)
{
	int inputMods = 0;

	if (windowsMods & MK_SHIFT) inputMods |= (int)InputManager::Mod::SHIFT;
	if (windowsMods & MK_CONTROL) inputMods |= (int)InputManager::Mod::CONTROL;
	//if (windowsMods & VK_MENU) inputMods |= (int)InputManager::Mod::ALT;
	//if ((windowsMods & VK_LWIN) || (windowsMods & VK_RWIN)) inputMods |= (int)InputManager::Mod::SUPER;

	return inputMods;
}

InputManager::MouseButton D3DWindowWrapper::D3DButtonToInputManagerMouseButton(int button)
{
	switch (button)
	{
	case MK_LBUTTON: return InputManager::MouseButton::MOUSE_BUTTON_1;
	case MK_MBUTTON: return InputManager::MouseButton::MOUSE_BUTTON_2;
	case MK_RBUTTON: return InputManager::MouseButton::MOUSE_BUTTON_3; 
	case MK_XBUTTON1: return InputManager::MouseButton::MOUSE_BUTTON_4; 
	case MK_XBUTTON2: return InputManager::MouseButton::MOUSE_BUTTON_5; 
	//case MK_MOUSE_BUTTON_6: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_6; break;
	//case MK_MOUSE_BUTTON_7: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_7; break;
	//case MK_MOUSE_BUTTON_8: inputMouseButton = InputManager::MouseButton::MOUSE_BUTTON_8; break;
	case -1: 
		break; // We don't care about events win32 can't handle
	default: 
		Logger::LogError("Unhandled button passed to D3DButtonToInputManagerMouseButton: " + std::to_string(button)); 
		break;
	}

	return InputManager::MouseButton::_NONE;
}

uint16_t ExtractInt(uint16_t orig16BitWord, unsigned from, unsigned to)
{
	unsigned mask = ((1 << (to - from + 1)) - 1) << from;
	return (orig16BitWord & mask) >> from;
}

//LRESULT D3DKeyCallback(HWND hWnd, int keyCode, WPARAM wParam, LPARAM lParam)
//{
//	const int repeatCount = ExtractInt(lParam, 0, 15);
//	const int scanCode = ExtractInt(lParam, 16, 23);
//	const bool previouslyDown = ExtractInt(lParam, 16, 16) == 1;
//	const bool pressed = ExtractInt(lParam, 17, 17) == 0;
//
//	Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
//	InputManager::Action inputAction;
//	if (pressed)
//	{
//		if (previouslyDown)
//		{
//			inputAction = InputManager::Action::PRESS;
//		}
//		else
//		{
//			inputAction = InputManager::Action::REPEAT;
//		}
//	}
//	else
//	{
//		inputAction = InputManager::Action::RELEASE;
//	}
//	const InputManager::KeyCode inputKey = D3DWindowWrapper::D3DKeyToInputManagerKey(wParam);
//
//	// TODO: Calculate mods here
//	const int inputMods = 0;
//
//	window->KeyCallback(inputKey, inputAction, inputMods);
//
//	return CallNextHookEx(NULL, keyCode, wParam, lParam);
//}
//
//LRESULT D3DMouseButtonCallback(HWND hWnd, int button, WPARAM wParam, LPARAM lParam)
//{
//	Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
//	InputManager::Action inputAction;
//
//
//	// TODO: Calculate mods here
//	const int inputMods = 0;
//
//	const InputManager::MouseButton mouseButton = D3DWindowWrapper::D3DButtonToInputManagerMouseButton(button);
//
//	window->MouseButtonCallback(mouseButton, inputAction, inputMods);
//}
//
//LRESULT D3DWindowFocusCallback(HWND hWnd, int focused)
//{
//	Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
//	window->WindowFocusCallback(focused);
//}
//
//LRESULT D3DCursorPosCallback(HWND hWnd, double x, double y)
//{
//	Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
//	window->CursorPosCallback(x, y);
//}
//
//LRESULT D3DWindowSizeCallback(HWND hWnd, int width, int height)
//{
//	Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
//	window->WindowSizeCallback(width, height);
//}

D3DWindowWrapper::D3DWindowWrapper(const std::string& title, glm::vec2i size, GameContext& gameContext) :
	Window(title, size, gameContext)
{
	RegisterWindow(title, size, gameContext);
	m_StartingTime = GetTickCount();

	if (!m_Window)
	{
		exit(EXIT_FAILURE);
	}
	
	m_HasFocus = true;

	//gameContext.program = ShaderUtils::LoadShaders("resources/shaders/simple.vert", "resources/shaders/simple.frag");
}

HWND D3DWindowWrapper::GetWindowHandle() const
{
	return m_Window;
}

void D3DWindowWrapper::RegisterWindow(const std::string& title, glm::vec2i size, const GameContext& gameContext)
{
	HINSTANCE hInstance = GetModuleHandle(NULL);

	m_WindowClassName = L"D3D11_WindowClass";

	// Register class
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProcedure;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, L"IDI_ICON");
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = m_WindowClassName;
	wcex.hIconSm = LoadIcon(wcex.hInstance, L"IDI_ICON");
	if (!RegisterClassEx(&wcex))
	{
		Logger::LogError("Unable to register D3D window class!");
		return;
	}

	// Create window
	RECT rc;
	rc.top = 0;
	rc.left = 0;
	rc.right = static_cast<LONG>(size.x / 2.0f);
	rc.bottom = static_cast<LONG>(size.y / 2.0f);

	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> unicodeConverter;
	const std::wstring title_w = unicodeConverter.from_bytes(title.c_str());

	// NOTE: For fullscreen use WS_EX_TOPMOST, .., .., WS_POPUP
	m_Window = CreateWindowEx(0, wcex.lpszClassName, title_w.c_str(), WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance,
		(LPVOID)gameContext.mainApp);

	if (!m_Window)
	{
		DWORD lastError = GetLastError();
		Logger::LogError("Unable to create D3D window!");
		return;
	}

	// NOTE: Set nCmdShow to SW_SHOWMAXIMIZED to default to fullscreen.
	int nCmdShow = SW_SHOWDEFAULT;
	ShowWindow(m_Window, nCmdShow);

	SetWindowLongPtr(m_Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	GetClientRect(m_Window, &rc);
}

D3DWindowWrapper::~D3DWindowWrapper()
{
	if (m_Window)
	{
		DestroyWindow(m_Window);
		UnregisterClass(m_WindowClassName, GetModuleHandle(NULL));
	}
}

float D3DWindowWrapper::GetTime()
{
	return (float)(GetTickCount() - m_StartingTime) / 1000.0f;
}

void D3DWindowWrapper::PollEvents()
{
	MSG msg = { 0 };
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

void D3DWindowWrapper::SetCursorMode(CursorMode mode)
{
	// TODO: Implement
	//int glfwCursorMode = 0;
	//
	//switch (mode)
	//{
	//case Window::CursorMode::NORMAL: glfwCursorMode = GLFW_CURSOR_NORMAL; break;
	//case Window::CursorMode::HIDDEN: glfwCursorMode = GLFW_CURSOR_HIDDEN; break;
	//case Window::CursorMode::DISABLED: glfwCursorMode = GLFW_CURSOR_DISABLED; break;
	//default: Logger::LogError("Unhandled cursor mode passed to GLFWWindowWrapper::SetCursorMode: " + std::to_string((int)mode)); break;
	//}
	//
	//glfwSetInputMode(m_Window, GLFW_CURSOR, glfwCursorMode);
}

void D3DWindowWrapper::Update(const GameContext& gameContext)
{
	Window::Update(gameContext);
}

void D3DWindowWrapper::SetSize(int width, int height)
{
	m_Size = glm::vec2i(width, height);
	m_GameContextRef.renderer->OnWindowSize(width, height);
}

void D3DWindowWrapper::SetWindowTitle(const std::string& title)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	std::wstring title_w = converter.from_bytes(title.c_str());
	SetWindowText(m_Window, title_w.c_str());
}

LRESULT CALLBACK WndProcedure(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Window* window = (Window*)GetWindowLongPtr(hWnd, GWL_USERDATA);
	
	switch (msg)
	{
	case WM_CLOSE:
	{
		DestroyWindow(hWnd);
		window->m_GameContextRef.mainApp->Stop();
	} return 0;
	case WM_KEYDOWN:
	{
		const InputManager::Action inputAction = InputManager::Action::PRESS; // GLFWActionToInputManagerAction(action);
		const InputManager::KeyCode inputKey = D3DWindowWrapper::D3DKeyToInputManagerKey((int)wParam);
		const int inputMods = 0;// GLFWModsToInputManagerMods(mods);

		window->KeyCallback(inputKey, inputAction, inputMods);
	} return 0;
	case WM_KEYUP:
	{
		const InputManager::Action inputAction = InputManager::Action::RELEASE; // GLFWActionToInputManagerAction(action);
		const InputManager::KeyCode inputKey = D3DWindowWrapper::D3DKeyToInputManagerKey((int)wParam);
		const int inputMods = 0;// GLFWModsToInputManagerMods(mods);

		window->KeyCallback(inputKey, inputAction, inputMods);
	} return 0;
	case WM_LBUTTONDOWN:
	{
		const InputManager::Action inputAction = InputManager::Action::PRESS; // GLFWActionToInputManagerAction(action);
		const InputManager::MouseButton mouseButton = InputManager::MouseButton::LEFT;
		const int inputMods = D3DWindowWrapper::D3DModsToInputManagerMods((int)wParam);

		window->MouseButtonCallback(mouseButton, inputAction, inputMods);
	} return 0;
	case WM_LBUTTONUP:
	{
		const InputManager::Action inputAction = InputManager::Action::RELEASE; // GLFWActionToInputManagerAction(action);
		const InputManager::MouseButton mouseButton = InputManager::MouseButton::LEFT;
		const int inputMods = D3DWindowWrapper::D3DModsToInputManagerMods((int)wParam);

		window->MouseButtonCallback(mouseButton, inputAction, inputMods);
	} return 0;
	case WM_RBUTTONDOWN:
	{
		const InputManager::Action inputAction = InputManager::Action::PRESS; // GLFWActionToInputManagerAction(action);
		const InputManager::MouseButton mouseButton = InputManager::MouseButton::RIGHT;
		const int inputMods = D3DWindowWrapper::D3DModsToInputManagerMods((int)wParam);

		window->MouseButtonCallback(mouseButton, inputAction, inputMods);
	} return 0;
	case WM_RBUTTONUP:
	{
		const InputManager::Action inputAction = InputManager::Action::RELEASE; // GLFWActionToInputManagerAction(action);
		const InputManager::MouseButton mouseButton = InputManager::MouseButton::RIGHT;
		const int inputMods = D3DWindowWrapper::D3DModsToInputManagerMods((int)wParam);

		window->MouseButtonCallback(mouseButton, inputAction, inputMods);
	} return 0;
	case WM_MBUTTONDOWN:
	{
		const InputManager::Action inputAction = InputManager::Action::PRESS; // GLFWActionToInputManagerAction(action);
		const InputManager::MouseButton mouseButton = InputManager::MouseButton::MIDDLE;
		const int inputMods = D3DWindowWrapper::D3DModsToInputManagerMods((int)wParam);

		window->MouseButtonCallback(mouseButton, inputAction, inputMods);
	} return 0;
	case WM_MBUTTONUP:
	{
		const InputManager::Action inputAction = InputManager::Action::RELEASE; // GLFWActionToInputManagerAction(action);
		const InputManager::MouseButton mouseButton = InputManager::MouseButton::MIDDLE;
		const int inputMods = D3DWindowWrapper::D3DModsToInputManagerMods((int)wParam);

		window->MouseButtonCallback(mouseButton, inputAction, inputMods);
	} return 0;
	case WM_MOUSEMOVE:
	{
		double x = GET_X_LPARAM(lParam);
		double y = GET_Y_LPARAM(lParam);

		window->CursorPosCallback(x, y);
	} return 0;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

#endif // COMPILE_D3D
