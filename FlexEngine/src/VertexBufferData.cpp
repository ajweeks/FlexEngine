
#include "stdafx.hpp"

#include "VertexBufferData.hpp"

#include <cstdlib>

#include "VertexAttribute.hpp"
#include "Logger.hpp"

namespace flex
{
	VertexBufferData::VertexBufferData() :
		pDataStart(nullptr),
		BufferSize(0),
		VertexStride(0),
		VertexCount(0)
	{
	}

	void VertexBufferData::Initialize(CreateInfo* createInfo)
	{
		VertexCount = createInfo->positions_3D.size();
		if (VertexCount == 0) VertexCount = createInfo->positions_2D.size();
		Attributes = createInfo->attributes;
		VertexStride = CalculateVertexStride(Attributes);
		BufferSize = VertexCount * VertexStride;

		void *pDataLocation = malloc(BufferSize);
		if (pDataLocation == nullptr)
		{
			Logger::LogWarning("Vertex Buffer Data failed to allocate memory required for vertex buffer data");
			return;
		}

		pDataStart = pDataLocation;

		for (u32 i = 0; i < VertexCount; ++i)
		{
			if (Attributes & (u32)VertexAttribute::POSITION)
			{
				memcpy(pDataLocation, &createInfo->positions_3D[i], sizeof(glm::vec3));
				pDataLocation = (real*)pDataLocation + 3;
			}

			if (Attributes & (u32)VertexAttribute::POSITION_2D)
			{
				memcpy(pDataLocation, &createInfo->positions_2D[i], sizeof(glm::vec2));
				pDataLocation = (real*)pDataLocation + 2;
			}

			if (Attributes & (u32)VertexAttribute::UV)
			{
				memcpy(pDataLocation, &createInfo->texCoords_UV[i], sizeof(glm::vec2));
				pDataLocation = (real*)pDataLocation + 2;
			}

			if (Attributes & (u32)VertexAttribute::UVW)
			{
				memcpy(pDataLocation, &createInfo->texCoords_UVW[i], sizeof(glm::vec3));
				pDataLocation = (real*)pDataLocation + 3;
			}

			if (Attributes & (u32)VertexAttribute::COLOR_R8G8B8A8_UNORM)
			{
				memcpy(pDataLocation, &createInfo->colors_R8G8B8A8[i], sizeof(i32));
				pDataLocation = (real*)pDataLocation + 1;
			}

			if (Attributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
			{
				memcpy(pDataLocation, &createInfo->colors_R32G32B32A32[i], sizeof(glm::vec4));
				pDataLocation = (real*)pDataLocation + 4;
			}

			if (Attributes & (u32)VertexAttribute::TANGENT)
			{
				memcpy(pDataLocation, &createInfo->tangents[i], sizeof(glm::vec3));
				pDataLocation = (real*)pDataLocation + 3;
			}

			if (Attributes & (u32)VertexAttribute::BITANGENT)
			{
				memcpy(pDataLocation, &createInfo->bitangents[i], sizeof(glm::vec3));
				pDataLocation = (real*)pDataLocation + 3;
			}

			if (Attributes & (u32)VertexAttribute::NORMAL)
			{
				memcpy(pDataLocation, &createInfo->normals[i], sizeof(glm::vec3));
				pDataLocation = (real*)pDataLocation + 3;
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
		};

		const size_t vertexTypeCount = sizeof(vertexTypes) / sizeof(vertexTypes[0]);
		real* currentLocation = (real*)0;
		for (size_t i = 0; i < vertexTypeCount; ++i)
		{
			VertexAttribute vertexAttribute = VertexAttribute(1 << i);
			if (Attributes & (i32)vertexAttribute)
			{
				renderer->DescribeShaderVariable(renderID, vertexTypes[i].name, vertexTypes[i].size, Renderer::Type::FLOAT, false,
					(i32)VertexStride, currentLocation);
				currentLocation += vertexTypes[i].size;
			}
		}
	}
} // namespace flex
