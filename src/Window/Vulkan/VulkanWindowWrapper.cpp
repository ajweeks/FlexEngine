#include "stdafx.h"
#if COMPILE_VULKAN

#include "Window/Vulkan/VulkanWindowWrapper.h"
#include "Logger.h"

VulkanWindowWrapper::VulkanWindowWrapper(std::string title, glm::vec2i size, glm::vec2i pos, GameContext& gameContext) :
	GLFWWindowWrapper(title, size, gameContext)
{
	glfwSetErrorCallback(GLFWErrorCallback);

	if (!glfwInit())
	{
		exit(EXIT_FAILURE);
	}

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
	glfwSetScrollCallback(m_Window, GLFWScrollCallback);
	glfwSetWindowFocusCallback(m_Window, GLFWWindowFocusCallback);

	glfwSetWindowPos(m_Window, pos.x, pos.y);

	glfwFocusWindow(m_Window);
	m_HasFocus = true;
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
	GLFWCursorPosCallback(glfwWindow, x, y);
}

#endif // COMPILE_VULKAN
