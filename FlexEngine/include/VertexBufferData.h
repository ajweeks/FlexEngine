#pragma once

#include <glm/integer.hpp>

namespace flex
{
	class VertexBufferData
	{
	public:
		VertexBufferData();

		typedef glm::uint Attribute;
		enum class AttributeBit : glm::uint
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

		struct CreateInfo
		{
			Attribute attributes;

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

		bool HasAttribute(AttributeBit attributeBits) const;
		glm::uint CalculateStride() const;
		void DescribeShaderVariables(Renderer* renderer, RenderID renderID);

		void* pDataStart;
		glm::uint BufferSize;
		glm::uint VertexCount;
		glm::uint VertexStride;
		Attribute Attributes;
	};
} // namespace flex
