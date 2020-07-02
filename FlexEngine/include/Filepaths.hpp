#pragma once

namespace flex
{

#define ROOT_LOCATION			"../../../FlexEngine/"
#define SAVED_LOCATION			"../../../FlexEngine/saved/"
#define RESOURCE_LOCATION		"../../../FlexEngine/resources/"
#define RESOURCE(path)			"../../../FlexEngine/resources/" path
	// TODO: Remove
#define RESOURCE_STR(path)		"../../../FlexEngine/resources/" + path

#define FONT_LOCATION			RESOURCE_LOCATION "fonts/"
#define FONT_SDF_LOCATION		SAVED_LOCATION "fonts/"

#define SHADER_SOURCE_LOCATION		RESOURCE_LOCATION "shaders/"
#define SPV_LOCATION				SAVED_LOCATION "spv/"
#define SHADER_CHECKSUM_LOCATION	SAVED_LOCATION "vk-shader-checksum.dat"

} // namespace flex
