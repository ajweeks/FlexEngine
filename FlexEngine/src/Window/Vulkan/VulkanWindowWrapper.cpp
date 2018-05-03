#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Window/Vulkan/VulkanWindowWrapper.hpp"
#include "Logger.hpp"

namespace flex
{
	namespace vk
	{
		VulkanWindowWrapper::VulkanWindowWrapper(std::string title, GameContext& gameContext) :
			GLFWWindowWrapper(title, gameContext)
		{
		}

		VulkanWindowWrapper::~VulkanWindowWrapper()
		{
		}

		void VulkanWindowWrapper::Create(glm::vec2i size, glm::vec2i pos)
		{
			m_Size = size;
			m_FrameBufferSize = size;
			m_LastWindowedSize = size;
			m_Position = pos;
			m_StartingPosition = pos;
			m_LastWindowedPos = pos;

			if (glfwVulkanSupported() != GLFW_TRUE)
			{
				Logger::LogError("This device does not support Vulkan! Aborting");
				exit(EXIT_FAILURE);
			}

			// Tell the window we're not using OpenGL
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

			// Don't hide window when losing focus in Windowed Fullscreen
			glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

			m_Window = glfwCreateWindow(m_Size.x, m_Size.y, m_TitleString.c_str(), NULL, NULL);
			if (!m_Window)
			{
				Logger::LogError("Failed to create glfw Window! Exiting");
				glfwTerminate();
				// TODO: Try creating a window manually here
				exit(EXIT_FAILURE);
			}

			glfwSetWindowUserPointer(m_Window, this);

			SetUpCallbacks();

			glfwSetWindowPos(m_Window, m_StartingPosition.x, m_StartingPosition.y);

			glfwFocusWindow(m_Window);
			m_HasFocus = true;

			if (!m_WindowIcons.empty() && m_WindowIcons[0].pixels)
			{
				glfwSetWindowIcon(m_Window, m_WindowIcons.size(), m_WindowIcons.data());
			}
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN
