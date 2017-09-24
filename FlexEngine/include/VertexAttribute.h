#pragma once

#include <glm/integer.hpp>

#include "Typedefs.h"

namespace flex 
{
	enum class VertexAttribute : glm::uint
	{
		NONE = 0,
		POSITION = (1 << 0),
		POSITION_2D = (1 << 1),
		UV = (1 << 2),
		UVW = (1 << 3),
		COLOR_R8G8B8A8_UNORM = (1 << 4),
		COLOR_R32G32B32A32_SFLOAT = (1 << 5),
		TANGENT = (1 << 6),
		BITANGENT = (1 << 7),
		NORMAL = (1 << 8),
	};

	glm::uint CalculateVertexStride(VertexAttributes vertexAttributes);

} // namespace flex