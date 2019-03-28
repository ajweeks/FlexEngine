
@ glslangvalidator --spirv-val -V vk_simple.vert -o spv/vk_simple_vert.spv 
@ glslangvalidator --spirv-val -V vk_simple.frag -o spv/vk_simple_frag.spv

@ glslangvalidator --spirv-val -V vk_color.vert -o spv/vk_color_vert.spv 
@ glslangvalidator --spirv-val -V vk_color.frag -o spv/vk_color_frag.spv

@ glslangvalidator --spirv-val -V vk_skybox.vert -o spv/vk_skybox_vert.spv
@ glslangvalidator --spirv-val -V vk_skybox.frag -o spv/vk_skybox_frag.spv

@ glslangvalidator --spirv-val -V vk_deferred_combine.vert -o spv/vk_deferred_combine_vert.spv
@ glslangvalidator --spirv-val -V vk_deferred_combine.frag -o spv/vk_deferred_combine_frag.spv

@ glslangvalidator --spirv-val -V vk_deferred_combine_cubemap.vert -o spv/vk_deferred_combine_cubemap_vert.spv
@ glslangvalidator --spirv-val -V vk_deferred_combine_cubemap.frag -o spv/vk_deferred_combine_cubemap_frag.spv

@ glslangvalidator --spirv-val -V vk_deferred_simple.vert -o spv/vk_deferred_simple_vert.spv
@ glslangvalidator --spirv-val -V vk_deferred_simple.frag -o spv/vk_deferred_simple_frag.spv

@ glslangvalidator --spirv-val -V vk_pbr.vert -o spv/vk_pbr_vert.spv
@ glslangvalidator --spirv-val -V vk_pbr.frag -o spv/vk_pbr_frag.spv

@ glslangvalidator --spirv-val -V vk_pbr_ws.vert -o spv/vk_pbr_ws_vert.spv
@ glslangvalidator --spirv-val -V vk_pbr_ws.frag -o spv/vk_pbr_ws_frag.spv

@ glslangvalidator --spirv-val -V vk_brdf.vert -o spv/vk_brdf_vert.spv
@ glslangvalidator --spirv-val -V vk_brdf.frag -o spv/vk_brdf_frag.spv

@ glslangvalidator --spirv-val -V vk_irradiance.frag -o spv/vk_irradiance_frag.spv
@ glslangvalidator --spirv-val -V vk_equirectangular_to_cube.frag -o spv/vk_equirectangular_to_cube_frag.spv
@ glslangvalidator --spirv-val -V vk_prefilter.frag -o spv/vk_prefilter_frag.spv

@REM ImGui
@ glslangValidator --spirv-val -V vk_imgui.frag -o spv/vk_imgui_frag.spv
@ glslangValidator --spirv-val -V vk_imgui.vert -o spv/vk_imgui_vert.spv

@REM @ pause

