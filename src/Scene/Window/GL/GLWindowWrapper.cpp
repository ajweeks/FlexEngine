#include "stdafx.h"
#if COMPILE_OPEN_GL

#include "Window/GL/GLWindowWrapper.h"
#include "Graphics/GL/GLHelpers.h"
#include "Logger.h"
#include "ShaderUtils.h"

GLWindowWrapper::GLWindowWrapper(std::string title, glm::vec2i size, glm::vec2i pos, GameContext& gameContext) :
	GLFWWindowWrapper(title, size, gameContext)
{
	glfwSetErrorCallback(GLFWErrorCallback);

	if (!glfwInit())
	{
		Logger::LogError("Failed to initialize glfw! Exiting");
		exit(EXIT_FAILURE);
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	m_Window = glfwCreateWindow(size.x, size.y, title.c_str(), NULL, NULL);
	if (!m_Window)
	{
		Logger::LogError("Failed to create glfw Window! Exiting");
		glfwTerminate();
		exit(EXIT_FAILURE);
	}

	glfwSetWindowUserPointer(m_Window, this);

	glfwSetKeyCallback(m_Window, GLFWKeyCallback);
	glfwSetMouseButtonCallback(m_Window, GLFWMouseButtonCallback);
	glfwSetCursorPosCallback(m_Window, GLFWCursorPosCallback);
	glfwSetWindowSizeCallback(m_Window, GLFWWindowSizeCallback);
	glfwSetWindowFocusCallback(m_Window, GLFWWindowFocusCallback);

	glfwSetWindowPos(m_Window, pos.x, pos.y);

	glfwFocusWindow(m_Window);
	m_HasFocus = true;

	glfwMakeContextCurrent(m_Window);
	
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	const std::string glVersion(reinterpret_cast<const char*>(glGetString(GL_VERSION)));
	Logger::LogInfo("OpenGL Version: " + glVersion);

	gameContext.program = ShaderUtils::LoadShaders("resources/shaders/GLSL/simple.vert", "resources/shaders/GLSL/simple.frag");
	
	CheckGLErrorMessages();
}

GLWindowWrapper::~GLWindowWrapper()
{
}

void GLWindowWrapper::SetSize(int width, int height)
{
	m_Size = glm::vec2i(width, height);
	m_GameContextRef.renderer->OnWindowSize(width, height);
}

#endif // COMPILE_OPEN_GL