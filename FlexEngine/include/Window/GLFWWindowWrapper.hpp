#pragma once
#if COMPILE_OPEN_GL || COMPILE_VULKAN

#include <GLFW/glfw3.h>

#include "Window.hpp"

namespace flex
{
	class GLFWWindowWrapper : public Window
	{
	public:
		GLFWWindowWrapper(std::string title, GameContext& gameContext);
		virtual ~GLFWWindowWrapper();

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Destroy() override;

		virtual void RetrieveMonitorInfo(GameContext& gameContext) override;

		virtual void SetSize(i32 width, i32 height) override;
		virtual void OnSizeChanged(i32 width, i32 height) override;
		virtual void SetPosition(i32 newX, i32 newY) override;
		virtual void OnPositionChanged(i32 newX, i32 newY) override;

		virtual void SetFrameBufferSize(i32 width, i32 height) override;

		virtual void Update(const GameContext& gameContext) override;
		virtual void PollEvents() override;
		virtual void SetCursorMode(CursorMode mode) override;

		virtual void SetFullscreenMode(FullscreenMode mode, bool force = false) override;
		virtual void ToggleFullscreen(bool force = false) override;

		GLFWwindow* GetWindow() const;

		void SetUpCallbacks();

		const char* GetClipboardText();
		void SetClipboardText(const char* text);

	protected:
		virtual void SetWindowTitle(const std::string& title) override;
		virtual void SetMousePosition(glm::vec2 mousePosition) override;

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

} // namespace flex

#endif // COMPILE_OPEN_GL || COMPILE_VULKAN