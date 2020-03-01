#include "stdafx.hpp"

#define VOLK_IMPLEMENTATION
#include "volk/volk.h"

namespace flex
{
#if COMPILE_OPEN_GL
    bool g_bOpenGLEnabled = true;
#else
    bool g_bOpenGLEnabled = false;
#endif

#if COMPILE_VULKAN
    bool g_bVulkanEnabled = true;
#else
    bool g_bVulkanEnabled = false;
#endif

    bool g_bEnableLogging_Loading = false;

    glm::vec3 VEC3_RIGHT = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 VEC3_UP = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 VEC3_FORWARD = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec2 VEC2_ONE = glm::vec2(1.0f);
    glm::vec2 VEC2_NEG_ONE = glm::vec2(-1.0f);
    glm::vec2 VEC2_ZERO = glm::vec2(0.0f);
    glm::vec3 VEC3_ONE = glm::vec3(1.0f);
    glm::vec3 VEC3_NEG_ONE = glm::vec3(-1.0f);
    glm::vec3 VEC3_ZERO = glm::vec3(0.0f);
    glm::vec4 VEC4_ONE = glm::vec4(1.0f);
    glm::vec4 VEC4_NEG_ONE = glm::vec4(-1.0f);
    glm::vec4 VEC4_ZERO = glm::vec4(0.0f);
    glm::quat QUAT_UNIT = glm::quat(VEC3_ZERO);
    glm::mat4 MAT4_IDENTITY = glm::mat4(1.0f);
    glm::mat4 MAT4_ZERO = glm::mat4(0.0f);
    flex::u32 COLOR32U_WHITE = 0xFFFFFFFF;
    flex::u32 COLOR32U_BLACK = 0x00000000;
    glm::vec4 COLOR128F_WHITE = VEC4_ONE;
    glm::vec4 COLOR128F_BLACK = VEC4_ZERO;
    std::string EMPTY_STRING = std::string();
    flex::u32 MAX_TEXTURE_DIM = 65536;
} // namespace flex
