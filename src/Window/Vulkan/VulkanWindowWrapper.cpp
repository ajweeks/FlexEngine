#include "stdafx.h"
#if COMPILE_VULKAN

#include "Window/Vulkan/VulkanWindowWrapper.h"
#include "Logger.h"

VulkanWindowWrapper::VulkanWindowWrapper(std::string title, glm::vec2i size, GameContext& gameContext) :
	GLFWWindowWrapper(title, size, gameContext)
{
	glfwSetErrorCallback(GLFWErrorCallback);

	if (!glfwInit())
	{
		exit(EXIT_FAILURE);
	}

	// TODO: Are these two lines needed?
	//glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	//glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

	//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Tell the window to not use OpenGL
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	
	m_Window = glfwCreateWindow(size.x, size.y, title.c_str(), NULL, NULL);
	if (!m_Window)
	{
		glfwTerminate();
		exit(EXIT_FAILURE);
	}

	glfwSetWindowUserPointer(m_Window, this);

	glfwSetKeyCallback(m_Window, GLFWKeyCallback);
	glfwSetMouseButtonCallback(m_Window, GLFWMouseButtonCallback);
	glfwSetCursorPosCallback(m_Window, VulkanCursorPosCallback);
	glfwSetWindowSizeCallback(m_Window, GLFWWindowSizeCallback);
	glfwSetWindowFocusCallback(m_Window, GLFWWindowFocusCallback);

	glfwFocusWindow(m_Window);
	m_HasFocus = true;

	//gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	//gameContext.program = ShaderUtils::LoadShaders("resources/shaders/vk_simple.spirv", "resources/shaders/vk_simple.spirv");

	//icons[0] = LoadGLFWImage("resources/icons/icon_01_48.png");
	//icons[1] = LoadGLFWImage("resources/icons/icon_01_32.png");
	//icons[2] = LoadGLFWImage("resources/icons/icon_01_16.png");
	//
	//glfwSetWindowIcon(m_Window, NUM_ICONS, icons);

	//SOIL_free_image_data(icons[0].pixels);
	//SOIL_free_image_data(icons[1].pixels);
	//SOIL_free_image_data(icons[2].pixels);
}

VulkanWindowWrapper::~VulkanWindowWrapper()
{
}

void VulkanWindowWrapper::SetSize(int width, int height)
{
	m_Size = glm::vec2i(width, height);
	m_GameContextRef.renderer->OnWindowSize(width, height);
}

void VulkanWindowWrapper::VulkanCursorPosCallback(GLFWwindow* glfwWindow, double x, double y)
{
	// Vulkan is whack yo
	GLFWCursorPosCallback(glfwWindow, x, -y);
}

#endif // COMPILE_VULKAN
