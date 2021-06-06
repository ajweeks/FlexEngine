#pragma once

class type_info;

namespace flex
{
	enum class VertexAttribute : u32
	{
		POSITION					= (1 << 0),
		POSITION2					= (1 << 1),
		POSITION4					= (1 << 2),
		VELOCITY3					= (1 << 3),
		UV							= (1 << 4),
		COLOUR_R8G8B8A8_UNORM		= (1 << 5),
		COLOUR_R32G32B32A32_SFLOAT	= (1 << 6),
		TANGENT						= (1 << 7),
		NORMAL						= (1 << 8),
		EXTRA_VEC4					= (1 << 9),
		EXTRA_INT					= (1 << 10),

		_NONE						= 0,
	};

	struct VertexAttributeMetaData
	{
		std::string name;
		i32 size;
		const type_info& type;
	};

	static VertexAttributeMetaData s_VertexAttributes[] =
	{
		{ "in_Position", 3, typeid(glm::vec3) },
		{ "in_Position2D", 2, typeid(glm::vec2) },
		{ "in_Position4", 4, typeid(glm::vec4) },
		{ "in_Velocity", 3, typeid(glm::vec3) },
		{ "in_TexCoord", 2, typeid(glm::vec2) },
		{ "in_Colour_32", 1, typeid(u32) },
		{ "in_Colour", 4, typeid(glm::vec4) },
		{ "in_Tangent", 3, typeid(glm::vec3) },
		{ "in_Normal", 3, typeid(glm::vec3) },
		{ "in_ExtraVec4", 4, typeid(glm::vec4) },
		{ "in_ExtraInt", 1, typeid(i32) },
	};

	VertexAttribute VertexAttributeFromString(const char* attributeName);
	// Returns true when attributeName maps to a known attribute and it matches
	// the typeInfo passed in
	// Returns true if attribute name does not match any known value
	bool CompareVertexAttributeType(const char* attributeName, const type_info& typeInfo);

	u32 CalculateVertexStride(VertexAttributes vertexAttributes);

} // namespace flex
