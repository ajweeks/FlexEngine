
#include "stdafx.hpp"

#include "Graphics/VertexBufferData.hpp"

#include "Graphics/Renderer.hpp"
#include "Graphics/VertexAttribute.hpp"
#include "Helpers.hpp"

namespace flex
{
	void VertexBufferData::Initialize(const VertexBufferDataCreateInfo& createInfo)
	{
		bDynamic = false;
		VertexCount = (u32)createInfo.positions_3D.size();
		if (VertexCount == 0)
		{
			VertexCount = (u32)createInfo.positions_2D.size();
		}
		if (VertexCount == 0)
		{
			VertexCount = (u32)createInfo.positions_4D.size();
		}
		UsedVertexCount = VertexCount;
		Attributes = createInfo.attributes;
		VertexStride = CalculateVertexStride(createInfo.attributes);
		VertexBufferSize = VertexCount * VertexStride;
		UsedVertexBufferSize = VertexBufferSize;

		assert(vertexData == nullptr);
		vertexData = (real*)malloc(VertexBufferSize);
		if (vertexData == nullptr)
		{
			PrintError("Failed to allocate vertex buffer memory (%u bytes)\n", VertexBufferSize);
			return;
		}

		UpdateData(createInfo);
	}

	void VertexBufferData::InitializeDynamic(VertexAttributes attributes, u32 initialMaxVertCount)
	{
		bDynamic = true;
		VertexCount = initialMaxVertCount;
		UsedVertexCount = initialMaxVertCount;
		Attributes = attributes;
		VertexStride = CalculateVertexStride(attributes);
		VertexBufferSize = VertexCount * VertexStride;
		UsedVertexBufferSize = VertexBufferSize;

		if (VertexBufferSize > 0)
		{
			assert(vertexData == nullptr);
			vertexData = (real*)malloc(VertexBufferSize);
			if (vertexData == nullptr)
			{
				PrintError("Failed to allocate dynamic vertex buffer memory (%u bytes)\n", VertexBufferSize);
				return;
			}
		}
	}

	void VertexBufferData::UpdateData(const VertexBufferDataCreateInfo& createInfo)
	{
		u32 vertCountToUpdate = glm::max((u32)createInfo.positions_2D.size(), glm::max((u32)createInfo.positions_3D.size(), (u32)createInfo.positions_4D.size()));
		if (vertCountToUpdate * VertexStride > VertexBufferSize)
		{
			VertexCount = vertCountToUpdate;
			VertexBufferSize = VertexCount * VertexStride;
			free(vertexData);
			vertexData = (real*)malloc(VertexBufferSize);
			if (vertexData == nullptr)
			{
				PrintError("Failed to allocate dynamic vertex buffer memory (%u bytes)\n", VertexBufferSize);
				return;
			}
		}

		// NOTE: We _could_ just be doing a partial update, while still using the remaining
		// data - but that's unlikely. This assumes we always update the whole buffer.
		UsedVertexCount = vertCountToUpdate;
		UsedVertexBufferSize = UsedVertexCount * VertexStride;

		assert(vertexData != nullptr);

		real* vertexDataP = vertexData;
		for (u32 i = 0; i < vertCountToUpdate; ++i)
		{
			if (Attributes & (u32)VertexAttribute::POSITION)
			{
				memcpy(vertexDataP, createInfo.positions_3D.data() + i, sizeof(glm::vec3));
				vertexDataP += 3;
			}

			if (Attributes & (u32)VertexAttribute::POSITION2)
			{
				memcpy(vertexDataP, createInfo.positions_2D.data() + i, sizeof(glm::vec2));
				vertexDataP += 2;
			}

			if (Attributes & (u32)VertexAttribute::POSITION4)
			{
				memcpy(vertexDataP, createInfo.positions_4D.data() + i, sizeof(glm::vec4));
				vertexDataP += 4;
			}

			if (Attributes & (u32)VertexAttribute::VELOCITY3)
			{
				memcpy(vertexDataP, createInfo.velocities.data() + i, sizeof(glm::vec3));
				vertexDataP += 3;
			}

			if (Attributes & (u32)VertexAttribute::UV)
			{
				memcpy(vertexDataP, createInfo.texCoords_UV.data() + i, sizeof(glm::vec2));
				vertexDataP += 2;
			}

			if (Attributes & (u32)VertexAttribute::COLOUR_R8G8B8A8_UNORM)
			{
				memcpy(vertexDataP, createInfo.colours_R8G8B8A8.data() + i, sizeof(i32));
				vertexDataP += 1;
			}

			if (Attributes & (u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT)
			{
				memcpy(vertexDataP, createInfo.colours_R32G32B32A32.data() + i, sizeof(glm::vec4));
				vertexDataP += 4;
			}

			if (Attributes & (u32)VertexAttribute::NORMAL)
			{
				memcpy(vertexDataP, createInfo.normals.data() + i, sizeof(glm::vec3));
				vertexDataP += 3;
			}

			if (Attributes & (u32)VertexAttribute::TANGENT)
			{
				memcpy(vertexDataP, createInfo.tangents.data() + i, sizeof(glm::vec3));
				vertexDataP += 3;
			}

			if (Attributes & (u32)VertexAttribute::EXTRA_VEC4)
			{
				memcpy(vertexDataP, createInfo.extraVec4s.data() + i, sizeof(glm::vec4));
				vertexDataP += 4;
			}

			if (Attributes & (u32)VertexAttribute::EXTRA_INT)
			{
				memcpy(vertexDataP, createInfo.extraInts.data() + i, sizeof(i32));
				vertexDataP += 1;
			}
		}
		assert(vertexDataP == vertexData + (VertexStride / sizeof(real) * vertCountToUpdate));
	}

	void VertexBufferData::Destroy()
	{
		if (vertexData != nullptr)
		{
			free(vertexData);
			vertexData = nullptr;
		}
		VertexCount = 0;
		UsedVertexCount = 0;
		VertexBufferSize = 0;
		UsedVertexBufferSize = 0;
		VertexStride = 0;
		Attributes = 0;
	}

	void VertexBufferData::ShrinkIfExcessGreaterThan(real minExcess /* = 0.0f */)
	{
		// Only dynamic buffers can be resized
		assert(bDynamic);

		real excess = (real)(VertexBufferSize - UsedVertexBufferSize) / VertexBufferSize;
		if (excess >= minExcess)
		{
			VertexCount = UsedVertexCount;
			VertexBufferSize = VertexCount * VertexStride;
			real* newVertexData = (real*)malloc(VertexBufferSize);
			if (newVertexData == nullptr)
			{
				PrintError("Failed to allocate dynamic vertex buffer memory (%u bytes)\n", VertexBufferSize);
				return;
			}

			real* oldVertexData = vertexData;
			memcpy(newVertexData, vertexData, VertexBufferSize);
			vertexData = newVertexData;
			free(oldVertexData);
		}
	}

	u32 VertexBufferData::CopyInto(real* dst, VertexAttributes usingAttributes)
	{
		if (vertexData == nullptr || VertexCount == 0)
		{
			return 0;
		}

		const real* initialDst = dst;
		real* src = vertexData;
		for (u32 i = 0; i < VertexCount; ++i)
		{
			real* vertSrc = src;
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

			if (Attributes & (u32)VertexAttribute::POSITION)
			{
				src += 3;
			}

			if (usingAttributes & (u32)VertexAttribute::POSITION2)
			{
				if (Attributes & (u32)VertexAttribute::POSITION2)
				{
					memcpy(dst, src, sizeof(glm::vec2));
				}
				else
				{
					memcpy(dst, &VEC2_ZERO, sizeof(glm::vec2));
				}
				dst += 2;
			}

			if (Attributes & (u32)VertexAttribute::POSITION2)
			{
				src += 2;
			}

			if (usingAttributes & (u32)VertexAttribute::POSITION4)
			{
				if (Attributes & (u32)VertexAttribute::POSITION4)
				{
					memcpy(dst, src, sizeof(glm::vec4));
				}
				else
				{
					memcpy(dst, &VEC4_ZERO, sizeof(glm::vec4));
				}
				dst += 4;
			}

			if (Attributes & (u32)VertexAttribute::POSITION4)
			{
				src += 4;
			}

			if (usingAttributes & (u32)VertexAttribute::VELOCITY3)
			{
				if (Attributes & (u32)VertexAttribute::VELOCITY3)
				{
					memcpy(dst, src, sizeof(glm::vec3));
				}
				else
				{
					memcpy(dst, &VEC4_ZERO, sizeof(glm::vec3));
				}
				dst += 3;
			}

			if (Attributes & (u32)VertexAttribute::VELOCITY3)
			{
				src += 3;
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

			if (Attributes & (u32)VertexAttribute::UV)
			{
				src += 2;
			}

			if (usingAttributes & (u32)VertexAttribute::COLOUR_R8G8B8A8_UNORM)
			{
				if (Attributes & (u32)VertexAttribute::COLOUR_R8G8B8A8_UNORM)
				{
					memcpy(dst, src, sizeof(i32));
				}
				else
				{
					memcpy(dst, &COLOUR32U_WHITE, sizeof(i32));
				}
				dst += 1;
			}

			if (Attributes & (u32)VertexAttribute::COLOUR_R8G8B8A8_UNORM)
			{
				src += 1;
			}

			if (usingAttributes & (u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT)
			{
				if (Attributes & (u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT)
				{
					memcpy(dst, src, sizeof(glm::vec4));
				}
				else
				{
					memcpy(dst, &COLOUR128F_WHITE, sizeof(glm::vec4));
				}
				dst += 4;
			}

			if (Attributes & (u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT)
			{
				src += 4;
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

			if (Attributes & (u32)VertexAttribute::NORMAL)
			{
				src += 3;
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

			if (Attributes & (u32)VertexAttribute::TANGENT)
			{
				src += 3;
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

			if (Attributes & (u32)VertexAttribute::EXTRA_VEC4)
			{
				src += 4;
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

			if (Attributes & (u32)VertexAttribute::EXTRA_INT)
			{
				src += 1;
			}

			assert(src == (vertSrc + VertexStride / sizeof(real)));
		}
		u32 bytesCopied = (u32)(dst - initialDst) * sizeof(real);
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

	void VertexBufferData::ResizeForPresentAttributes(VertexBufferDataCreateInfo& createInfo, u32 vertCount)
	{
		if (createInfo.attributes & (u32)VertexAttribute::POSITION)
		{
			createInfo.positions_3D.resize(vertCount, VEC3_ZERO);
		}

		if (createInfo.attributes & (u32)VertexAttribute::POSITION2)
		{
			createInfo.positions_2D.resize(vertCount, VEC2_ZERO);
		}

		if (createInfo.attributes & (u32)VertexAttribute::POSITION4)
		{
			createInfo.positions_4D.resize(vertCount, VEC4_ZERO);
		}

		if (createInfo.attributes & (u32)VertexAttribute::VELOCITY3)
		{
			createInfo.velocities.resize(vertCount, VEC3_ZERO);
		}

		if (createInfo.attributes & (u32)VertexAttribute::UV)
		{
			createInfo.texCoords_UV.resize(vertCount, VEC2_ZERO);
		}

		if (createInfo.attributes & (u32)VertexAttribute::COLOUR_R8G8B8A8_UNORM)
		{
			createInfo.colours_R8G8B8A8.resize(vertCount, COLOUR32U_WHITE);
		}

		if (createInfo.attributes & (u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT)
		{
			createInfo.colours_R32G32B32A32.resize(vertCount, COLOUR128F_WHITE);
		}

		if (createInfo.attributes & (u32)VertexAttribute::NORMAL)
		{
			createInfo.normals.resize(vertCount, VEC3_UP);
		}

		if (createInfo.attributes & (u32)VertexAttribute::TANGENT)
		{
			createInfo.tangents.resize(vertCount, VEC3_RIGHT);
		}

		if (createInfo.attributes & (u32)VertexAttribute::EXTRA_VEC4)
		{
			createInfo.extraVec4s.resize(vertCount, VEC4_ZERO);
		}

		if (createInfo.attributes & (u32)VertexAttribute::EXTRA_INT)
		{
			createInfo.extraInts.resize(vertCount, 0);
		}
	}

	u32 HashVertexBufferDataCreateInfo(const VertexBufferDataCreateInfo& info)
	{
		u32 result = (u32)info.attributes;

		// TODO: Test
		// TODO: Use smarter hash
		const real scale = 1000.0f; // Inverse of how much a quantity needs to change by to compute a different hash
		for (const glm::vec2& pos : info.positions_2D) result += (u32)((pos.x + pos.y) * scale);
		for (const glm::vec3& pos : info.positions_3D) result += (u32)((pos.x + pos.y + pos.z) * scale);
		for (const glm::vec4& pos : info.positions_4D) result += (u32)((pos.x + pos.y + pos.z + pos.w) * scale);
		for (const glm::vec3& v : info.velocities) result += (u32)((v.x + v.y + v.z));
		for (const glm::vec2& uv : info.texCoords_UV) result += (u32)((uv.x + uv.y) * scale);
		for (i32 i : info.colours_R8G8B8A8) result += (u32)(i);
		for (const glm::vec4& col : info.colours_R32G32B32A32) result += (u32)((col.x + col.y + col.z + col.w) * scale);
		for (const glm::vec3& tangent : info.tangents) result += (u32)((tangent.x + tangent.y + tangent.z) * scale);
		for (const glm::vec3& normal : info.normals) result += (u32)((normal.x + normal.y + normal.z) * scale);

		for (const glm::vec4& v : info.extraVec4s) result += (u32)((v.x + v.y + v.z + v.w) * scale);
		for (i32 i : info.extraInts) result += (u32)(i);

		return result;
	}
} // namespace flex
