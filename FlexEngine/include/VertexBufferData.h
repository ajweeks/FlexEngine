#pragma once

#include <cstdlib>

#include <glm/integer.hpp>

#include "Logger.h"

namespace flex
{
	struct VertexBufferData
	{
		VertexBufferData() :
			pDataStart(nullptr),
			BufferSize(0),
			VertexStride(0),
			VertexCount(0)
		{}

		void VertexBufferData::Destroy()
		{
			free(pDataStart);
		}

		enum class Attribute : glm::uint
		{
			NONE = 0,
			POSITION =					(1 << 0),
			POSITION_2D =				(1 << 1),
			UV =						(1 << 2),
			UVW =						(1 << 3),
			COLOR_R8G8B8A8_UNORM =		(1 << 4),
			COLOR_R32G32B32A32_SFLOAT =	(1 << 5),
			TANGENT =					(1 << 6),
			BITANGENT =					(1 << 7),
			NORMAL =					(1 << 8),
		};

		inline bool HasAttribute(Attribute attribute) const
		{
			return (Attributes & ((glm::uint)attribute));
		}

		glm::uint CalculateStride() const
		{
			glm::uint stride = 0;

			if (Attributes & (glm::uint)VertexBufferData::Attribute::POSITION) stride += sizeof(glm::vec3);
			if (Attributes & (glm::uint)VertexBufferData::Attribute::POSITION_2D) stride += sizeof(glm::vec2);
			if (Attributes & (glm::uint)VertexBufferData::Attribute::UV) stride += sizeof(glm::vec2);
			if (Attributes & (glm::uint)VertexBufferData::Attribute::UVW) stride += sizeof(glm::vec3);
			if (Attributes & (glm::uint)VertexBufferData::Attribute::COLOR_R8G8B8A8_UNORM) stride += sizeof(glm::int32);
			if (Attributes & (glm::uint)VertexBufferData::Attribute::COLOR_R32G32B32A32_SFLOAT) stride += sizeof(glm::vec4);
			if (Attributes & (glm::uint)VertexBufferData::Attribute::TANGENT) stride += sizeof(glm::vec3);
			if (Attributes & (glm::uint)VertexBufferData::Attribute::BITANGENT) stride += sizeof(glm::vec3);
			if (Attributes & (glm::uint)VertexBufferData::Attribute::NORMAL) stride += sizeof(glm::vec3);

			if (stride == 0)
			{
				Logger::LogWarning("Vertex buffer stride is 0!");
			}

			return stride;
		}

		void* pDataStart;
		glm::uint BufferSize;
		glm::uint VertexStride;
		glm::uint VertexCount;
		glm::uint Attributes;
	};
} // namespace flex
