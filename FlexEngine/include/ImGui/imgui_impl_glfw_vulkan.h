// ImGui GLFW binding with Vulkan + shaders

// Missing features:
//  [ ] User texture binding. Changes of ImTextureID aren't supported by this binding! See https://github.com/ocornut/imgui/pull/914

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you use this binding you'll need to call 5 functions: ImGui_ImplXXXX_Init(), ImGui_ImplXXX_CreateFontsTexture(), ImGui_ImplXXXX_NewFrame(), ImGui_ImplXXXX_Render() and ImGui_ImplXXXX_Shutdown().
// If you are new to ImGui, see examples/README.txt and documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

struct GLFWwindow;

#define IMGUI_VK_QUEUED_FRAMES 3

struct ImGui_ImplGlfwVulkan_Init_Data
{
    VkAllocationCallbacks* allocator;
    VkPhysicalDevice       gpu;
    VkDevice               device;
    VkRenderPass           render_pass;
    VkPipelineCache        pipeline_cache;
    VkDescriptorPool       descriptor_pool;
    void (*check_vk_result)(VkResult err);
};

IMGUI_API bool        ImGui_ImplGlfwVulkan_Init(GLFWwindow* window, ImGui_ImplGlfwVulkan_Init_Data *init_data);
IMGUI_API void        ImGui_ImplGlfwVulkan_Shutdown();
IMGUI_API void        ImGui_ImplGlfwVulkan_NewFrame();
IMGUI_API void        ImGui_ImplGlfwVulkan_DrawFrame(VkCommandBuffer command_buffer);
IMGUI_API void        ImGui_ImplGlfwVulkan_Render();

// Use if you want to reset your rendering device without losing ImGui state.
IMGUI_API void        ImGui_ImplGlfwVulkan_InvalidateFontUploadObjects();
IMGUI_API void        ImGui_ImplGlfwVulkan_InvalidateDeviceObjects();
IMGUI_API bool        ImGui_ImplGlfwVulkan_CreateFontsTexture(VkCommandBuffer command_buffer);
IMGUI_API bool        ImGui_ImplGlfwVulkan_CreateDeviceObjects();

