
#include "stdafx.h"

#include "VertexBufferData.h"
#include "VertexAttribute.h"

#include <cstdlib>

#include "Logger.h"

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

		for (UINT i = 0; i < VertexCount; ++i)
		{
			if (Attributes & (glm::uint)VertexAttribute::POSITION)
			{
				memcpy(pDataLocation, &createInfo->positions_3D[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (Attributes & (glm::uint)VertexAttribute::UV)
			{
				memcpy(pDataLocation, &createInfo->texCoords_UV[i], sizeof(glm::vec2));
				pDataLocation = (float*)pDataLocation + 2;
			}

			if (Attributes & (glm::uint)VertexAttribute::UVW)
			{
				memcpy(pDataLocation, &createInfo->texCoords_UVW[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (Attributes & (glm::uint)VertexAttribute::COLOR_R8G8B8A8_UNORM)
			{
				memcpy(pDataLocation, &createInfo->colors_R8G8B8A8[i], sizeof(glm::int32));
				pDataLocation = (float*)pDataLocation + 1;
			}

			if (Attributes & (glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
			{
				memcpy(pDataLocation, &createInfo->colors_R32G32B32A32[i], sizeof(glm::vec4));
				pDataLocation = (float*)pDataLocation + 4;
			}

			if (Attributes & (glm::uint)VertexAttribute::TANGENT)
			{
				memcpy(pDataLocation, &createInfo->tangents[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (Attributes & (glm::uint)VertexAttribute::BITANGENT)
			{
				memcpy(pDataLocation, &createInfo->bitangents[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (Attributes & (glm::uint)VertexAttribute::NORMAL)
			{
				memcpy(pDataLocation, &createInfo->normals[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}
		}
	}

	void VertexBufferData::Destroy()
	{
		free(pDataStart);
	}

	void VertexBufferData::DescribeShaderVariables(Renderer* renderer, RenderID renderID)
	{
		struct VertexType
		{
			std::string name;
			int size;
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
		float* currentLocation = (float*)0;
		for (size_t i = 0; i < vertexTypeCount; ++i)
		{
			VertexAttribute vertexAttribute = VertexAttribute(1 << i);
			if (Attributes & (int)vertexAttribute)
			{
				renderer->DescribeShaderVariable(renderID, vertexTypes[i].name, vertexTypes[i].size, Renderer::Type::FLOAT, false,
					(int)VertexStride, currentLocation);
				currentLocation += vertexTypes[i].size;
			}
		}
	}
} // namespace flex
