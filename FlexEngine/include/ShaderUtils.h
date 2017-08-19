#pragma once

#include <string>

#include <glm\integer.hpp>

namespace flex
{
	struct Uniform
	{
		enum Type : glm::uint
		{
			NONE = 0,
			PROJECTION_MAT4 = (1 << 0),
			VIEW_MAT4 = (1 << 1),
			VIEW_INV_MAT4 = (1 << 2),
			VIEW_PROJECTION_MAT4 = (1 << 3),
			MODEL_MAT4 = (1 << 4),
			MODEL_INV_TRANSPOSE_MAT4 = (1 << 5),
			MODEL_VIEW_PROJECTION_MAT4 = (1 << 6),
			CAM_POS_VEC4 = (1 << 7),
			VIEW_DIR_VEC4 = (1 << 8),
			LIGHT_DIR_VEC4 = (1 << 9),
			AMBIENT_COLOR_VEC4 = (1 << 10),
			SPECULAR_COLOR_VEC4 = (1 << 11),
			USE_DIFFUSE_TEXTURE_INT = (1 << 12),
			USE_NORMAL_TEXTURE_INT = (1 << 13),
			USE_SPECULAR_TEXTURE_INT = (1 << 14),
		};

		static bool HasUniform(Type elements, Type uniform);
		static glm::uint CalculateSize(Type elements);
	};
	Uniform::Type operator|(const Uniform::Type& lhs, const Uniform::Type& rhs);

	enum class ShaderType
	{
		FRAGMENT, VERTEX
	};

	namespace ShaderUtils
	{
		bool LoadShaders(glm::uint program,
			std::string vertexShaderFilePath, glm::uint& vertexShaderID,
			std::string fragmentShaderFilePath, glm::uint& fragmentShaderID);
		void LinkProgram(glm::uint program);
	}
} // namespace flex
