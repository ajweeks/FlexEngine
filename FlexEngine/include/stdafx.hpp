#pragma once

#define NOMINMAX

#define COMPILE_OPEN_GL 1
#define COMPILE_VULKAN 1

#pragma warning(disable : 4201)
#pragma warning(disable : 4820)
#pragma warning(disable : 4868)
#pragma warning(disable : 4710)

#if COMPILE_VULKAN
#pragma warning(push, 0) // Don't generate warnings for 3rd party code    
	#include <glad/glad.h>
	#include <vulkan/vulkan.h>
	#include <GLFW/glfw3.h>
#pragma warning(pop)

	#include "Graphics/Vulkan/VulkanRenderer.hpp"
	#include "Window/Vulkan/VulkanWindowWrapper.hpp"
#endif // COMPILE_VULKAN

#if COMPILE_OPEN_GL
#pragma warning(push, 0) // Don't generate warnings for 3rd party code    
#define GLFW_EXPOSE_NATIVE_WIN32
	#include <glad/glad.h>
	#include <GLFW/glfw3.h>
	#include <GLFW/glfw3native.h>
#pragma warning(pop)

#if _DEBUG
#ifndef CheckGLErrorMessages
void _CheckGLErrorMessages(const char *file, int line);
#define CheckGLErrorMessages() _CheckGLErrorMessages(__FILE__,__LINE__)
#endif
#else
#define CheckGLErrorMessages() 
#endif

	#include "Graphics/GL/GLRenderer.hpp"
	#include "Window/GL/GLWindowWrapper.hpp"


#endif // COMPILE_OPEN_GL

template<class T>
inline void SafeDelete(T &pObjectToDelete)
{
	if (pObjectToDelete != 0)
	{
		delete(pObjectToDelete);
		pObjectToDelete = 0;
	}
}

#pragma warning(push, 0) // Don't generate warnings for 3rd party code    
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED

#include <glm/glm.hpp>
#pragma warning(pop)

#define PI (glm::pi<float>())
#define TWO_PI (glm::two_pi<float>())
#define PI_DIV_TWO (glm::half_pi<float>())
#define PI_DIV_FOUR (glm::quarter_pi<float>())
#define THREE_OVER_TWO_PI (glm::three_over_two_pi<float>())

namespace flex
{
	static const std::string RESOURCE_LOCATION = "FlexEngine/resources/";
} // namspace flex
