
#include "stdafx.h"

#include "VertexBufferData.h"

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
		VertexStride = CalculateStride();
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
			if (Attributes & (glm::uint)AttributeBit::POSITION)
			{
				memcpy(pDataLocation, &createInfo->positions_3D[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (Attributes & (glm::uint)AttributeBit::UV)
			{
				memcpy(pDataLocation, &createInfo->texCoords_UV[i], sizeof(glm::vec2));
				pDataLocation = (float*)pDataLocation + 2;
			}

			if (Attributes & (glm::uint)AttributeBit::UVW)
			{
				memcpy(pDataLocation, &createInfo->texCoords_UVW[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (Attributes & (glm::uint)AttributeBit::COLOR_R8G8B8A8_UNORM)
			{
				memcpy(pDataLocation, &createInfo->colors_R8G8B8A8[i], sizeof(glm::int32));
				pDataLocation = (float*)pDataLocation + 1;
			}

			if (Attributes & (glm::uint)AttributeBit::COLOR_R32G32B32A32_SFLOAT)
			{
				memcpy(pDataLocation, &createInfo->colors_R32G32B32A32[i], sizeof(glm::vec4));
				pDataLocation = (float*)pDataLocation + 4;
			}

			if (Attributes & (glm::uint)AttributeBit::TANGENT)
			{
				memcpy(pDataLocation, &createInfo->tangents[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (Attributes & (glm::uint)AttributeBit::BITANGENT)
			{
				memcpy(pDataLocation, &createInfo->bitangents[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (Attributes & (glm::uint)AttributeBit::NORMAL)
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

	bool VertexBufferData::HasAttribute(AttributeBit attributeBits) const
	{
		return (Attributes & ((glm::uint)attributeBits));
	}

	glm::uint VertexBufferData::CalculateStride() const
	{
		glm::uint stride = 0;

		if (Attributes & (glm::uint)VertexBufferData::AttributeBit::POSITION) stride += sizeof(glm::vec3);
		if (Attributes & (glm::uint)VertexBufferData::AttributeBit::POSITION_2D) stride += sizeof(glm::vec2);
		if (Attributes & (glm::uint)VertexBufferData::AttributeBit::UV) stride += sizeof(glm::vec2);
		if (Attributes & (glm::uint)VertexBufferData::AttributeBit::UVW) stride += sizeof(glm::vec3);
		if (Attributes & (glm::uint)VertexBufferData::AttributeBit::COLOR_R8G8B8A8_UNORM) stride += sizeof(glm::int32);
		if (Attributes & (glm::uint)VertexBufferData::AttributeBit::COLOR_R32G32B32A32_SFLOAT) stride += sizeof(glm::vec4);
		if (Attributes & (glm::uint)VertexBufferData::AttributeBit::TANGENT) stride += sizeof(glm::vec3);
		if (Attributes & (glm::uint)VertexBufferData::AttributeBit::BITANGENT) stride += sizeof(glm::vec3);
		if (Attributes & (glm::uint)VertexBufferData::AttributeBit::NORMAL) stride += sizeof(glm::vec3);

		if (stride == 0)
		{
			Logger::LogWarning("Vertex buffer stride is 0!");
		}

		return stride;
	}

	void VertexBufferData::DescribeShaderVariables(Renderer* renderer, RenderID renderID)
	{
		struct VertexType
		{
			std::string name;
			int size;
		};

		VertexType vertexTypes[] = {
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
			VertexBufferData::Attribute vertexType = VertexBufferData::Attribute(1 << i);
			if (Attributes & (int)vertexType)
			{
				renderer->DescribeShaderVariable(renderID, vertexTypes[i].name, vertexTypes[i].size, Renderer::Type::FLOAT, false,
					(int)VertexStride, currentLocation);
				currentLocation += vertexTypes[i].size;
			}
		}
	}
} // namespace flex
