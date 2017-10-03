#pragma once

#include <string>

#include "GameContext.hpp"
#include "InputManager.hpp"
#include "Typedefs.hpp"

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

		Window(const std::string& title, glm::vec2 size, GameContext& gameContext);
		virtual ~Window();

		// Return time in miliseconds since app start
		virtual float GetTime() = 0;

		virtual void Update(const GameContext& gameContext);
		virtual void PollEvents() = 0;

		glm::vec2i GetSize() const;
		virtual void SetSize(int width, int height) = 0;
		glm::vec2i GetFrameBufferSize() const;
		virtual void SetFrameBufferSize(int width, int height) = 0;
		bool HasFocus() const;

		void SetTitleString(const std::string& title);
		void SetShowFPSInTitleBar(bool showFPS);
		void SetShowMSInTitleBar(bool showMS);
		// Set to 0 to update window title every frame
		void SetUpdateWindowTitleFrequency(float updateFrequencyinSeconds);

		std::string GetTitle() const;

		virtual void SetCursorMode(CursorMode mode);

		// Callbacks
		virtual void KeyCallback(InputManager::KeyCode keycode, InputManager::Action action, int mods);
		virtual void CharCallback(unsigned int character);
		virtual void MouseButtonCallback(InputManager::MouseButton mouseButton, InputManager::Action action, int mods);
		virtual void WindowFocusCallback(int focused);
		virtual void CursorPosCallback(double x, double y);
		virtual void ScrollCallback(double xoffset, double yoffset);
		virtual void WindowSizeCallback(int width, int height);
		virtual void FrameBufferSizeCallback(int width, int height);
	protected:

#if COMPILE_OPEN_GL || COMPILE_VULKAN
		friend void GLFWKeyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods);
		friend void GLFWCharCallback(GLFWwindow* glfwWindow, unsigned int character);
		friend void GLFWMouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods);
		friend void GLFWWindowFocusCallback(GLFWwindow* glfwWindow, int focused);
		friend void GLFWCursorPosCallback(GLFWwindow* glfwWindow, double x, double y);
		friend void GLFWScrollCallback(GLFWwindow* glfwWindow, double xoffset, double yoffset);
		friend void GLFWWindowSizeCallback(GLFWwindow* glfwWindow, int width, int height);
		friend void GLFWFramebufferSizeCallback(GLFWwindow* glfwWindow, int width, int height);
#endif // COMPILE_OPEN_GL || COMPILE_VULKAN

#if COMPILE_D3D
		friend LRESULT CALLBACK WndProcedure(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif // COMPILE_D3D

		//void UpdateWindowSize(int width, int height);
		//void UpdateWindowSize(glm::vec2i windowSize);
		//void UpdateWindowFocused(int focused);

		virtual void SetWindowTitle(const std::string& title) = 0;

		// Called when we want to manually position the cursor
		virtual void SetMousePosition(glm::vec2 mousePosition) = 0;

		// Store this privately so we can access it in callbacks
		// Should be updated with every call to Update()
		GameContext& m_GameContextRef;

		std::string m_TitleString;
		glm::vec2i m_Size;
		glm::vec2i m_FrameBufferSize;
		bool m_HasFocus;

		bool m_ShowFPSInWindowTitle;
		bool m_ShowMSInWindowTitle;

		float m_UpdateWindowTitleFrequency;
		float m_SecondsSinceTitleUpdate;

		CursorMode m_CursorMode;

	private:
		std::string GenerateWindowTitle(float dt);

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

	};
} // namepace flex
