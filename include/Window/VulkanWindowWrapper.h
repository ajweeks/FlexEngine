#pragma once
#if COMPILE_VULKAN

#define GLFW_INCLUDE_VULKAN
#include "GLFWWindowWrapper.h"

struct GameContext;

class VulkanWindowWrapper : public GLFWWindowWrapper
{
public:
	VulkanWindowWrapper(std::string title, glm::vec2i size, GameContext& gameContext);
	virtual ~VulkanWindowWrapper();

	virtual void SetSize(int width, int height) override;

private:
	static void VulkanCursorPosCallback(GLFWwindow* glfwWindow, double x, double y);

	VulkanWindowWrapper(const VulkanWindowWrapper&) = delete;
	VulkanWindowWrapper& operator=(const VulkanWindowWrapper&) = delete;
};

#endif // COMPILE_VULKAN
