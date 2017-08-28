#pragma once
#if COMPILE_VULKAN

#define GLFW_INCLUDE_VULKAN
#include "Window/GLFWWindowWrapper.h"

namespace flex
{
	namespace vk
	{
		class VulkanWindowWrapper : public GLFWWindowWrapper
		{
		public:
			VulkanWindowWrapper(std::string title, glm::vec2i size, glm::vec2i pos, GameContext& gameContext);
			virtual ~VulkanWindowWrapper();

			virtual void SetSize(int width, int height) override;
			virtual void SetFrameBufferSize(int width, int height) override;

		private:
			static void VulkanCursorPosCallback(GLFWwindow* glfwWindow, double x, double y);

			VulkanWindowWrapper(const VulkanWindowWrapper&) = delete;
			VulkanWindowWrapper& operator=(const VulkanWindowWrapper&) = delete;
		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN
