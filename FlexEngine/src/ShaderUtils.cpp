#include "stdafx.h"

#include "ShaderUtils.h"

#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>

#include <glad/glad.h>

#include "Graphics/GL/GLHelpers.h"

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

	namespace ShaderUtils
	{
		bool LoadShaders(glm::uint program,
			std::string vertexShaderFilePath, glm::uint& vertexShaderID,
			std::string fragmentShaderFilePath, glm::uint& fragmentShaderID)
		{
			CheckGLErrorMessages();

			vertexShaderID = glCreateShader(GL_VERTEX_SHADER);
			CheckGLErrorMessages();

			fragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);
			CheckGLErrorMessages();

			std::stringstream vertexShaderCodeSS;
			std::ifstream vertexShaderFileStream(vertexShaderFilePath, std::ios::in);
			if (vertexShaderFileStream.is_open())
			{
				std::string line;
				while (getline(vertexShaderFileStream, line))
				{
					vertexShaderCodeSS << "\n" << line;
				}
				vertexShaderFileStream.close();
			}
			else
			{
				Logger::LogError("Could not open vertex shader: " + vertexShaderFilePath);
				return 0;
			}
			std::string vertexShaderCode = vertexShaderCodeSS.str();

			std::stringstream fragmentShaderCodeSS;
			std::ifstream fragmentShaderFileStream(fragmentShaderFilePath, std::ios::in);
			if (fragmentShaderFileStream.is_open())
			{
				std::string line;
				while (getline(fragmentShaderFileStream, line))
				{
					fragmentShaderCodeSS << "\n" << line;
				}
				fragmentShaderFileStream.close();
			}
			else
			{
				Logger::LogError("Could not open fragment shader: " + fragmentShaderFilePath);
				return 0;
			}
			std::string fragmentShaderCode = fragmentShaderCodeSS.str();

			GLint result = GL_FALSE;
			int infoLogLength;

			// Compile vertex shader
			char const* vertexSourcePointer = vertexShaderCode.c_str();
			glShaderSource(vertexShaderID, 1, &vertexSourcePointer, NULL);
			glCompileShader(vertexShaderID);

			glGetShaderiv(vertexShaderID, GL_COMPILE_STATUS, &result);
			if (result == GL_FALSE)
			{
				glGetShaderiv(vertexShaderID, GL_INFO_LOG_LENGTH, &infoLogLength);
				std::string vertexShaderErrorMessage;
				vertexShaderErrorMessage.resize((size_t)infoLogLength);
				glGetShaderInfoLog(vertexShaderID, infoLogLength, NULL, (GLchar*)vertexShaderErrorMessage.data());
				Logger::LogError(vertexShaderErrorMessage);
			}

			// Compile Fragment Shader
			char const* fragmentSourcePointer = fragmentShaderCode.c_str();
			glShaderSource(fragmentShaderID, 1, &fragmentSourcePointer, NULL);
			glCompileShader(fragmentShaderID);

			glGetShaderiv(fragmentShaderID, GL_COMPILE_STATUS, &result);
			if (result == GL_FALSE)
			{
				glGetShaderiv(fragmentShaderID, GL_INFO_LOG_LENGTH, &infoLogLength);
				std::string fragmentShaderErrorMessage;
				fragmentShaderErrorMessage.resize((size_t)infoLogLength);
				glGetShaderInfoLog(fragmentShaderID, infoLogLength, NULL, (GLchar*)fragmentShaderErrorMessage.data());
				Logger::LogError(fragmentShaderErrorMessage);
			}

			glAttachShader(program, vertexShaderID);
			glAttachShader(program, fragmentShaderID);
			CheckGLErrorMessages();

			return true;
		}

		void ShaderUtils::LinkProgram(glm::uint program)
		{
			glLinkProgram(program);

			GLint result = GL_FALSE;
			glGetProgramiv(program, GL_LINK_STATUS, &result);
			if (result == GL_FALSE)
			{
				int infoLogLength;
				glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
				std::string programErrorMessage;
				programErrorMessage.resize((size_t)infoLogLength);
				glGetProgramInfoLog(program, infoLogLength, NULL, (GLchar*)programErrorMessage.data());
				Logger::LogError(programErrorMessage);
			}
		}
	} // namespace ShaderUtils
} // namespace flex