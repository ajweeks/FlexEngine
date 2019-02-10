#pragma once

#include "InputTypes.hpp"

struct GLFWwindow;

namespace flex
{
	enum class CursorMode
	{
		NORMAL,
		HIDDEN,
		DISABLED,

		_NONE
	};

	enum class WindowMode
	{
		WINDOWED,
		WINDOWED_FULLSCREEN, // (aka "Borderless windowed")
		FULLSCREEN,

		_NONE
	};

	static const char* WindowModeStrings[] =
	{
		"Windowed",
		"Windowed Fullscreen",
		"Fullscreen",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(WindowModeStrings) == (u32)WindowMode::_NONE + 1, "WindowModeStrings length must match WindowMode enum");

	class Window
	{
	public:
		Window(const std::string& title);
		virtual ~Window();

		virtual void Initialize() = 0;
		/* Called after the window has been created */
		virtual void PostInitialize() = 0;
		virtual void Destroy() = 0;

		// Size and pos are only used if "window-settings" config file does not exist
		virtual void Create(const glm::vec2i& size, const glm::vec2i& pos) = 0;

		virtual void RetrieveMonitorInfo() = 0;

		virtual void Update();
		virtual void PollEvents() = 0;

		glm::vec2i GetSize() const;
		/* Sets the window's size */
		virtual void SetSize(i32 width, i32 height) = 0;
		/* Called when the window's size changes */
		virtual void OnSizeChanged(i32 width, i32 height) = 0;

		glm::vec2i GetPosition() const;
		/* Set the window's position */
		virtual void SetPosition(i32 newX, i32 newY) = 0;
		/* Called when the window's position changes */
		virtual void OnPositionChanged(i32 newX, i32 newY) = 0;

		glm::vec2i GetFrameBufferSize() const;
		virtual void SetFrameBufferSize(i32 width, i32 height) = 0;

		/* Returns whether or not this window is the last window the user interacted with */
		bool HasFocus() const;

		// Set to 0 to update window title every frame
		void SetUpdateWindowTitleFrequency(real updateFrequencyinSeconds);

		std::string GetTitle() const;

		virtual void SetCursorPos(const glm::vec2& newCursorPos) = 0;
		virtual void SetCursorMode(CursorMode mode);

		/* If force, window mode will be updated even if value hasn't changed */
		virtual void SetWindowMode(WindowMode mode, bool force = false) = 0;
		/* Toggles between fullscreen and the last used non-fullscreen mode (windowed or borderless windowed */
		virtual void ToggleFullscreen(bool force = false) = 0;
		WindowMode GetWindowMode();

		const char* WindowModeToStr(WindowMode mode);
		WindowMode StrToWindowMode(const char* modeStr);

		// Callbacks
		virtual void KeyCallback(KeyCode keycode, KeyAction action, i32 mods);
		virtual void CharCallback(u32 character);
		virtual void MouseButtonCallback(MouseButton mouseButton, KeyAction action, i32 mods);
		virtual void WindowFocusCallback(i32 focused);
		virtual void CursorPosCallback(double x, double y);
		virtual void ScrollCallback(double xoffset, double yoffset);
		virtual void WindowSizeCallback(i32 width, i32 height, bool bMaximized, bool bIconified);
		virtual void WindowPosCallback(i32 newX, i32 newY);
		virtual void FrameBufferSizeCallback(i32 width, i32 height);

		bool IsMaximized() const;
		virtual void Maximize() = 0;
		bool IsIconified() const;
		virtual void Iconify() = 0;

		bool InitFromConfig();
		void SaveToConfig();

		void DrawImGuiObjects();

		bool GetVSyncEnabled() const;
		void SetVSyncEnabled(bool bEnabled);

	protected:

#if COMPILE_OPEN_GL || COMPILE_VULKAN
		friend void GLFWKeyCallback(GLFWwindow* glfwWindow, i32 key, i32 scancode, i32 action, i32 mods);
		friend void GLFWCharCallback(GLFWwindow* glfwWindow, u32 character);
		friend void GLFWMouseButtonCallback(GLFWwindow* glfwWindow, i32 button, i32 action, i32 mods);
		friend void GLFWWindowFocusCallback(GLFWwindow* glfwWindow, i32 focused);
		friend void GLFWCursorPosCallback(GLFWwindow* glfwWindow, double x, double y);
		friend void GLFWScrollCallback(GLFWwindow* glfwWindow, double xoffset, double yoffset);
		friend void GLFWWindowSizeCallback(GLFWwindow* glfwWindow, i32 width, i32 height);
		friend void GLFWWindowPosCallback(GLFWwindow* glfwWindow, i32 width, i32 height);
		friend void GLFWFramebufferSizeCallback(GLFWwindow* glfwWindow, i32 width, i32 height);
#endif // COMPILE_OPEN_GL || COMPILE_VULKAN

		//void UpdateWindowSize(i32 width, i32 height);
		//void UpdateWindowSize(glm::vec2i windowSize);
		//void UpdateWindowFocused(i32 focused);

		virtual void SetWindowTitle(const std::string& title) = 0;

		// Called when we want to manually position the cursor
		virtual void SetMousePosition(glm::vec2 mousePosition) = 0;

		std::string m_TitleString = "";
		glm::vec2i m_Size = { 0, 0 };
		glm::vec2i m_StartingPosition = { 0, 0 };
		glm::vec2i m_Position = { 0, 0 };
		glm::vec2i m_FrameBufferSize = { 0, 0 };
		bool m_bHasFocus = false;

		static std::string s_ConfigFilePath;

		// Whether to move the console to an additional monitor when present
		bool m_bMoveConsoleToOtherMonitor = true;
		// Whether to restore the size and position from the previous session on bootup
		bool m_bAutoRestoreStateOnBootup = true;

		WindowMode m_CurrentWindowMode = WindowMode::_NONE;

		// Used to store previous window size and position to restore after exiting fullscreen
		glm::vec2i m_LastWindowedSize;
		glm::vec2i m_LastWindowedPos;
		WindowMode m_LastNonFullscreenWindowMode; // Stores which mode we were in before entering fullscreen

		bool m_bShowFPSInWindowTitle = true;
		bool m_bShowMSInWindowTitle = true;
		bool m_bMaximized = false;
		bool m_bIconified = false;
		bool m_bVSyncEnabled = true;

		real m_UpdateWindowTitleFrequency = 0.0f;
		real m_SecondsSinceTitleUpdate = 0.0f;

		CursorMode m_CursorMode = CursorMode::NORMAL;

	private:
		std::string GenerateWindowTitle();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

	};
} // namepace flex
