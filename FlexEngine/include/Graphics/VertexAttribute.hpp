#pragma once

namespace flex
{
	enum class VertexAttribute : u32
	{
		NONE						= 0,
		POSITION					= (1 << 0),
		POSITION_2D					= (1 << 1),
		UV							= (1 << 2),
		COLOR_R8G8B8A8_UNORM		= (1 << 3),
		COLOR_R32G32B32A32_SFLOAT	= (1 << 4),
		TANGENT						= (1 << 5),
		BITANGENT					= (1 << 6),
		NORMAL						= (1 << 7),
		EXTRA_VEC4					= (1 << 8),
		EXTRA_INT					= (1 << 9),
		// NOTE: s_VertexTypes must be updated to stay aligned with this enum
	};

	struct VertexType
	{
		std::string name;
		i32 size;
	};

	static VertexType s_VertexTypes[] = {
		{ "in_Position", 3 },
		{ "in_Position2D", 2 },
		{ "in_TexCoord", 2 },
		{ "in_Color_32", 1 },
		{ "in_Color", 4 },
		{ "in_Tangent", 3 },
		{ "in_Bitangent", 3 },
		{ "in_Normal", 3 },
		{ "in_ExtraVec4", 4 },
		{ "in_ExtraInt", 1 },
	};

	u32 CalculateVertexStride(VertexAttributes vertexAttributes);

} // namespace flex
