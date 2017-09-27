#pragma once

#include <vector>

#include <glm/integer.hpp>

#include "Typedefs.h"

namespace flex
{
	class Renderer;

	class VertexBufferData
	{
	public:
		VertexBufferData();

		struct CreateInfo
		{
			VertexAttributes attributes;

			std::vector<glm::vec3> positions_3D;
			std::vector<glm::vec2> positions_2D;
			std::vector<glm::vec2> texCoords_UV;
			std::vector<glm::vec3> texCoords_UVW;
			std::vector<glm::int32> colors_R8G8B8A8;
			std::vector<glm::vec4> colors_R32G32B32A32;
			std::vector<glm::vec3> tangents;
			std::vector<glm::vec3> bitangents;
			std::vector<glm::vec3> normals;
		};

		void Initialize(CreateInfo* createInfo);
		void Destroy();

		void DescribeShaderVariables(Renderer* renderer, RenderID renderID);

		void* pDataStart;
		glm::uint BufferSize;
		glm::uint VertexCount;
		glm::uint VertexStride;
		VertexAttributes Attributes;
	};
} // namespace flex
