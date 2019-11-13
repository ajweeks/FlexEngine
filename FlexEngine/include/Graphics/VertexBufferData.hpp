#pragma once

namespace flex
{
	class Renderer;

	struct VertexBufferDataCreateInfo
	{
		VertexAttributes attributes;

		std::vector<glm::vec3> positions_3D;
		std::vector<glm::vec2> positions_2D;
		std::vector<glm::vec4> positions_4D;
		std::vector<glm::vec4> velocities_4D;
		std::vector<glm::vec2> texCoords_UV;
		std::vector<i32> colors_R8G8B8A8;
		std::vector<glm::vec4> colors_R32G32B32A32;
		std::vector<glm::vec3> tangents;
		std::vector<glm::vec3> normals;

		std::vector<glm::vec4> extraVec4s;
		std::vector<i32> extraInts;
	};

	class VertexBufferData
	{
	public:
		void Initialize(VertexBufferDataCreateInfo* createInfo);
		void InitializeDynamic(VertexAttributes attributes, u32 maxNumVerts); // Allocates enough memory for maxNumVerts with given attributes
		void UpdateData(VertexBufferDataCreateInfo const* createInfo);
		void Destroy();

		// Copies data from this buffer into dst for each given attribute
		// If this buffer doesn't contain a given attribute, default values will be used
		u32 CopyInto(real* dst, VertexAttributes usingAttributes);

		void DescribeShaderVariables(Renderer* renderer, RenderID renderID);

		real* vertexData = nullptr;
		u32 VertexBufferSize = 0;
		u32 VertexCount = 0;
		u32 VertexStride = 0;
		VertexAttributes Attributes = 0;
	};
} // namespace flex
