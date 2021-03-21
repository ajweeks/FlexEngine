#pragma once

#include "Types.hpp"

struct cgltf_data;

namespace flex
{
	struct LoadedMesh
	{
		std::string relativeFilePath;
		cgltf_data* data = nullptr;
	};
} // namespace flex
