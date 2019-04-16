#pragma once

#define COMPILE_OPEN_GL 1
#define COMPILE_VULKAN 0

#define COMPILE_IMGUI 1

#if COMPILE_OPEN_GL
const bool g_bOpenGLEnabled = true;
#else
const bool g_bOpenGLEnabled = false;
#endif

#if COMPILE_VULKAN
const bool g_bVulkanEnabled = true;
#else
const bool g_bVulkanEnabled = false;
#endif

const bool g_bEnableLogging_Loading = false;

#ifdef DEBUG
#define THOROUGH_CHECKS 1
#define ENABLE_PROFILING 1
#define COMPILE_RENDERDOC_API 0
#else
#define THOROUGH_CHECKS 0
#define ENABLE_PROFILING 0
#define COMPILE_RENDERDOC_API 0
#endif

#define VC_EXTRALEAN

#define BT_NO_SIMD_OPERATOR_OVERLOADS
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_INCLUDE_NONE

#if COMPILE_IMGUI
#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS 1
#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS 1
#endif

#define FLEX_VERSION(major, minor, patch) (((major) << 22) | ((minor) << 12) | (patch))

#if defined(__clang__)
#define IGNORE_WARNINGS_PUSH \
		_Pragma("clang diagnostic push") \
		_Pragma("clang diagnostic ignored \"-Weverything\"")
#define IGNORE_WARNINGS_POP _Pragma("clang diagnostic pop")
#elif defined(_MSC_VER)
#define IGNORE_WARNINGS_PUSH __pragma(warning(push, 0))
#define IGNORE_WARNINGS_POP __pragma(warning(pop))
#else
	// Unhandled compiler
	#error
#endif

#undef FORMAT_STRING
#if defined(__clang__)
#define FORMAT_STRING __attribute__ (( format( __printf__, fmtargnumber, firstvarargnumber )))
#elif defined(_MSC_VER)
#define FORMAT_STRING _Printf_format_string_
#endif

#define FT_EXPORT(Type) Type

//#pragma warning(disable : 4201) // nonstandard extension used: nameless struct/union
//#pragma warning(disable : 4820) // bytes' bytes padding added after construct 'member_name'
//#pragma warning(disable : 4868) // compiler may not enforce left-to-right evaluation order in braced initializer list
//#pragma warning(disable : 4710) // function not inlined

#include <algorithm>
#include <functional>
#include <future>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <sstream>
#include <fstream>
#include <array>
#include <set>
#include <limits>
#include <unordered_map>

#include <crtdbg.h>
#include <stdlib.h>
#include <string.h>

#include "MinWindows.hpp"

#include "Logger.hpp"
#include "Types.hpp"

IGNORE_WARNINGS_PUSH
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED

// For matrix_decompose
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
IGNORE_WARNINGS_POP

#if COMPILE_VULKAN
IGNORE_WARNINGS_PUSH
#include <vulkan/vulkan.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
IGNORE_WARNINGS_POP
#endif // COMPILE_VULKAN

#if COMPILE_OPEN_GL
IGNORE_WARNINGS_PUSH

#if !COMPILE_IMGUI
#include <glad/glad.h>
#endif // !COMPILE_IMGUI

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
IGNORE_WARNINGS_POP
#endif // COMPILE_OPEN_GL

#if COMPILE_IMGUI
IGNORE_WARNINGS_PUSH
#include "imgui.h"

namespace flex
{
	extern ImVec4 g_WarningTextColor;
	extern ImVec4 g_WarningButtonColor;
	extern ImVec4 g_WarningButtonHoveredColor;
	extern ImVec4 g_WarningButtonActiveColor;
}
IGNORE_WARNINGS_POP
#endif // COMPILE_IMGUI


#include "Physics/PhysicsTypeConversions.hpp"

#ifndef btAssert
#define btAssert(e) assert(e)
#endif

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

#define DEBUG_BREAK __debugbreak()
// Linux/Max: (untested)
//#define DEBUG_BREAK __builtin_trap()

#define ENSURE_NO_ENTRY() { PrintError("Execution entered no entry path! %s\n", __FUNCTION__); DEBUG_BREAK; }
#ifdef DEBUG
#define ENSURE(condition) if (!(condition)) { PrintError("Ensure failed! File: %s, Line: %d\n", __FILE__, __LINE__); DEBUG_BREAK; }
#else
#define ENSURE(condition)
#endif

#if COMPILE_OPEN_GL
#ifdef DEBUG
#define GL_PUSH_DEBUG_GROUP(str) \
if (FlexEngine::s_bHasGLDebugExtension) { glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION, 0, -1, str); }
#define GL_POP_DEBUG_GROUP() \
if (FlexEngine::s_bHasGLDebugExtension) { glPopDebugGroupKHR(); }
#else
#define GL_PUSH_DEBUG_GROUP(str)
#define GL_POP_DEBUG_GROUP()
#endif // DEBUG
#else
#define GL_PUSH_DEBUG_GROUP(str)
#define GL_POP_DEBUG_GROUP()
#endif // COMPILE_OPEN_GL

namespace flex
{
#define ROOT_LOCATION "..\\..\\..\\FlexEngine\\"
#define SAVED_LOCATION "..\\..\\..\\FlexEngine\\saved\\"
#define RESOURCE_LOCATION "..\\..\\..\\FlexEngine\\resources\\"
#define RESOURCE(path) "..\\..\\..\\FlexEngine\\resources\\" path
#define RESOURCE_STR(path) "..\\..\\..\\FlexEngine\\resources\\" + path

	// TODO: Use int to represent string
	//typedef u32 StringID;
	typedef std::string StringID;

	// TODO: Calculate string hash here
#define SID(str) (str)

	static const glm::vec3 VEC3_RIGHT = glm::vec3(1.0f, 0.0f, 0.0f);
	static const glm::vec3 VEC3_UP = glm::vec3(0.0f, 1.0f, 0.0f);
	static const glm::vec3 VEC3_FORWARD = glm::vec3(0.0f, 0.0f, 1.0f);
	static const glm::vec2 VEC2_ONE = glm::vec2(1.0f);
	static const glm::vec2 VEC2_NEG_ONE = glm::vec2(-1.0f);
	static const glm::vec2 VEC2_ZERO = glm::vec2(0.0f);
	static const glm::vec3 VEC3_ONE = glm::vec3(1.0f);
	static const glm::vec3 VEC3_NEG_ONE = glm::vec3(-1.0f);
	static const glm::vec3 VEC3_ZERO = glm::vec3(0.0f);
	static const glm::vec4 VEC4_ONE = glm::vec4(1.0f);
	static const glm::vec4 VEC4_NEG_ONE = glm::vec4(-1.0f);
	static const glm::vec4 VEC4_ZERO = glm::vec4(0.0f);
	static const glm::quat QUAT_UNIT = glm::quat(VEC3_ZERO);
	static const glm::mat4 MAT4_IDENTITY = glm::mat4(1.0f);
	static const glm::mat4 MAT4_ZERO = glm::mat4(0.0f);

	static const std::string EMPTY_STRING = std::string();

	static const u32 MAX_TEXTURE_DIM = 65536;

	// These fields are defined and initialized in FlexEngine.cpp
	extern class Window* g_Window;
	extern class CameraManager* g_CameraManager;

	extern class InputManager* g_InputManager;
	extern class Renderer* g_Renderer;
	extern class FlexEngine* g_EngineInstance;
	extern class SceneManager* g_SceneManager;
	extern struct Monitor* g_Monitor;
	extern class PhysicsManager* g_PhysicsManager;

	extern sec g_SecElapsedSinceProgramStart;
	extern sec g_DeltaTime;
}

namespace glm
{
	using vec2i = tvec2<flex::i32>;
	using vec2u = tvec2<flex::u32>;
}
