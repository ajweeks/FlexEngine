#pragma once

#define GLFW_INCLUDE_VULKAN
#include "GLFWWindowWrapper.h"

struct GameContext;

class VulkanWindowWrapper : public GLFWWindowWrapper
{
public:
	VulkanWindowWrapper(std::string title, glm::vec2i size, GameContext& gameContext);
	virtual ~VulkanWindowWrapper();

private:

	VulkanWindowWrapper(const VulkanWindowWrapper&) = delete;
	VulkanWindowWrapper& operator=(const VulkanWindowWrapper&) = delete;
};
