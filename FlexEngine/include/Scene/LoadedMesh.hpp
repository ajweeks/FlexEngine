#pragma once

IGNORE_WARNINGS_PUSH
#include <cgltf/cgltf.h>
IGNORE_WARNINGS_POP

#include "Types.hpp"

namespace flex
{
	struct LoadedMesh
	{
		std::string relativeFilePath;
		cgltf_data* data = nullptr;
	};
} // namespace flex
