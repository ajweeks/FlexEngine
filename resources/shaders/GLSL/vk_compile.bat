:: @ for /f "usebackq delims=|" %%f in (`dir /b "" ^| findstr /i "frag vert"`) do  C:\VulkanSDK\1.0.42.0\Bin32\glslangValidator.exe -V %%f -o %%~nf.spv
:: C:\VulkanSDK\1.0.42.0\Bin32\glslangValidator.exe -V simple.vert -o vk_simple_vert.spv

@call C:\VulkanSDK\1.0.42.0\Bin32\glslangValidator.exe -V vk_vertex_pos_col.frag -o spv/vk_vertex_pos_col_frag.spv
@call C:\VulkanSDK\1.0.42.0\Bin32\glslangValidator.exe -V vk_vertex_pos_col.vert -o spv/vk_vertex_pos_col_vert.spv

@pause
