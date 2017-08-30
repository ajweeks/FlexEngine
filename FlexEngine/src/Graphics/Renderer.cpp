#include "stdafx.h"

#include "Graphics/Renderer.h"

namespace flex
{
	Renderer::Renderer()
	{
	}

	Renderer::~Renderer()
	{
	}

	Renderer::SceneInfo& Renderer::GetSceneInfo()
	{
		return m_SceneInfo;
	}

	Renderer::Uniform::Type operator|(const Renderer::Uniform::Type& lhs, const Renderer::Uniform::Type& rhs)
	{
		return Renderer::Uniform::Type((glm::uint)lhs | (glm::uint)rhs);
	}

	bool Renderer::Uniform::HasUniform(Uniform::Type elements, Uniform::Type uniform)
	{
		return (elements & (glm::uint)uniform);
	}

	glm::uint Renderer::Uniform::CalculateSize(Type elements)
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
		if (HasUniform(elements, Uniform::Type::DIR_LIGHT)) size += sizeof(glm::vec4);
		if (HasUniform(elements, Uniform::Type::POINT_LIGHTS_VEC)) size += sizeof(glm::vec4);
		if (HasUniform(elements, Uniform::Type::AMBIENT_COLOR_VEC4)) size += sizeof(glm::vec4);
		if (HasUniform(elements, Uniform::Type::SPECULAR_COLOR_VEC4)) size += sizeof(glm::vec4);
		if (HasUniform(elements, Uniform::Type::USE_DIFFUSE_TEXTURE_INT)) size += sizeof(glm::int32);
		if (HasUniform(elements, Uniform::Type::USE_NORMAL_TEXTURE_INT)) size += sizeof(glm::int32);
		if (HasUniform(elements, Uniform::Type::USE_SPECULAR_TEXTURE_INT)) size += sizeof(glm::int32);

		return size;
	}
} // namespace flex