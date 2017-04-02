#pragma once

#define COMPILE_OPEN_GL 0
#define COMPILE_VULKAN 1

#if COMPILE_VULKAN
	#include <glad/glad.h>
	#define GLFW_INCLUDE_VULKAN
	#include <GLFW/glfw3.h>

	#include "Graphics/VulkanRenderer.h"
	#include "Window/VulkanWindowWrapper.h"
#endif

#if COMPILE_OPEN_GL
	#include <glad/glad.h>
	#define GLFW_INCLUDE_NONE
	#include <GLFW/glfw3.h>

	#include "Graphics/GLRenderer.h"
	#include "Window/GLWindowWrapper.h"
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

//#define TINYOBJLOADER_IMPLEMENTATION
//#include <tiny_obj_loader.h>