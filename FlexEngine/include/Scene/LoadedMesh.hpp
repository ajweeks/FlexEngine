#pragma once

IGNORE_WARNINGS_PUSH
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf/tiny_gltf.h>
IGNORE_WARNINGS_POP

#include "Types.hpp"

namespace flex
{
	struct LoadedMesh
	{
		std::string relativeFilePath;
		MeshImportSettings importSettings;
		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
	};
} // namespace flex
