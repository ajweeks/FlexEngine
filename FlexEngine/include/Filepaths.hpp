#pragma once

namespace flex
{

#define ROOT_LOCATION			"../../../FlexEngine/"
#define SAVED_LOCATION			"../../../FlexEngine/saved/"
#define RESOURCE_LOCATION		"../../../FlexEngine/resources/"


#define CONFIG_LOCATION				ROOT_LOCATION "config/"
#define COMMON_CONFIG_LOCATION		CONFIG_LOCATION "common.json"
#define WINDOW_CONFIG_LOCATION		CONFIG_LOCATION "window-settings.json"
#define INPUT_BINDINGS_LOCATION		CONFIG_LOCATION "input-bindings.json"
#define RENDERER_SETTINGS_LOCATION	CONFIG_LOCATION "renderer-settings.json"
#define FONT_DEFINITION_LOCATION	CONFIG_LOCATION "fonts.json"
#define IMGUI_INI_LOCATION			CONFIG_LOCATION "imgui.ini"
#define IMGUI_LOG_LOCATION			CONFIG_LOCATION "imgui.log"

#define SCENE_DEFAULT_LOCATION		RESOURCE_LOCATION "scenes/default/"
#define SCENE_SAVED_LOCATION		RESOURCE_LOCATION "scenes/saved/"
#define PREFAB_LOCATION				RESOURCE_LOCATION "scenes/prefabs/"

#define MESH_DIRECTORY				RESOURCE_LOCATION "meshes/"
#define MESHES_FILE_LOCATION		RESOURCE_LOCATION "scenes/meshes.json"
#define MATERIALS_FILE_LOCATION		RESOURCE_LOCATION "scenes/materials.json"

#define TEXTURE_LOCATION			RESOURCE_LOCATION "textures/"

#define SFX_LOCATION				RESOURCE_LOCATION "audio/"

#define APP_ICON_LOCATION			RESOURCE_LOCATION "icons/"

#define FONT_LOCATION				RESOURCE_LOCATION "fonts/"
#define FONT_SDF_LOCATION			SAVED_LOCATION "fonts/"

#define SHADER_SOURCE_LOCATION		RESOURCE_LOCATION "shaders/"
#define SPV_LOCATION				SAVED_LOCATION "spv/"
#define SHADER_CHECKSUM_LOCATION	SAVED_LOCATION "vk-shader-checksum.dat"

#define SCREENSHOT_DIRECTORY		SAVED_LOCATION "screenshots/"

} // namespace flex
