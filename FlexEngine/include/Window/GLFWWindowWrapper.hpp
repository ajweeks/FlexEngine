#pragma once
#if COMPILE_OPEN_GL || COMPILE_VULKAN

#pragma warning(push, 0)
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#pragma warning(pop)

#include "Window.hpp"

namespace flex
{
	class GLFWWindowWrapper : public Window
	{
	public:
		GLFWWindowWrapper(const std::string& title);
		virtual ~GLFWWindowWrapper();

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Destroy() override;
		virtual void Create(const glm::vec2i& size, const glm::vec2i& pos) override;

		virtual void RetrieveMonitorInfo() override;

		virtual void SetSize(i32 width, i32 height) override;
		virtual void OnSizeChanged(i32 width, i32 height) override;
		virtual void SetPosition(i32 newX, i32 newY) override;
		virtual void OnPositionChanged(i32 newX, i32 newY) override;

		virtual void SetFrameBufferSize(i32 width, i32 height) override;

		virtual void Update() override;
		virtual void PollEvents() override;

		virtual void SetCursorPos(const glm::vec2& newCursorPos) override;
		virtual void SetCursorMode(CursorMode mode) override;

		virtual void SetWindowMode(WindowMode mode, bool force = false) override;
		virtual void ToggleFullscreen(bool force = false) override;

		virtual void Maximize() override;
		virtual void Iconify() override;

		GLFWwindow* GetWindow() const;

		void SetUpCallbacks();

		const char* GetClipboardText();
		void SetClipboardText(const char* text);

	protected:
		virtual void SetWindowTitle(const std::string& title) override;
		virtual void SetMousePosition(glm::vec2 mousePosition) override;

		void MoveConsole();

		GLFWwindow* m_Window = nullptr;

		std::vector<GLFWimage> m_WindowIcons;

	private:

		real m_PreviousFrameTime = 0;

		GLFWWindowWrapper(const GLFWWindowWrapper&) = delete;
		GLFWWindowWrapper& operator=(const GLFWWindowWrapper&) = delete;
	};

	InputManager::Action GLFWActionToInputManagerAction(i32 glfwAction);
	InputManager::KeyCode GLFWKeyToInputManagerKey(i32 glfwKey);
	i32 GLFWModsToInputManagerMods(i32 glfwMods);
	InputManager::MouseButton GLFWButtonToInputManagerMouseButton(i32 glfwButton);

	void GLFWErrorCallback(i32 error, const char* description);
	void GLFWKeyCallback(GLFWwindow* glfwWindow, i32 key, i32 scancode, i32 action, i32 mods);
	void GLFWCharCallback(GLFWwindow* glfwWindow, u32 character);
	void GLFWMouseButtonCallback(GLFWwindow* glfwWindow, i32 button, i32 action, i32 mods);
	void GLFWWindowFocusCallback(GLFWwindow* glfwWindow, i32 focused);
	void GLFWCursorPosCallback(GLFWwindow* glfwWindow, double x, double y);
	void GLFWWindowSizeCallback(GLFWwindow* glfwWindow, i32 width, i32 height);
	void GLFWWindowPosCallback(GLFWwindow* glfwWindow, i32 newX, i32 newY);
	void GLFWFramebufferSizeCallback(GLFWwindow* glfwWindow, i32 width, i32 height);
	void GLFWJoystickCallback(i32 JID, i32 event);

	void WINAPI glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity,
							  GLsizei length, const GLchar *message, const void *userParam);

	// Stores whether a controller is connected or not
	const i32 MAX_JOYSTICK_COUNT = 4;
	extern std::array<bool, MAX_JOYSTICK_COUNT> g_JoysticksConnected;

} // namespace flex

#endif // COMPILE_OPEN_GL || COMPILE_VULKAN