@ glslangvalidator -V vk_simple.vert -o spv/vk_simple_vert.spv 
@ glslangvalidator -V vk_simple.frag -o spv/vk_simple_frag.spv
@ glslangvalidator -V vk_color.vert -o spv/vk_color_vert.spv 
@ glslangvalidator -V vk_color.frag -o spv/vk_color_frag.spv
@ glslangvalidator -V vk_skybox.vert -o spv/vk_skybox_vert.spv
@ glslangvalidator -V vk_skybox.frag -o spv/vk_skybox_frag.spv

@REM ImGui
@ glslangValidator -V vk_imgui.frag -o spv/vk_imgui_frag.spv
@ glslangValidator -V vk_imgui.vert -o spv/vk_imgui_vert.spv

@ pause
