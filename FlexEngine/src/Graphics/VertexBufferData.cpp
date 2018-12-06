
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
		BufferSize = VertexCount * VertexStride;

		void *pDataLocation = malloc(BufferSize);
		if (pDataLocation == nullptr)
		{
			PrintWarn("Failed to allocate memory required for vertex buffer data\n");
			return;
		}

		pDataStart = pDataLocation;

		for (u32 i = 0; i < VertexCount; ++i)
		{
			if (Attributes & (u32)VertexAttribute::POSITION)
			{
				memcpy(pDataLocation, createInfo->positions_3D.data() + i, sizeof(glm::vec3));
				pDataLocation = (real*)pDataLocation + 3;
			}

			if (Attributes & (u32)VertexAttribute::POSITION_2D)
			{
				memcpy(pDataLocation, createInfo->positions_2D.data() + i, sizeof(glm::vec2));
				pDataLocation = (real*)pDataLocation + 2;
			}

			if (Attributes & (u32)VertexAttribute::UV)
			{
				memcpy(pDataLocation, createInfo->texCoords_UV.data() + i, sizeof(glm::vec2));
				pDataLocation = (real*)pDataLocation + 2;
			}

			if (Attributes & (u32)VertexAttribute::UVW)
			{
				memcpy(pDataLocation, createInfo->texCoords_UVW.data() + i, sizeof(glm::vec3));
				pDataLocation = (real*)pDataLocation + 3;
			}

			if (Attributes & (u32)VertexAttribute::COLOR_R8G8B8A8_UNORM)
			{
				memcpy(pDataLocation, createInfo->colors_R8G8B8A8.data() + i, sizeof(i32));
				pDataLocation = (real*)pDataLocation + 1;
			}

			if (Attributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
			{
				memcpy(pDataLocation, createInfo->colors_R32G32B32A32.data() + i, sizeof(glm::vec4));
				pDataLocation = (real*)pDataLocation + 4;
			}

			if (Attributes & (u32)VertexAttribute::TANGENT)
			{
				memcpy(pDataLocation, createInfo->tangents.data() + i, sizeof(glm::vec3));
				pDataLocation = (real*)pDataLocation + 3;
			}

			if (Attributes & (u32)VertexAttribute::BITANGENT)
			{
				memcpy(pDataLocation, createInfo->bitangents.data() + i, sizeof(glm::vec3));
				pDataLocation = (real*)pDataLocation + 3;
			}

			if (Attributes & (u32)VertexAttribute::NORMAL)
			{
				memcpy(pDataLocation, createInfo->normals.data() + i, sizeof(glm::vec3));
				pDataLocation = (real*)pDataLocation + 3;
			}

			if (Attributes & (u32)VertexAttribute::EXTRA_VEC4)
			{
				memcpy(pDataLocation, createInfo->extraVec4s.data() + i, sizeof(glm::vec4));
				pDataLocation = (real*)pDataLocation + 4;
			}

			if (Attributes & (u32)VertexAttribute::EXTRA_INT)
			{
				memcpy(pDataLocation, createInfo->extraInts.data() + i, sizeof(i32));
				pDataLocation = (real*)pDataLocation + 1;
			}
		}
	}

	void VertexBufferData::Destroy()
	{
		if (pDataStart)
		{
			free(pDataStart);
			pDataStart = nullptr;
		}
		VertexCount = 0;
		BufferSize = 0;
		VertexStride = 0;
		Attributes = 0;
	}

	void VertexBufferData::DescribeShaderVariables(Renderer* renderer, RenderID renderID)
	{
		struct VertexType
		{
			std::string name;
			i32 size;
		};

		static VertexType vertexTypes[] = {
			{ "in_Position", 3 },
			{ "in_Position2D", 2 },
			{ "in_TexCoord", 2 },
			{ "in_TexCoord_UVW", 3 },
			{ "in_Color_32", 1 },
			{ "in_Color", 4 },
			{ "in_Tangent", 3 },
			{ "in_Bitangent", 3 },
			{ "in_Normal", 3 },
			{ "in_ExtraVec4", 4 },
			{ "in_ExtraInt", 1 },
		};

		const size_t vertexTypeCount = ARRAY_SIZE(vertexTypes);
		real* currentLocation = (real*)0;
		for (size_t i = 0; i < vertexTypeCount; ++i)
		{
			VertexAttribute vertexAttribute = VertexAttribute(1 << i);
			if (Attributes & (i32)vertexAttribute)
			{
				renderer->DescribeShaderVariable(renderID, vertexTypes[i].name, vertexTypes[i].size, DataType::FLOAT, false,
					(i32)VertexStride, currentLocation);
				currentLocation += vertexTypes[i].size;
			}
		}
	}
} // namespace flex
