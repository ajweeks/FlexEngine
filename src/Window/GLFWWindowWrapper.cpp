
#include "Window/GLFWWindowWrapper.h"
#include "Logger.h"
#include "GLHelpers.h"
#include "GameContext.h"
#include "ShaderUtils.h"
#include "TechDemo.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

void GLFWErrorCallback(int error, const char* description)
{
	Logger::LogError("GL Error: " + std::string(description));
}

void GLFWKeyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods)
{
	Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
	window->KeyCallback(key, scancode, action, mods);
}

void GLFWMouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods)
{
	Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
	window->MouseButtonCallback(button, action, mods);
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

void GLFWWindowSizeCallback(GLFWwindow* glfwWindow, int width, int height)
{
	Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
	window->WindowSizeCallback(width, height);
}

GLFWWindowWrapper::GLFWWindowWrapper(std::string title, glm::vec2 size, GameContext& gameContext) :
	Window(gameContext, title, size)
{
	glfwSetErrorCallback(GLFWErrorCallback);

	if (!glfwInit())
	{
		exit(EXIT_FAILURE);
	}

	// Call before window creation
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	m_Window = glfwCreateWindow(size.x, size.y, title.c_str(), NULL, NULL);
	if (!m_Window)
	{
		glfwTerminate();
		exit(EXIT_FAILURE);
	}

	glfwSetWindowUserPointer(m_Window, this);

	glfwSetKeyCallback(m_Window, GLFWKeyCallback);
	glfwSetMouseButtonCallback(m_Window, GLFWMouseButtonCallback);
	glfwSetCursorPosCallback(m_Window, GLFWCursorPosCallback);
	glfwSetWindowSizeCallback(m_Window, GLFWWindowSizeCallback);
	glfwSetWindowFocusCallback(m_Window, GLFWWindowFocusCallback);

	glfwFocusWindow(m_Window);
	m_HasFocus = true;

	glfwMakeContextCurrent(m_Window);
	
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	Logger::LogInfo("OpenGL Version: " + std::string(reinterpret_cast<const char*>(glGetString(GL_VERSION))));

	gameContext.program = ShaderUtils::LoadShaders("resources/shaders/simple.vert", "resources/shaders/simple.frag");

	icons[0] = LoadGLFWImage("resources/icons/icon_01_48.png");
	icons[1] = LoadGLFWImage("resources/icons/icon_01_32.png");
	icons[2] = LoadGLFWImage("resources/icons/icon_01_16.png");

	glfwSetWindowIcon(m_Window, NUM_ICONS, icons);

	SOIL_free_image_data(icons[0].pixels);
	SOIL_free_image_data(icons[1].pixels);
	SOIL_free_image_data(icons[2].pixels);
}

GLFWWindowWrapper::~GLFWWindowWrapper()
{
	if (m_Window)
	{
		glfwDestroyWindow(m_Window);
		m_Window = nullptr;
	}
}

void GLFWWindowWrapper::PollEvents()
{
	glfwPollEvents();
}

void GLFWWindowWrapper::Update(const GameContext& gameContext)
{
	Window::Update(gameContext);

	if (glfwWindowShouldClose(m_Window))
	{
		gameContext.mainApp->Stop();
		return;
	}
}

GLFWwindow* GLFWWindowWrapper::GetWindow() const
{
	return m_Window;
}

void GLFWWindowWrapper::SetWindowTitle(std::string title)
{
	glfwSetWindowTitle(m_Window, title.c_str());
}

