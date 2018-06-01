#include "stdafx.hpp"

#include "VertexAttribute.hpp"
#include "Logger.hpp"

namespace flex
{
	u32 CalculateVertexStride(VertexAttributes vertexAttributes)
	{
		u32 stride = 0;

		if (vertexAttributes & (u32)VertexAttribute::POSITION)
		{
			stride += sizeof(glm::vec3);
		}
		if (vertexAttributes & (u32)VertexAttribute::POSITION_2D)
		{
			stride += sizeof(glm::vec2);
		}
		if (vertexAttributes & (u32)VertexAttribute::UV){
			stride += sizeof(glm::vec2);
		}
		if (vertexAttributes & (u32)VertexAttribute::UVW){
			stride += sizeof(glm::vec3);
		}
		if (vertexAttributes & (u32)VertexAttribute::COLOR_R8G8B8A8_UNORM){
			stride += sizeof(i32);
		}
		if (vertexAttributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
		{
			stride += sizeof(glm::vec4);
		}
		if (vertexAttributes & (u32)VertexAttribute::TANGENT)
		{
			stride += sizeof(glm::vec3);
		}
		if (vertexAttributes & (u32)VertexAttribute::BITANGENT)
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

		if (stride == 0)
		{
			Logger::LogWarning("Vertex attribute stride is 0!");
		}

		return stride;
	}
} // namespace flex
