// Configuration variables:

#define COMPILE_OPEN_GL 0
#define COMPILE_VULKAN 1

#define COMPILE_IMGUI 1

#define RUN_UNIT_TESTS 0

#define ENABLE_CONSOLE_COLOURS 1

#define FLEX_OVERRIDE_NEW_DELETE 0
#define FLEX_OVERRIDE_MALLOC 0

#ifdef DEBUG
#define THOROUGH_CHECKS 1
#define ENABLE_PROFILING 1
#define COMPILE_RENDERDOC_API 0
#define COMPILE_SHADER_COMPILER 1
#else
#define THOROUGH_CHECKS 0
#define ENABLE_PROFILING 0
#define COMPILE_RENDERDOC_API 0
#define COMPILE_SHADER_COMPILER 0
#endif

// End configuration variables

#define VK_NO_PROTOTYPES

#define VC_EXTRALEAN

#define USE_SSE2

#if COMPILE_VULKAN
#define VULKAN_HPP_TYPESAFE_CONVERSION
#endif

#define _CRT_SECURE_NO_WARNINGS

#include <cstddef>

#include "memory.hpp"

#define BT_NO_SIMD_OPERATOR_OVERLOADS
#define NOMINMAX

#if defined(_WINDOWS)
#define WRITE_BARRIER _WriteBarrier(); _mm_sfence()
#elif defined(__linux__)
#define WRITE_BARRIER __sync_synchronize()
#define ACQUIRE_BARRIER smp_cond_load_acquire()
#define RELEASE_BARRIER smp_cond_store_release()
#else
#error
#endif

#ifdef _WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#define FLEX_UNUSED(param) ((void)param)

#define GLFW_INCLUDE_NONE

#if COMPILE_IMGUI
#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS 1
#endif

#define FLEX_VERSION(major, minor, patch) (((major) << 22) | ((minor) << 12) | (patch))

#define FLEX_NO_DISCARD [[nodiscard]]

#if defined(__clang__)
#define IGNORE_WARNINGS_PUSH \
		_Pragma("clang diagnostic push") \
		_Pragma("clang diagnostic ignored \"-Weverything\"")
#define IGNORE_WARNINGS_POP _Pragma("clang diagnostic pop")
#elif defined(_MSC_VER)
#define IGNORE_WARNINGS_PUSH __pragma(warning(push, 0))
#define IGNORE_WARNINGS_POP __pragma(warning(pop))
#elif defined(__GNUG__)
#define IGNORE_WARNINGS_PUSH \
		_Pragma("GCC diagnostic push") \
		_Pragma("GCC diagnostic ignored \"-Wall\"")
#define IGNORE_WARNINGS_POP _Pragma("GCC diagnostic pop")
#else
	// Unhandled compiler
	#error
#endif

#undef FORMAT_STRING

#undef TRUE
#undef FALSE

#if defined(__clang__)
#define FORMAT_STRING(n,m) __attribute__ (( format( __printf__, fmtargnumber, firstvarargnumber )))
#elif defined(_MSC_VER)
#define FORMAT_STRING(n,m) _Printf_format_string_
#elif defined(__GNUG__)
#define FORMAT_STRING(n,m) __attribute__ (( format( __printf__, n, m )))
#endif

#define FT_EXPORT(Type) Type

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

#ifdef _WINDOWS
#include <crtdbg.h>
#include "MinWindows.hpp"
#endif

#include <stdlib.h>
#include <string.h>

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
#define VOLK_VULKAN_H_PATH <vulkan/vulkan.hpp>
#define VULKAN_HPP_NAMESPACE vkhpp
#include "volk/volk.h"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
IGNORE_WARNINGS_POP
#endif // COMPILE_VULKAN

#if COMPILE_IMGUI
IGNORE_WARNINGS_PUSH
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

namespace flex
{
	extern ImVec4 g_WarningTextColour;
	extern ImVec4 g_WarningButtonColour;
	extern ImVec4 g_WarningButtonHoveredColour;
	extern ImVec4 g_WarningButtonActiveColour;
}

#include "ImGuiBezier.hpp"
IGNORE_WARNINGS_POP
#endif // COMPILE_IMGUI

#include "Filepaths.hpp"
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

#define X_AXIS_IDX   0
#define Y_AXIS_IDX   1
#define Z_AXIS_IDX   2
#define ALL_AXES_IDX 3

#define TOKEN_PASTE2(x, y) x ## y
#define TOKEN_PASTE(x, y) TOKEN_PASTE2(x, y)

#if ENABLE_PROFILING
#define PROFILE_BEGIN(blockName) Profiler::Begin(blockName);
#define PROFILE_END(blockName) Profiler::End(blockName);
#define PROFILE_AUTO(blockName) AutoProfilerBlock TOKEN_PASTE(autoProfileBlock_, __LINE__)(blockName);
#else
#define PROFILE_BEGIN(blockName)
#define PROFILE_END(blockName)
#define PROFILE_AUTO(blockName)
#endif

#if defined(_WINDOWS)
#define DEBUG_BREAK() __debugbreak()
#elif defined(__linux__)
// Linux: (untested)
#define DEBUG_BREAK() __builtin_trap()
#else
#error
#endif

#define ENSURE_NO_ENTRY() { PrintError("Execution entered no entry path! %s\n", __FUNCTION__); DEBUG_BREAK(); }
#ifdef DEBUG
#define ENSURE(condition) if (!(condition)) { PrintError("Ensure failed! File: %s, Line: %d\n", __FILE__, __LINE__); DEBUG_BREAK(); }
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
	// TODO: Calculate string hash here
#define SID(str) (str)

	extern glm::vec3 VEC3_RIGHT;
	extern glm::vec3 VEC3_UP;
	extern glm::vec3 VEC3_FORWARD;
	extern glm::vec2 VEC2_ONE;
	extern glm::vec2 VEC2_NEG_ONE;
	extern glm::vec2 VEC2_ZERO;
	extern glm::vec3 VEC3_ONE;
	extern glm::vec3 VEC3_NEG_ONE;
	extern glm::vec3 VEC3_ZERO;
	extern glm::vec4 VEC4_ONE;
	extern glm::vec4 VEC4_NEG_ONE;
	extern glm::vec4 VEC4_ZERO;
	extern glm::quat QUAT_IDENTITY;
	extern glm::mat4 MAT4_IDENTITY;
	extern glm::mat4 MAT4_ZERO;
	extern u32 COLOUR32U_WHITE;
	extern u32 COLOUR32U_BLACK;
	extern glm::vec4 COLOUR128F_WHITE;
	extern glm::vec4 COLOUR128F_BLACK;

	extern std::string EMPTY_STRING;

	extern u32 MAX_TEXTURE_DIM;

	// These fields are defined and initialized in FlexEngine.cpp
	extern class Window* g_Window;
	extern class CameraManager* g_CameraManager;

	extern class InputManager* g_InputManager;
	extern class Renderer* g_Renderer;
	extern class PluggablesSystem* g_PluggablesSystem;
	extern class FlexEngine* g_EngineInstance;
	extern class Editor* g_Editor;
	extern class SceneManager* g_SceneManager;
	extern struct Monitor* g_Monitor;
	extern class PhysicsManager* g_PhysicsManager;

	extern sec g_SecElapsedSinceProgramStart;
	extern sec g_DeltaTime;
	extern sec g_UnpausedDeltaTime; // Unpaused and unscaled

	extern std::size_t g_TotalTrackedAllocatedMemory;
	extern std::size_t g_TrackedAllocationCount;
	extern std::size_t g_TrackedDeallocationCount;

	extern bool g_bEnableLogging_Loading;
	extern bool g_bEnableLogging_Shaders;

	extern bool g_bOpenGLEnabled;
	extern bool g_bVulkanEnabled;
}

namespace glm
{
	using vec2i = tvec2<flex::i32>;
	using vec2u = tvec2<flex::u32>;
}
