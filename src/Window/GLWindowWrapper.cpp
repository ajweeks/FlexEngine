#include "stdafx.h"
#if COMPILE_OPEN_GL

#include "Window/GLWindowWrapper.h"
#include "Logger.h"
#include "ShaderUtils.h"

GLWindowWrapper::GLWindowWrapper(std::string title, glm::vec2i size, GameContext& gameContext) :
	GLFWWindowWrapper(title, size, gameContext)
{
	glfwSetErrorCallback(GLFWErrorCallback);

	if (!glfwInit())
	{
		exit(EXIT_FAILURE);
	}

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

	const std::string glVersion(reinterpret_cast<const char*>(glGetString(GL_VERSION)));
	Logger::LogInfo("OpenGL Version: " + glVersion);

	gameContext.program = ShaderUtils::LoadShaders("resources/shaders/simple.vert", "resources/shaders/simple.frag");
	
	//icons[0] = LoadGLFWImage("resources/icons/icon_01_48.png");
	//icons[1] = LoadGLFWImage("resources/icons/icon_01_32.png");
	//icons[2] = LoadGLFWImage("resources/icons/icon_01_16.png");
	//
	//glfwSetWindowIcon(m_Window, NUM_ICONS, icons);
	//
	//SOIL_free_image_data(icons[0].pixels);
	//SOIL_free_image_data(icons[1].pixels);
	//SOIL_free_image_data(icons[2].pixels);
}

GLWindowWrapper::~GLWindowWrapper()
{
}

void GLWindowWrapper::SetSize(int width, int height)
{
	m_Size = glm::vec2i(width, height);
	glViewport(0, 0, width, height);
}

#endif // COMPILE_OPEN_GL