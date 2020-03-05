#!/bin/bash

glslangValidator --spirv-val -V vk_barebones_pos2_uv.vert -o spv/vk_barebones_pos2_uv_vert.spv
glslangValidator --spirv-val -V vk_barebones_pos3_uv.vert -o spv/vk_barebones_pos3_uv_vert.spv

glslangValidator --spirv-val -V vk_color.vert -o spv/vk_color_vert.spv 
glslangValidator --spirv-val -V vk_color.frag -o spv/vk_color_frag.spv

glslangValidator --spirv-val -V vk_skybox.vert -o spv/vk_skybox_vert.spv
glslangValidator --spirv-val -V vk_skybox.frag -o spv/vk_skybox_frag.spv

glslangValidator --spirv-val -V vk_deferred_combine.vert -o spv/vk_deferred_combine_vert.spv
glslangValidator --spirv-val -V vk_deferred_combine.frag -o spv/vk_deferred_combine_frag.spv

glslangValidator --spirv-val -V vk_deferred_combine_cubemap.vert -o spv/vk_deferred_combine_cubemap_vert.spv
glslangValidator --spirv-val -V vk_deferred_combine_cubemap.frag -o spv/vk_deferred_combine_cubemap_frag.spv

glslangValidator --spirv-val -V vk_pbr.vert -o spv/vk_pbr_vert.spv
glslangValidator --spirv-val -V vk_pbr.frag -o spv/vk_pbr_frag.spv

glslangValidator --spirv-val -V vk_pbr_ws.vert -o spv/vk_pbr_ws_vert.spv
glslangValidator --spirv-val -V vk_pbr_ws.frag -o spv/vk_pbr_ws_frag.spv

glslangValidator --spirv-val -V vk_brdf.vert -o spv/vk_brdf_vert.spv
glslangValidator --spirv-val -V vk_brdf.frag -o spv/vk_brdf_frag.spv

glslangValidator --spirv-val -V vk_irradiance.frag -o spv/vk_irradiance_frag.spv
glslangValidator --spirv-val -V vk_equirectangular_to_cube.frag -o spv/vk_equirectangular_to_cube_frag.spv
glslangValidator --spirv-val -V vk_prefilter.frag -o spv/vk_prefilter_frag.spv

glslangValidator --spirv-val -V vk_compute_sdf.vert -o spv/vk_compute_sdf_vert.spv
glslangValidator --spirv-val -V vk_compute_sdf.frag -o spv/vk_compute_sdf_frag.spv

glslangValidator --spirv-val -V vk_font_ss.vert -o spv/vk_font_ss_vert.spv
glslangValidator --spirv-val -V vk_font_ss.geom -o spv/vk_font_ss_geom.spv

glslangValidator --spirv-val -V vk_font_ws.vert -o spv/vk_font_ws_vert.spv
glslangValidator --spirv-val -V vk_font_ws.geom -o spv/vk_font_ws_geom.spv
glslangValidator --spirv-val -V vk_font.frag -o spv/vk_font_frag.spv

glslangValidator --spirv-val -V vk_ssao.frag -o spv/vk_ssao_frag.spv

glslangValidator --spirv-val -V vk_ssao_blur.frag -o spv/vk_ssao_blur_frag.spv

glslangValidator --spirv-val -V vk_post_process.vert -o spv/vk_post_process_vert.spv
glslangValidator --spirv-val -V vk_post_process.frag -o spv/vk_post_process_frag.spv

glslangValidator --spirv-val -V vk_post_fxaa.frag -o spv/vk_post_fxaa_frag.spv

glslangValidator --spirv-val -V vk_shadow.vert -o spv/vk_shadow_vert.spv

glslangValidator --spirv-val -V vk_sprite.vert -o spv/vk_sprite_vert.spv
glslangValidator --spirv-val -V vk_sprite.frag -o spv/vk_sprite_frag.spv
glslangValidator --spirv-val -V vk_sprite_arr.frag -o spv/vk_sprite_arr_frag.spv

glslangValidator --spirv-val -V vk_taa_resolve.frag -o spv/vk_taa_resolve_frag.spv

glslangValidator --spirv-val -V vk_gamma_correct.frag -o spv/vk_gamma_correct_frag.spv

glslangValidator --spirv-val -V vk_blit.frag -o spv/vk_blit_frag.spv

glslangValidator --spirv-val -V vk_simulate_particles.comp -o spv/vk_simulate_particles_comp.spv

glslangValidator --spirv-val -V vk_particles.vert -o spv/vk_particles_vert.spv
glslangValidator --spirv-val -V vk_particles.geom -o spv/vk_particles_geom.spv
glslangValidator --spirv-val -V vk_particles.frag -o spv/vk_particles_frag.spv

# ImGui
glslangValidator --spirv-val -V vk_imgui.frag -o spv/vk_imgui_frag.spv
glslangValidator --spirv-val -V vk_imgui.vert -o spv/vk_imgui_vert.spv
