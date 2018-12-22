#pragma once

// Configuration
#define COMPILE_OPEN_GL 1
#define COMPILE_VULKAN 0
#define COMPILE_IMGUI 1

#ifdef _DEBUG
#define THOROUGH_CHECKS 1
#else
#define THOROUGH_CHECKS 0
#endif

#define ENABLE_PROFILING 1

#define NOMINMAX

#define GLFW_INCLUDE_NONE

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS 1

//#pragma warning(disable : 4201) // nonstandard extension used: nanmeless struct/union
//#pragma warning(disable : 4820) // bytes' bytes padding added after construct 'member_name'
//#pragma warning(disable : 4868) // compiler may not enforce left-to-right evaluation order in braced initializer list
//#pragma warning(disable : 4710) // function not inlined

#include "Logger.hpp"
#include "Types.hpp"

#pragma warning(push, 0)
#include <ft2build.h>
#include FT_FREETYPE_H
#pragma warning(pop)

#if COMPILE_VULKAN
#pragma warning(push, 0)
	#include <glad/glad.h>
	#include <GLFW/glfw3.h>
	#include <GLFW/glfw3native.h>
	#include <vulkan/vulkan.hpp>
#pragma warning(pop)
#endif // COMPILE_VULKAN

#if COMPILE_OPEN_GL
#pragma warning(push, 0)
// TODO: Does this line need to be included above earlier include in Vulkan section?
#define GLFW_EXPOSE_NATIVE_WIN32
#if COMPILE_IMGUI
#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include "imgui.h"
#else
#include <glad/glad.h>
#endif
	#include <GLFW/glfw3.h>
	#include <GLFW/glfw3native.h>
#pragma warning(pop)
#endif // COMPILE_OPEN_GL

#include "Physics/PhysicsTypeConversions.hpp"

template<class T>
inline void SafeDelete(T &pObjectToDelete)
{
	if (pObjectToDelete != nullptr)
	{
		delete(pObjectToDelete);
		pObjectToDelete = nullptr;
	}
}

#ifndef btAssert
#define btAssert(e) assert(e)
#endif

#pragma warning(push, 0)
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED

// For matrix_decompose
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#pragma warning(pop)

#define PI (glm::pi<real>())
#define TWO_PI (glm::two_pi<real>())
#define PI_DIV_TWO (glm::half_pi<real>())
#define PI_DIV_FOUR (glm::quarter_pi<real>())
#define THREE_PI_DIV_TWO (glm::three_over_two_pi<real>())
#define EPSILON (glm::epsilon<real>())

#if ENABLE_PROFILING
#define PROFILE_BEGIN(blockName) Profiler::Begin(blockName);
#define PROFILE_END(blockName) Profiler::End(blockName);
#define PROFILE_AUTO(blockName) AutoProfilerBlock autoProfileBlock(blockName);
#else
#define PROFILE_BEGIN(blockName)
#define PROFILE_END(blockName)
#define PROFILE_AUTO(blockName)
#endif

#define ENSURE_NO_ENTRY() { PrintError("Execution entered no entry path! %s\n", __FUNCTION__); assert(false); }
#ifdef _DEBUG
#define ENSURE(condition) if (!(condition)) { PrintError("Ensure failed! File: %s, Line: %d\n", __FILE__, __LINE__); assert(false); }
#else
#define ENSURE(condition)
#endif

namespace flex
{
#define ROOT_LOCATION "../../../FlexEngine/"
#define RESOURCE_LOCATION "../../../FlexEngine/resources/"
#define RESOURCE(path) "../../../FlexEngine/resources/" path
#define RESOURCE_STR(path) "../../../FlexEngine/resources/" + path

	static const glm::vec3 VEC3_RIGHT = glm::vec3(1.0f, 0.0f, 0.0f);
	static const glm::vec3 VEC3_UP = glm::vec3(0.0f, 1.0f, 0.0f);
	static const glm::vec3 VEC3_FORWARD = glm::vec3(0.0f, 0.0f, 1.0f);
	static const glm::vec2 VEC2_ONE = glm::vec2(1.0f);
	static const glm::vec2 VEC2_ZERO = glm::vec2(0.0f);
	static const glm::vec3 VEC3_ONE = glm::vec3(1.0f);
	static const glm::vec3 VEC3_ZERO = glm::vec3(0.0f);
	static const glm::vec4 VEC4_ONE = glm::vec4(1.0f);
	static const glm::vec4 VEC4_ZERO = glm::vec4(0.0f);
	static const glm::quat QUAT_UNIT = glm::quat(VEC3_ZERO);
	static const glm::mat4 MAT4_IDENTITY = glm::mat4(1.0f);
	static const glm::mat4 MAT4_ZERO = glm::mat4(0.0f);

	// These fields are defined and initialized in FlexEngine.cpp
	extern class Window* g_Window;
	extern class CameraManager* g_CameraManager;
	namespace Input
	{
		class Manager;
	}
	extern Input::Manager* g_InputManager;
	extern class Renderer* g_Renderer;
	extern class FlexEngine* g_EngineInstance;
	extern class SceneManager* g_SceneManager;
	extern struct Monitor* g_Monitor;
	extern class PhysicsManager* g_PhysicsManager;

	extern sec g_SecElapsedSinceProgramStart;
	extern sec g_DeltaTime;
}
