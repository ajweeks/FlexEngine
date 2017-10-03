#include "stdafx.hpp"

#include "VertexAttribute.hpp"
#include "Logger.hpp"

namespace flex
{
	glm::uint CalculateVertexStride(VertexAttributes vertexAttributes)
	{
		glm::uint stride = 0;

		if (vertexAttributes & (glm::uint)VertexAttribute::POSITION) stride += sizeof(glm::vec3);
		if (vertexAttributes & (glm::uint)VertexAttribute::POSITION_2D) stride += sizeof(glm::vec2);
		if (vertexAttributes & (glm::uint)VertexAttribute::UV) stride += sizeof(glm::vec2);
		if (vertexAttributes & (glm::uint)VertexAttribute::UVW) stride += sizeof(glm::vec3);
		if (vertexAttributes & (glm::uint)VertexAttribute::COLOR_R8G8B8A8_UNORM) stride += sizeof(glm::int32);
		if (vertexAttributes & (glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT) stride += sizeof(glm::vec4);
		if (vertexAttributes & (glm::uint)VertexAttribute::TANGENT) stride += sizeof(glm::vec3);
		if (vertexAttributes & (glm::uint)VertexAttribute::BITANGENT) stride += sizeof(glm::vec3);
		if (vertexAttributes & (glm::uint)VertexAttribute::NORMAL) stride += sizeof(glm::vec3);

		if (stride == 0)
		{
			Logger::LogWarning("Vertex attribute stride is 0!");
		}

		return stride;
	}
} // namespace flex
