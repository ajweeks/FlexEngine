#pragma once

namespace flex
{
	enum class VertexAttribute : u32
	{
		POSITION					= (1 << 0),
		POSITION2					= (1 << 1),
		POSITION4					= (1 << 2),
		VELOCITY4					= (1 << 3),
		UV							= (1 << 4),
		COLOR_R8G8B8A8_UNORM		= (1 << 5),
		COLOR_R32G32B32A32_SFLOAT	= (1 << 6),
		TANGENT						= (1 << 7),
		NORMAL						= (1 << 8),
		EXTRA_VEC4					= (1 << 9),
		EXTRA_INT					= (1 << 10),

		_NONE						= 0,
	};

	struct VertexType
	{
		std::string name;
		i32 size;
	};

	static VertexType s_VertexTypes[] =
	{
		{ "in_Position", 3 },
		{ "in_Position2D", 2 },
		{ "in_Position4", 4 },
		{ "in_TexCoord", 2 },
		{ "in_Color_32", 1 },
		{ "in_Color", 4 },
		{ "in_Tangent", 3 },
		{ "in_Normal", 3 },
		{ "in_ExtraVec4", 4 },
		{ "in_ExtraInt", 1 },
	};

	u32 CalculateVertexStride(VertexAttributes vertexAttributes);

} // namespace flex
