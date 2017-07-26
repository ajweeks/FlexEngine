#pragma once

#include <glm\detail\type_int.hpp>

struct VertexBufferData
{
	VertexBufferData() :
		pDataStart(nullptr),
		BufferSize(0),
		VertexStride(0),
		VertexCount(0)
	{}

	void* pDataStart;
	glm::uint BufferSize;
	glm::uint VertexStride;
	glm::uint VertexCount;

	void VertexBufferData::Destroy()
	{
		free(pDataStart);
	}
};
