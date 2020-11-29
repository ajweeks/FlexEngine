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
		std::vector<glm::vec3> velocities;
		std::vector<glm::vec2> texCoords_UV;
		std::vector<i32> colours_R8G8B8A8;
		std::vector<glm::vec4> colours_R32G32B32A32;
		std::vector<glm::vec3> tangents;
		std::vector<glm::vec3> normals;

		std::vector<glm::vec4> extraVec4s;
		std::vector<i32> extraInts;
	};

	u32 HashVertexBufferDataCreateInfo(const VertexBufferDataCreateInfo& info);

	class VertexBufferData
	{
	public:
		void Initialize(const VertexBufferDataCreateInfo& createInfo);
		void InitializeDynamic(VertexAttributes attributes, u32 initialMaxVertCount);
		void UpdateData(const VertexBufferDataCreateInfo& createInfo);
		void Destroy();

		void ShrinkIfExcessGreaterThan(real minExcess = 0.0f);

		// Copies data from this buffer into dst for each given attribute
		// If this buffer doesn't contain a given attribute, default values will be used
		// Returns bytes copied
		u32 CopyInto(real* dst, VertexAttributes usingAttributes);

		void DescribeShaderVariables(Renderer* renderer, RenderID renderID);

		static void ResizeForPresentAttributes(VertexBufferDataCreateInfo& createInfo, u32 vertCount);

		bool bDynamic = false;
		real* vertexData = nullptr;
		u32 VertexBufferSize = 0;
		u32 UsedVertexBufferSize = 0;
		u32 VertexCount = 0;
		u32 UsedVertexCount = 0;
		u32 VertexStride = 0;
		VertexAttributes Attributes = 0;
	};
} // namespace flex
