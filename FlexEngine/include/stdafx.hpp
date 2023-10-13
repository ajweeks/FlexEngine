
// Configuration variables

#define COMPILE_VULKAN 1

#define COMPILE_IMGUI 1

#define ENABLE_CONSOLE_COLOURS 1

#define FLEX_OVERRIDE_NEW_DELETE 0
#define FLEX_OVERRIDE_MALLOC 0


#if defined(DEBUG) && defined(_WINDOWS)
// RenderDoc API only supported on windows
#define COMPILE_RENDERDOC_API 0
#else
// Disable render doc integration in non-debug builds
#define COMPILE_RENDERDOC_API 0
#endif

#ifdef DEBUG
#define THOROUGH_CHECKS			1
#define ENABLE_PROFILING		1
#define COMPILE_SHADER_COMPILER 1
#else
#define THOROUGH_CHECKS			0
#define ENABLE_PROFILING		0
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

#if defined(_MSC_VER) && _MSVC_LANG >= 201703L // C++17
#define FLEX_NO_DISCARD [[nodiscard]]
#else
#define FLEX_NO_DISCARD
#endif

#include "FlexPreprocessors.hpp"
#include "AssertHelpers.hpp"

#undef TRUE
#undef FALSE
#undef CreateWindow

// Markup variadic arguments. n = index of format string, m = index of first variadic ar
// Indices are 1-based due to implicit this param
#if defined(__clang__)
#define FORMAT_STRING_POST(n, m) __attribute__ (( format( __printf__, n, m )))
#define FORMAT_STRING_PRE
#elif defined(_MSC_VER)
#define FORMAT_STRING_POST(n, m)
#define FORMAT_STRING_PRE _Printf_format_string_
#elif defined(__GNUG__)
#define FORMAT_STRING_POST(n, m) __attribute__ (( format( __printf__, n, m )))
#define FORMAT_STRING_PRE
#endif

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

IGNORE_WARNINGS_PUSH
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED

// For matrix_decompose
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>

#include "palanteer/palanteer.h"
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

IGNORE_WARNINGS_PUSH
#include <cgltf/cgltf.h>
IGNORE_WARNINGS_POP

#if COMPILE_IMGUI
IGNORE_WARNINGS_PUSH
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

namespace flex
{
	extern const ImVec4 g_WarningTextColour;
	extern const ImVec4 g_WarningButtonColour;
	extern const ImVec4 g_WarningButtonHoveredColour;
	extern const ImVec4 g_WarningButtonActiveColour;
}

#include "Types.hpp"

#include "ImGuiBezier.hpp"
#include "imgui_plot.h"
IGNORE_WARNINGS_POP
#endif // COMPILE_IMGUI

#include <mikktspace.h>

#include "GUID.hpp"
#include "Logger.hpp"
#include "Systems/Systems.hpp"
#include "Filepaths.hpp"
#include "Physics/PhysicsTypeConversions.hpp"
#include "Tweakable.hpp"
#include "Systems/JobSystem.hpp"

#ifndef btAssert
#define btAssert(e) assert(e)
#endif

#define PI (glm::pi<real>())
#define TWO_PI (glm::two_pi<real>())
#define PI_DIV_TWO (glm::half_pi<real>())
#define PI_DIV_FOUR (glm::quarter_pi<real>())
#define THREE_PI_DIV_TWO (glm::three_over_two_pi<real>())
#define EPSILON (glm::epsilon<real>())

#define TOKEN_PASTE2(x, y) x ## y
#define TOKEN_PASTE(x, y) TOKEN_PASTE2(x, y)

#if ENABLE_PROFILING
#define PROFILE_BEGIN(blockName) plBegin(blockName)
#define PROFILE_END(blockName) plEnd(blockName)
#define PROFILE_AUTO(blockName) plScope(blockName)
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

#define CONCAT_INNER(a, b) a ## b
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define FLEX_MUTEX_LOCK(lock) std::lock_guard<std::mutex> CONCAT(_mutexLock_, __LINE__)(lock);

// Turns a const char* into a StringID and const char* pair of params
#define SID_PAIR(str) SID(str), str

#define SID(str) Hash(str)

namespace flex
{
	// Constants
	extern const glm::vec3 VEC3_RIGHT;
	extern const glm::vec3 VEC3_UP;
	extern const glm::vec3 VEC3_FORWARD;
	extern const glm::vec2 VEC2_ONE;
	extern const glm::vec2 VEC2_NEG_ONE;
	extern const glm::vec2 VEC2_ZERO;
	extern const glm::vec3 VEC3_ONE;
	extern const glm::vec3 VEC3_NEG_ONE;
	extern const glm::vec3 VEC3_ZERO;
	extern const glm::vec3 VEC3_GAMMA;
	extern const glm::vec3 VEC3_GAMMA_INVERSE;
	extern const glm::vec4 VEC4_ONE;
	extern const glm::vec4 VEC4_NEG_ONE;
	extern const glm::vec4 VEC4_ZERO;
	extern const glm::vec4 VEC4_GAMMA;
	extern const glm::vec4 VEC4_GAMMA_INVERSE;
	extern const glm::quat QUAT_IDENTITY;
	extern const glm::mat2 MAT2_IDENTITY;
	extern const glm::mat3 MAT3_IDENTITY;
	extern const glm::mat4 MAT4_IDENTITY;
	extern const glm::mat4 MAT4_ZERO;
	extern const u32 COLOUR32U_WHITE;
	extern const u32 COLOUR32U_BLACK;
	extern const glm::vec4 COLOUR128F_WHITE;
	extern const glm::vec4 COLOUR128F_BLACK;

	extern const std::string EMPTY_STRING;

	extern const u32 MAX_TEXTURE_DIM;

	const i32 X_AXIS_IDX = 0;
	const i32 Y_AXIS_IDX = 1;
	const i32 Z_AXIS_IDX = 2;
	const i32 ALL_AXES_IDX = 3;
	const i32 YZ_AXIS_IDX = 4;
	const i32 XZ_AXIS_IDX = 5;
	const i32 XY_AXIS_IDX = 6;

	// Globals
	extern class Window* g_Window;
	extern class CameraManager* g_CameraManager;
	extern class InputManager* g_InputManager;
	extern class Renderer* g_Renderer;
	extern class System* g_Systems[(i32)SystemType::_NONE];
	extern class FlexEngine* g_EngineInstance;
	extern class Editor* g_Editor;
	extern class SceneManager* g_SceneManager;
	extern struct Monitor* g_Monitor;
	extern class PhysicsManager* g_PhysicsManager;
	extern class ResourceManager* g_ResourceManager;
	extern class UIManager* g_UIManager;
	extern class ConfigFileManager* g_ConfigFileManager;
	extern const bool g_bDebugBuild;

	template<typename T>
	T* GetSystem(SystemType systemType)
	{
		return (T*)g_Systems[(i32)systemType];
	}

	PropertyCollectionManager* GetPropertyCollectionManager();

	extern sec g_SecElapsedSinceProgramStart;
	extern sec g_DeltaTime;
	extern const sec g_FixedDeltaTime;
	extern sec g_UnpausedDeltaTime; // Unpaused and unscaled

	extern std::size_t g_TotalTrackedAllocatedMemory;
	extern std::size_t g_TrackedAllocationCount;
	extern std::size_t g_TrackedDeallocationCount;

	extern const bool g_bEnableLogging_Loading;
	extern const bool g_bEnableLogging_Shaders;

	extern const bool g_bVulkanEnabled;
}

namespace glm
{
	using vec2i = tvec2<flex::i32>;
	using vec2u = tvec2<flex::u32>;
}
