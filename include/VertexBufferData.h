#pragma once

#include <glm\detail\type_int.hpp>

struct VertexBufferData
{
	VertexBufferData() :
		pDataStart(nullptr),
		//pVertexBuffer(nullptr),
		BufferSize(0),
		VertexStride(0),
		VertexCount(0),
		IndexCount(0),
		InputLayoutID(0) {}

	void* pDataStart;
	//ID3D11Buffer* pVertexBuffer;
	glm::uint BufferSize;
	glm::uint VertexStride;
	glm::uint VertexCount;
	glm::uint IndexCount;
	glm::uint InputLayoutID;

	void VertexBufferData::Destroy()
	{
		free(pDataStart);
		//SafeRelease(pVertexBuffer);
	}
};
