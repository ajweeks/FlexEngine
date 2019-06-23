
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

@ glslangvalidator --spirv-val -V vk_pbr.vert -o spv/vk_pbr_vert.spv
@ glslangvalidator --spirv-val -V vk_pbr.frag -o spv/vk_pbr_frag.spv

@ glslangvalidator --spirv-val -V vk_pbr_ws.vert -o spv/vk_pbr_ws_vert.spv
@ glslangvalidator --spirv-val -V vk_pbr_ws.frag -o spv/vk_pbr_ws_frag.spv

@ glslangvalidator --spirv-val -V vk_brdf.vert -o spv/vk_brdf_vert.spv
@ glslangvalidator --spirv-val -V vk_brdf.frag -o spv/vk_brdf_frag.spv

@ glslangvalidator --spirv-val -V vk_irradiance.frag -o spv/vk_irradiance_frag.spv
@ glslangvalidator --spirv-val -V vk_equirectangular_to_cube.frag -o spv/vk_equirectangular_to_cube_frag.spv
@ glslangvalidator --spirv-val -V vk_prefilter.frag -o spv/vk_prefilter_frag.spv

@ glslangvalidator --spirv-val -V vk_compute_sdf.vert -o spv/vk_compute_sdf_vert.spv
@ glslangvalidator --spirv-val -V vk_compute_sdf.frag -o spv/vk_compute_sdf_frag.spv

@ glslangvalidator --spirv-val -V vk_font_ss.vert -o spv/vk_font_ss_vert.spv
@ glslangvalidator --spirv-val -V vk_font_ss.geom -o spv/vk_font_ss_geom.spv

@ glslangvalidator --spirv-val -V vk_font_ws.vert -o spv/vk_font_ws_vert.spv
@ glslangvalidator --spirv-val -V vk_font_ws.geom -o spv/vk_font_ws_geom.spv
@ glslangvalidator --spirv-val -V vk_font.frag -o spv/vk_font_frag.spv

@ glslangvalidator --spirv-val -V vk_ssao.vert -o spv/vk_ssao_vert.spv
@ glslangvalidator --spirv-val -V vk_ssao.frag -o spv/vk_ssao_frag.spv

@ glslangvalidator --spirv-val -V vk_ssao_blur.vert -o spv/vk_ssao_blur_vert.spv
@ glslangvalidator --spirv-val -V vk_ssao_blur.frag -o spv/vk_ssao_blur_frag.spv

@ glslangvalidator --spirv-val -V vk_post_process.vert -o spv/vk_post_process_vert.spv
@ glslangvalidator --spirv-val -V vk_post_process.frag -o spv/vk_post_process_frag.spv

@ glslangvalidator --spirv-val -V vk_post_fxaa.vert -o spv/vk_post_fxaa_vert.spv
@ glslangvalidator --spirv-val -V vk_post_fxaa.frag -o spv/vk_post_fxaa_frag.spv

@ glslangvalidator --spirv-val -V vk_shadow.vert -o spv/vk_shadow_vert.spv

@ glslangvalidator --spirv-val -V vk_sprite.vert -o spv/vk_sprite_vert.spv
@ glslangvalidator --spirv-val -V vk_sprite.frag -o spv/vk_sprite_frag.spv
@ glslangvalidator --spirv-val -V vk_sprite_arr.frag -o spv/vk_sprite_arr_frag.spv

@REM ImGui
@ glslangValidator --spirv-val -V vk_imgui.frag -o spv/vk_imgui_frag.spv
@ glslangValidator --spirv-val -V vk_imgui.vert -o spv/vk_imgui_vert.spv

@REM @ pause

@ EXIT /B %ERRORLEVEL%
