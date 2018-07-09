#pragma once
#if COMPILE_VULKAN

#define GLFW_INCLUDE_VULKAN
#include "Window/GLFWWindowWrapper.hpp"

namespace flex
{
	namespace vk
	{
		class VulkanWindowWrapper : public GLFWWindowWrapper
		{
		public:
			VulkanWindowWrapper(std::string title);
			virtual ~VulkanWindowWrapper();

			virtual void Create(glm::vec2i size, glm::vec2i pos) override;

		private:

			VulkanWindowWrapper(const VulkanWindowWrapper&) = delete;
			VulkanWindowWrapper& operator=(const VulkanWindowWrapper&) = delete;
		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN
