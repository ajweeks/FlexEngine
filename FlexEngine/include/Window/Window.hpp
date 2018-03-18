#pragma once

#include <string>

#include "GameContext.hpp"
#include "InputManager.hpp"

struct GLFWwindow;

namespace flex
{
	class Window
	{
	public:
		enum class CursorMode
		{
			NORMAL,
			HIDDEN,
			DISABLED
		};

		Window(const std::string& title, glm::vec2 size, glm::vec2 startingPos, GameContext& gameContext);
		virtual ~Window();
		
		virtual void Initialize() = 0;
		virtual void RetrieveMonitorInfo(GameContext& gameContext) = 0;
		virtual void Create() = 0;

		virtual void Update(const GameContext& gameContext);
		virtual void PollEvents() = 0;

		glm::vec2i GetSize() const;
		virtual void SetSize(i32 width, i32 height) = 0;
		glm::vec2i GetPosition() const;
		virtual void SetPosition(i32 newX, i32 newY) = 0;
		glm::vec2i GetFrameBufferSize() const;
		virtual void SetFrameBufferSize(i32 width, i32 height) = 0;
		bool HasFocus() const;

		void SetShowFPSInTitleBar(bool showFPS);
		void SetShowMSInTitleBar(bool showMS);
		// Set to 0 to update window title every frame
		void SetUpdateWindowTitleFrequency(real updateFrequencyinSeconds);

		std::string GetTitle() const;

		virtual void SetCursorMode(CursorMode mode);

		// Callbacks
		virtual void KeyCallback(InputManager::KeyCode keycode, InputManager::Action action, i32 mods);
		virtual void CharCallback(u32 character);
		virtual void MouseButtonCallback(InputManager::MouseButton mouseButton, InputManager::Action action, i32 mods);
		virtual void WindowFocusCallback(i32 focused);
		virtual void CursorPosCallback(double x, double y);
		virtual void ScrollCallback(double xoffset, double yoffset);
		virtual void WindowSizeCallback(i32 width, i32 height);
		virtual void WindowPosCallback(i32 newX, i32 newY);
		virtual void FrameBufferSizeCallback(i32 width, i32 height);
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

#if COMPILE_D3D
		friend LRESULT CALLBACK WndProcedure(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif // COMPILE_D3D

		//void UpdateWindowSize(i32 width, i32 height);
		//void UpdateWindowSize(glm::vec2i windowSize);
		//void UpdateWindowFocused(i32 focused);

		virtual void SetWindowTitle(const std::string& title) = 0;

		// Called when we want to manually position the cursor
		virtual void SetMousePosition(glm::vec2 mousePosition) = 0;

		// Store this privately so we can access it in callbacks
		// Should be updated with every call to Update()
		GameContext& m_GameContextRef;

		std::string m_TitleString = "";
		glm::vec2i m_Size = { 0, 0 };
		glm::vec2i m_StartingPosition = { 0, 0 };
		glm::vec2i m_Position = { 0, 0 };
		glm::vec2i m_FrameBufferSize = { 0, 0 };
		bool m_HasFocus = false;

		bool m_ShowFPSInWindowTitle = true;
		bool m_ShowMSInWindowTitle = true;

		real m_UpdateWindowTitleFrequency = 0;
		real m_SecondsSinceTitleUpdate = 0;

		CursorMode m_CursorMode = CursorMode::NORMAL;

	private:
		std::string GenerateWindowTitle(real dt);

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

	};
} // namepace flex
