#pragma once
#if COMPILE_OPEN_GL || COMPILE_VULKAN

#include "Window.h"

#include <GLFW\glfw3.h>

struct GameContext;

class GLFWWindowWrapper : public Window
{
public:
	GLFWWindowWrapper(std::string title, glm::vec2i size, GameContext& gameContext);
	virtual ~GLFWWindowWrapper();
	
	virtual float GetTime() override;

	virtual void Update(const GameContext& gameContext) override;
	virtual void PollEvents() override;
	virtual void SetCursorMode(CursorMode mode) override;

	GLFWwindow* GetWindow() const;

protected:
	virtual void SetWindowTitle(const std::string& title) override;
	virtual void SetMousePosition(glm::vec2 mousePosition) override;

	GLFWwindow* m_Window;

private:
	static const int NUM_ICONS = 3;
	GLFWimage icons[NUM_ICONS];

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
void GLFWMouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods);
void GLFWWindowFocusCallback(GLFWwindow* glfwWindow, int focused);
void GLFWCursorPosCallback(GLFWwindow* glfwWindow, double x, double y);
void GLFWWindowSizeCallback(GLFWwindow* glfwWindow, int width, int height);

#endif // COMPILE_OPEN_GL || COMPILE_VULKAN