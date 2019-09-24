
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
		VertexStride = CalculateVertexStride(createInfo->attributes);
		VertexBufferSize = VertexCount * VertexStride;

		assert(vertexData == nullptr);
		vertexData = (real*)malloc_hooked(VertexBufferSize);
		if (vertexData == nullptr)
		{
			PrintError("Failed to allocate vertex buffer memory (%u bytes)\n", VertexBufferSize);
			return;
		}

		UpdateData(createInfo);
	}

	void VertexBufferData::InitializeDynamic(VertexAttributes attributes, u32 maxNumVerts)
	{
		VertexCount = maxNumVerts;
		Attributes = attributes;
		VertexStride = CalculateVertexStride(attributes);
		VertexBufferSize = VertexCount * VertexStride;

		assert(vertexData == nullptr);
		vertexData = (real*)malloc_hooked(VertexBufferSize);
		if (vertexData == nullptr)
		{
			PrintError("Failed to allocate vertex buffer memory (%u bytes)\n", VertexBufferSize);
			return;
		}
	}

	void VertexBufferData::UpdateData(CreateInfo const* createInfo)
	{
		assert(vertexData != nullptr);
		assert(VertexCount > 0);

		real* vertexDataP = vertexData;
		u32 count = glm::min(VertexCount, createInfo->positions_3D.empty() ? createInfo->positions_2D.size() : createInfo->positions_3D.size());
		for (u32 i = 0; i < count; ++i)
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

			if (Attributes & (u32)VertexAttribute::NORMAL)
			{
				memcpy(vertexDataP, createInfo->normals.data() + i, sizeof(glm::vec3));
				vertexDataP += 3;
			}

			if (Attributes & (u32)VertexAttribute::TANGENT)
			{
				memcpy(vertexDataP, createInfo->tangents.data() + i, sizeof(glm::vec3));
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
		assert(vertexDataP == vertexData + (VertexStride / sizeof(real) * count));
	}

	void VertexBufferData::Destroy()
	{
		if (vertexData)
		{
			free_hooked(vertexData);
			vertexData = nullptr;
		}
		VertexCount = 0;
		VertexBufferSize = 0;
		VertexStride = 0;
		Attributes = 0;
	}

	u32 VertexBufferData::CopyInto(real* dst, VertexAttributes usingAttributes)
	{
		assert(vertexData != nullptr);
		assert(VertexCount > 0);

		const real* initialDst = dst;
		real* src = vertexData;
		for (u32 i = 0; i < VertexCount; ++i)
		{
			if (usingAttributes & (u32)VertexAttribute::POSITION)
			{
				if (Attributes & (u32)VertexAttribute::POSITION)
				{
					memcpy(dst, src, sizeof(glm::vec3));
				}
				else
				{
					memcpy(dst, &VEC3_ZERO, sizeof(glm::vec3));
				}
				dst += 3;
			}

			if (usingAttributes & (u32)VertexAttribute::POSITION_2D)
			{
				if (Attributes & (u32)VertexAttribute::POSITION_2D)
				{
					memcpy(dst, src, sizeof(glm::vec2));
				}
				else
				{
					memcpy(dst, &VEC2_ZERO, sizeof(glm::vec2));
				}
				dst += 2;
			}

			if (usingAttributes & (u32)VertexAttribute::UV)
			{
				if (Attributes & (u32)VertexAttribute::UV)
				{
					memcpy(dst, src, sizeof(glm::vec2));
				}
				else
				{
					memcpy(dst, &VEC2_ZERO, sizeof(glm::vec2));
				}
				dst += 2;
			}

			if (usingAttributes & (u32)VertexAttribute::COLOR_R8G8B8A8_UNORM)
			{
				if (Attributes & (u32)VertexAttribute::COLOR_R8G8B8A8_UNORM)
				{
					memcpy(dst, src, sizeof(i32));
				}
				else
				{
					memcpy(dst, &COLOR32U_WHITE, sizeof(i32));
				}
				dst += 1;
			}

			if (usingAttributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
			{
				if (Attributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
				{
					memcpy(dst, src, sizeof(glm::vec4));
				}
				else
				{
					memcpy(dst, &COLOR128F_WHITE, sizeof(glm::vec4));
				}
				dst += 4;
			}

			if (usingAttributes & (u32)VertexAttribute::NORMAL)
			{
				if (Attributes & (u32)VertexAttribute::NORMAL)
				{
					memcpy(dst, src, sizeof(glm::vec3));
				}
				else
				{
					memcpy(dst, &VEC3_UP, sizeof(glm::vec3));
				}
				dst += 3;
			}

			if (usingAttributes & (u32)VertexAttribute::TANGENT)
			{
				if (Attributes & (u32)VertexAttribute::TANGENT)
				{
					memcpy(dst, src, sizeof(glm::vec3));
				}
				else
				{
					memcpy(dst, &VEC3_RIGHT, sizeof(glm::vec3));
				}
				dst += 3;
			}

			if (usingAttributes & (u32)VertexAttribute::EXTRA_VEC4)
			{
				if (Attributes & (u32)VertexAttribute::EXTRA_VEC4)
				{
					memcpy(dst, src, sizeof(glm::vec4));
				}
				else
				{
					memcpy(dst, &VEC4_ZERO, sizeof(glm::vec4));
				}
				dst += 4;
			}

			if (usingAttributes & (u32)VertexAttribute::EXTRA_INT)
			{
				if (Attributes & (u32)VertexAttribute::EXTRA_INT)
				{
					memcpy(dst, src, sizeof(i32));
				}
				else
				{
					i32 zero = 0;
					memcpy(dst, &zero, sizeof(i32));
				}
				dst += 1;
			}

			src += VertexStride / sizeof(real);
		}
		u32 bytesCopied = (dst - initialDst) * sizeof(real);
		return bytesCopied;
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
