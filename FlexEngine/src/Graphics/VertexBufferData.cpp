
#include "stdafx.hpp"

#include "Graphics/VertexBufferData.hpp"

#include "Graphics/Renderer.hpp"
#include "Graphics/VertexAttribute.hpp"

namespace flex
{
	void VertexBufferData::Initialize(CreateInfo* createInfo)
	{
		VertexCount = createInfo->positions_3D.size();
		if (VertexCount == 0)
		{
			VertexCount = createInfo->positions_2D.size();
		}
		Attributes = createInfo->attributes;
		VertexStride = CalculateVertexStride(Attributes);
		VertexBufferSize = VertexCount * VertexStride;

		assert(vertexData == nullptr);
		vertexData = (real*)malloc(VertexBufferSize);
		if (vertexData == nullptr)
		{
			PrintError("Failed to allocate memory required for vertex buffer data (%u bytes)\n", VertexBufferSize);
			return;
		}

		UpdateData(createInfo);
	}

	void VertexBufferData::UpdateData(CreateInfo* createInfo)
	{
		assert(vertexData != nullptr);
		assert(VertexCount > 0);

		real* vertexDataP = vertexData;
		for (u32 i = 0; i < VertexCount; ++i)
		{
			if (Attributes & (u32)VertexAttribute::POSITION)
			{
				memcpy(vertexDataP, createInfo->positions_3D.data() + i, sizeof(glm::vec3));
				vertexDataP += 3;
			}

			if (Attributes & (u32)VertexAttribute::POSITION_2D)
			{
				memcpy(vertexDataP, createInfo->positions_2D.data() + i, sizeof(glm::vec2));
				vertexDataP += 2;
			}

			if (Attributes & (u32)VertexAttribute::UV)
			{
				memcpy(vertexDataP, createInfo->texCoords_UV.data() + i, sizeof(glm::vec2));
				vertexDataP += 2;
			}

			if (Attributes & (u32)VertexAttribute::COLOR_R8G8B8A8_UNORM)
			{
				memcpy(vertexDataP, createInfo->colors_R8G8B8A8.data() + i, sizeof(i32));
				vertexDataP += 1;
			}

			if (Attributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
			{
				memcpy(vertexDataP, createInfo->colors_R32G32B32A32.data() + i, sizeof(glm::vec4));
				vertexDataP += 4;
			}

			if (Attributes & (u32)VertexAttribute::TANGENT)
			{
				memcpy(vertexDataP, createInfo->tangents.data() + i, sizeof(glm::vec3));
				vertexDataP += 3;
			}

			if (Attributes & (u32)VertexAttribute::BITANGENT)
			{
				memcpy(vertexDataP, createInfo->bitangents.data() + i, sizeof(glm::vec3));
				vertexDataP += 3;
			}

			if (Attributes & (u32)VertexAttribute::NORMAL)
			{
				memcpy(vertexDataP, createInfo->normals.data() + i, sizeof(glm::vec3));
				vertexDataP += 3;
			}

			if (Attributes & (u32)VertexAttribute::EXTRA_VEC4)
			{
				memcpy(vertexDataP, createInfo->extraVec4s.data() + i, sizeof(glm::vec4));
				vertexDataP += 4;
			}

			if (Attributes & (u32)VertexAttribute::EXTRA_INT)
			{
				memcpy(vertexDataP, createInfo->extraInts.data() + i, sizeof(i32));
				vertexDataP += 1;
			}
		}
		assert(vertexDataP == vertexData + (VertexStride / sizeof(real) * VertexCount));
	}

	void VertexBufferData::Destroy()
	{
		if (vertexData)
		{
			free(vertexData);
			vertexData = nullptr;
		}
		VertexCount = 0;
		VertexBufferSize = 0;
		VertexStride = 0;
		Attributes = 0;
	}

	void VertexBufferData::DescribeShaderVariables(Renderer* renderer, RenderID renderID)
	{
		const size_t vertexTypeCount = ARRAY_SIZE(s_VertexTypes);
		real* currentLocation = (real*)0;
		for (size_t i = 0; i < vertexTypeCount; ++i)
		{
			VertexAttribute vertexAttribute = VertexAttribute(1 << i);
			if (Attributes & (i32)vertexAttribute)
			{
				renderer->DescribeShaderVariable(renderID, s_VertexTypes[i].name, s_VertexTypes[i].size, DataType::FLOAT, false,
					(i32)VertexStride, currentLocation);
				currentLocation += s_VertexTypes[i].size;
			}
		}
	}
} // namespace flex
