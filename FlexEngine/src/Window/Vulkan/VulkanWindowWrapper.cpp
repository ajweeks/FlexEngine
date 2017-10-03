#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Window/Vulkan/VulkanWindowWrapper.hpp"

#include "Logger.hpp"

namespace flex
{
	namespace vk
	{
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
			glfwSetCharCallback(m_Window, GLFWCharCallback);
			glfwSetMouseButtonCallback(m_Window, GLFWMouseButtonCallback);
			glfwSetCursorPosCallback(m_Window, VulkanCursorPosCallback);
			glfwSetScrollCallback(m_Window, GLFWScrollCallback);
			glfwSetWindowFocusCallback(m_Window, GLFWWindowFocusCallback);
			glfwSetWindowSizeCallback(m_Window, GLFWWindowSizeCallback);
			glfwSetFramebufferSizeCallback(m_Window, GLFWFramebufferSizeCallback);
			glfwSetCharCallback(m_Window, GLFWCharCallback);

			glfwSetWindowPos(m_Window, pos.x, pos.y);

			glfwFocusWindow(m_Window);
			m_HasFocus = true;

			// TODO: Move duplicated code between glfw window classes to consolidated functions
			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* vidMode = glfwGetVideoMode(monitor);

			gameContext.monitor.width = vidMode->width;
			gameContext.monitor.height = vidMode->height;
			gameContext.monitor.redBits = vidMode->redBits;
			gameContext.monitor.greenBits = vidMode->greenBits;
			gameContext.monitor.blueBits = vidMode->blueBits;
			gameContext.monitor.refreshRate = vidMode->refreshRate;
		}

		VulkanWindowWrapper::~VulkanWindowWrapper()
		{
		}

		void VulkanWindowWrapper::SetSize(int width, int height)
		{
			m_Size = glm::vec2i(width, height);
			m_GameContextRef.renderer->OnWindowSize(width, height);
		}

		void VulkanWindowWrapper::SetFrameBufferSize(int width, int height)
		{
			m_FrameBufferSize = glm::vec2i(width, height);
			// TODO: Call OnFrameBufferSize here?
			m_GameContextRef.renderer->OnWindowSize(width, height);
		}

		void VulkanWindowWrapper::VulkanCursorPosCallback(GLFWwindow* glfwWindow, double x, double y)
		{
			GLFWCursorPosCallback(glfwWindow, x, y);
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN
