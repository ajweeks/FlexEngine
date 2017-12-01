#pragma once
#if COMPILE_OPEN_GL || COMPILE_VULKAN

#include <GLFW/glfw3.h>

#include "Window.hpp"

namespace flex
{
	class GLFWWindowWrapper : public Window
	{
	public:
		GLFWWindowWrapper(std::string title, glm::vec2i size, glm::vec2i startingPos, GameContext& gameContext);
		virtual ~GLFWWindowWrapper();

		virtual void Initialize() override;
		virtual void RetrieveMonitorInfo(GameContext& gameContext) override;
		void SetUpCallbacks();

		virtual float GetTime() override;

		virtual void SetSize(int width, int height) override;
		virtual void SetPosition(int newX, int newY) override;

		virtual void Update(const GameContext& gameContext) override;
		virtual void PollEvents() override;
		virtual void SetCursorMode(CursorMode mode) override;

		GLFWwindow* GetWindow() const;

		const char* GetClipboardText();
		void SetClipboardText(const char* text);


	protected:
		virtual void SetWindowTitle(const std::string& title) override;
		virtual void SetMousePosition(glm::vec2 mousePosition) override;

		GLFWwindow* m_Window;

		std::vector<GLFWimage> m_WindowIcons;

	private:

		float m_PreviousFrameTime;

		GLFWWindowWrapper(const GLFWWindowWrapper&) = delete;
		GLFWWindowWrapper& operator=(const GLFWWindowWrapper&) = delete;
	};

	InputManager::Action GLFWActionToInputManagerAction(int glfwAction);
	InputManager::KeyCode GLFWKeyToInputManagerKey(int glfwKey);
	int GLFWModsToInputManagerMods(int glfwMods);
	InputManager::MouseButton GLFWButtonToInputManagerMouseButton(int glfwButton);

	void GLFWErrorCallback(int error, const char* description);
	void GLFWKeyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods);
	void GLFWCharCallback(GLFWwindow* glfwWindow, unsigned int character);
	void GLFWMouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods);
	void GLFWWindowFocusCallback(GLFWwindow* glfwWindow, int focused);
	void GLFWCursorPosCallback(GLFWwindow* glfwWindow, double x, double y);
	void GLFWWindowSizeCallback(GLFWwindow* glfwWindow, int width, int height);
	void GLFWWindowPosCallback(GLFWwindow* glfwWindow, int newX, int newY);
	void GLFWFramebufferSizeCallback(GLFWwindow* glfwWindow, int width, int height);

} // namespace flex

#endif // COMPILE_OPEN_GL || COMPILE_VULKAN