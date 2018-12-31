#pragma once

namespace flex
{
	class Renderer;

	class VertexBufferData
	{
	public:
		struct CreateInfo
		{
			VertexAttributes attributes;

			std::vector<glm::vec3> positions_3D;
			std::vector<glm::vec2> positions_2D;
			std::vector<glm::vec2> texCoords_UV;
			std::vector<glm::vec3> texCoords_UVW;
			std::vector<i32> colors_R8G8B8A8;
			std::vector<glm::vec4> colors_R32G32B32A32;
			std::vector<glm::vec3> tangents;
			std::vector<glm::vec3> bitangents;
			std::vector<glm::vec3> normals;

			std::vector<glm::vec4> extraVec4s;
			std::vector<i32> extraInts;
		};

		void Initialize(CreateInfo* createInfo);
		void Destroy();

		void DescribeShaderVariables(Renderer* renderer, RenderID renderID);

		void* pDataStart = nullptr;
		u32 BufferSize = 0;
		u32 VertexCount = 0;
		u32 VertexStride = 0;
		VertexAttributes Attributes = 0;
	};
} // namespace flex
