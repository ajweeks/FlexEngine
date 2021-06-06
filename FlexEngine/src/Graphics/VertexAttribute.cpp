#include "stdafx.hpp"

#include <typeinfo>

#include "Graphics/VertexAttribute.hpp"

namespace flex
{
	VertexAttribute VertexAttributeFromString(const char* attributeName)
	{
		for (u32 i = 0; i < ARRAY_LENGTH(s_VertexAttributes); ++i)
		{
			const char* attribute = s_VertexAttributes[i].name.c_str();
			if (strlen(attribute) == strlen(attributeName) && strcmp(attribute, attributeName) == 0)
			{
				return (VertexAttribute)(1 << i);
			}
		}

		return VertexAttribute::_NONE;
	}

	bool CompareVertexAttributeType(const char* attributeName, const type_info& typeInfo)
	{
		for (const VertexAttributeMetaData& attributeMetaData : s_VertexAttributes)
		{
			const char* attribute = attributeMetaData.name.c_str();
			if (strlen(attribute) == strlen(attributeName) && strcmp(attribute, attributeName) == 0)
			{
				return attributeMetaData.type == typeInfo;
			}
		}

		return true;
	}

	u32 CalculateVertexStride(VertexAttributes vertexAttributes)
	{
		u32 stride = 0;

		if (vertexAttributes & (u32)VertexAttribute::POSITION)
		{
			stride += sizeof(glm::vec3);
		}
		if (vertexAttributes & (u32)VertexAttribute::POSITION2)
		{
			stride += sizeof(glm::vec2);
		}
		if (vertexAttributes & (u32)VertexAttribute::POSITION4)
		{
			stride += sizeof(glm::vec4);
		}
		if (vertexAttributes & (u32)VertexAttribute::VELOCITY3)
		{
			stride += sizeof(glm::vec3);
		}
		if (vertexAttributes & (u32)VertexAttribute::UV)
		{
			stride += sizeof(glm::vec2);
		}
		if (vertexAttributes & (u32)VertexAttribute::COLOUR_R8G8B8A8_UNORM)
		{
			stride += sizeof(i32);
		}
		if (vertexAttributes & (u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT)
		{
			stride += sizeof(glm::vec4);
		}
		if (vertexAttributes & (u32)VertexAttribute::TANGENT)
		{
			stride += sizeof(glm::vec3);
		}
		if (vertexAttributes & (u32)VertexAttribute::NORMAL)
		{
			stride += sizeof(glm::vec3);
		}
		if (vertexAttributes & (u32)VertexAttribute::EXTRA_VEC4)
		{
			stride += sizeof(glm::vec4);
		}
		if (vertexAttributes & (u32)VertexAttribute::EXTRA_INT)
		{
			stride += sizeof(i32);
		}

		return stride;
	}
} // namespace flex
