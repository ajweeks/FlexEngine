#pragma once

#include <string>

#include <glm/integer.hpp>

namespace flex
{
	struct Uniform
	{
		enum Type : glm::uint
		{
			NONE = 0,
			UNIFORM_BUFFER_CONSTANT =		(1 << 0),
			UNIFORM_BUFFER_DYNAMIC =		(1 << 1),
			PROJECTION_MAT4 =				(1 << 2),
			VIEW_MAT4 =						(1 << 3),
			VIEW_INV_MAT4 =					(1 << 4),
			VIEW_PROJECTION_MAT4 =			(1 << 5),
			MODEL_MAT4 =					(1 << 6),
			MODEL_INV_TRANSPOSE_MAT4 =		(1 << 7),
			MODEL_VIEW_PROJECTION_MAT4 =	(1 << 8),
			CAM_POS_VEC4 =					(1 << 9),
			VIEW_DIR_VEC4 =					(1 << 10),
			LIGHT_DIR_VEC4 =				(1 << 11),
			AMBIENT_COLOR_VEC4 =			(1 << 12),
			SPECULAR_COLOR_VEC4 =			(1 << 13),
			USE_DIFFUSE_TEXTURE_INT =		(1 << 14), // bool for toggling texture usage
			DIFFUSE_TEXTURE_SAMPLER =		(1 << 15), // texture itself
			USE_NORMAL_TEXTURE_INT =		(1 << 16), 
			NORMAL_TEXTURE_SAMPLER =		(1 << 17), 
			USE_SPECULAR_TEXTURE_INT =		(1 << 18),
			SPECULAR_TEXTURE_SAMPLER =		(1 << 19),
			USE_CUBEMAP_TEXTURE_INT =		(1 << 20),
			CUBEMAP_TEXTURE_SAMPLER =		(1 << 21),

			MAX_ENUM =						(1 << 30)
		};

		static bool HasUniform(Type elements, Type uniform);
		static glm::uint CalculateSize(Type elements);
	};
	Uniform::Type operator|(const Uniform::Type& lhs, const Uniform::Type& rhs);

	enum class ShaderType
	{
		FRAGMENT, VERTEX
	};
} // namespace flex
