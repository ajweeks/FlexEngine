#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#define VOLK_IMPLEMENTATION
#include "volk/volk.h"
IGNORE_WARNINGS_POP

namespace flex
{
#if COMPILE_IMGUI
	ImVec4 g_WarningTextColour(1.0f, 0.25f, 0.25f, 1.0f);
	ImVec4 g_WarningButtonColour(0.65f, 0.12f, 0.09f, 1.0f);
	ImVec4 g_WarningButtonHoveredColour(0.45f, 0.04f, 0.01f, 1.0f);
	ImVec4 g_WarningButtonActiveColour(0.35f, 0.0f, 0.0f, 1.0f);
#endif // COMPILE_IMGUI

	glm::vec3 VEC3_RIGHT = glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 VEC3_UP = glm::vec3(0.0f, 1.0f, 0.0f);
	glm::vec3 VEC3_FORWARD = glm::vec3(0.0f, 0.0f, 1.0f);
	glm::vec2 VEC2_ONE = glm::vec2(1.0f);
	glm::vec2 VEC2_NEG_ONE = glm::vec2(-1.0f);
	glm::vec2 VEC2_ZERO = glm::vec2(0.0f);
	glm::vec3 VEC3_ONE = glm::vec3(1.0f);
	glm::vec3 VEC3_NEG_ONE = glm::vec3(-1.0f);
	glm::vec3 VEC3_ZERO = glm::vec3(0.0f);
	glm::vec3 VEC3_GAMMA = glm::vec3(2.2f);
	glm::vec3 VEC3_GAMMA_INVERSE = glm::vec3(1.0f / 2.2f);
	glm::vec4 VEC4_ONE = glm::vec4(1.0f);
	glm::vec4 VEC4_NEG_ONE = glm::vec4(-1.0f);
	glm::vec4 VEC4_ZERO = glm::vec4(0.0f);
	glm::vec4 VEC4_GAMMA = glm::vec4(2.2f);
	glm::vec4 VEC4_GAMMA_INVERSE = glm::vec4(1.0f / 2.2f);
	glm::quat QUAT_IDENTITY = glm::quat(VEC3_ZERO);
	glm::mat2 MAT2_IDENTITY = glm::mat2(1.0f);
	glm::mat3 MAT3_IDENTITY = glm::mat3(1.0f);
	glm::mat4 MAT4_IDENTITY = glm::mat4(1.0f);
	glm::mat4 MAT4_ZERO = glm::mat4(0.0f);
	flex::u32 COLOUR32U_WHITE = 0xFFFFFFFF;
	flex::u32 COLOUR32U_BLACK = 0x00000000;
	glm::vec4 COLOUR128F_WHITE = VEC4_ONE;
	glm::vec4 COLOUR128F_BLACK = VEC4_ZERO;

	std::string EMPTY_STRING = std::string();

	flex::u32 MAX_TEXTURE_DIM = 65536;

	class Window* g_Window = nullptr;
	class CameraManager* g_CameraManager = nullptr;
	class InputManager* g_InputManager = nullptr;
	class Renderer* g_Renderer = nullptr;
	System* g_Systems[(i32)SystemType::_NONE];
	class FlexEngine* g_EngineInstance = nullptr;
	class Editor* g_Editor = nullptr;
	class SceneManager* g_SceneManager = nullptr;
	struct Monitor* g_Monitor = nullptr;
	class PhysicsManager* g_PhysicsManager = nullptr;
	class ResourceManager* g_ResourceManager = nullptr;
	class UIManager* g_UIManager = nullptr;
	class ConfigFileManager* g_ConfigFileManager = nullptr;
	class PropertyCollectionManager* g_PropertyCollectionManager = nullptr;

#if DEBUG
	bool g_bDebugBuild = true;
#else
	bool g_bDebugBuild = false;
#endif

	sec g_SecElapsedSinceProgramStart = 0;
	sec g_DeltaTime = 0;
	const sec g_FixedDeltaTime = 1.0f / 60.0f;
	sec g_UnpausedDeltaTime = 0;

	size_t g_TotalTrackedAllocatedMemory = 0;
	size_t g_TrackedAllocationCount = 0;
	size_t g_TrackedDeallocationCount = 0;

	bool g_bEnableLogging_Loading = false;
	bool g_bEnableLogging_Shaders = true;

#if COMPILE_VULKAN
	bool g_bVulkanEnabled = true;
#else
	bool g_bVulkanEnabled = false;
#endif
} // namespace flex
