#include "stdafx.h"

#include "ShaderUtils.h"

#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>

#include <glad/glad.h>

namespace flex
{
	Uniform::Type operator|(const Uniform::Type& lhs, const Uniform::Type& rhs)
	{
		return Uniform::Type((glm::uint)lhs | (glm::uint)rhs);
	}

	bool Uniform::HasUniform(Uniform::Type elements, Uniform::Type uniform)
	{
		return (elements & (glm::uint)uniform);
	}

	glm::uint Uniform::CalculateSize(Type elements)
	{
		glm::uint size = 0;

		if (HasUniform(elements, Uniform::Type::PROJECTION_MAT4)) size += sizeof(glm::mat4);
		if (HasUniform(elements, Uniform::Type::VIEW_MAT4)) size += sizeof(glm::mat4);
		if (HasUniform(elements, Uniform::Type::VIEW_INV_MAT4)) size += sizeof(glm::mat4);
		if (HasUniform(elements, Uniform::Type::VIEW_PROJECTION_MAT4)) size += sizeof(glm::mat4);
		if (HasUniform(elements, Uniform::Type::MODEL_MAT4)) size += sizeof(glm::mat4);
		if (HasUniform(elements, Uniform::Type::MODEL_INV_TRANSPOSE_MAT4)) size += sizeof(glm::mat4);
		if (HasUniform(elements, Uniform::Type::MODEL_VIEW_PROJECTION_MAT4)) size += sizeof(glm::mat4);
		if (HasUniform(elements, Uniform::Type::CAM_POS_VEC4)) size += sizeof(glm::vec4);
		if (HasUniform(elements, Uniform::Type::VIEW_DIR_VEC4)) size += sizeof(glm::vec4);
		if (HasUniform(elements, Uniform::Type::LIGHT_DIR_VEC4)) size += sizeof(glm::vec4);
		if (HasUniform(elements, Uniform::Type::AMBIENT_COLOR_VEC4)) size += sizeof(glm::vec4);
		if (HasUniform(elements, Uniform::Type::SPECULAR_COLOR_VEC4)) size += sizeof(glm::vec4);
		if (HasUniform(elements, Uniform::Type::USE_DIFFUSE_TEXTURE_INT)) size += sizeof(glm::int32);
		if (HasUniform(elements, Uniform::Type::USE_NORMAL_TEXTURE_INT)) size += sizeof(glm::int32);
		if (HasUniform(elements, Uniform::Type::USE_SPECULAR_TEXTURE_INT)) size += sizeof(glm::int32);

		return size;
	}
} // namespace flex