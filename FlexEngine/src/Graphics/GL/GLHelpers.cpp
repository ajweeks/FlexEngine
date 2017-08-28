#include "stdafx.h"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLHelpers.h"

#include <sstream>
#include <fstream>

namespace flex
{
	namespace gl
	{
		GLFWimage LoadGLFWimage(const std::string& filePath)
		{
			GLFWimage result = {};

			int channels;
			unsigned char* data = SOIL_load_image(filePath.c_str(), &result.width, &result.height, &channels, SOIL_LOAD_RGB);

			if (data == 0)
			{
				Logger::LogError("SOIL loading error: " + std::string(SOIL_last_result()) + "\nimage filepath: " + filePath);
				return result;
			}
			else
			{
				result.pixels = static_cast<unsigned char*>(data);
			}

			return result;
		}

		void DestroyGLFWimage(const GLFWimage& image)
		{
			SOIL_free_image_data(image.pixels);
		}

		void GenerateGLTexture(glm::uint& textureID, const std::string& filePath, int sWrap, int tWrap, int minFilter, int magFilter)
		{
			glGenTextures(1, &textureID);
			glBindTexture(GL_TEXTURE_2D, textureID);

			GLFWimage image = LoadGLFWimage(filePath);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, image.pixels);
			glGenerateMipmap(GL_TEXTURE_2D);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sWrap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tWrap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

			glBindTexture(GL_TEXTURE_2D, 0);

			glBindVertexArray(0);

			DestroyGLFWimage(image);
		}

		void GenerateCubemapTextures(glm::uint& textureID, const std::array<std::string, 6> filePaths)
		{
			glGenTextures(1, &textureID);
			glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
			CheckGLErrorMessages();

			for (size_t i = 0; i < filePaths.size(); ++i)
			{
				GLFWimage image = LoadGLFWimage(filePaths[i]);

				if (image.pixels)
				{
					glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, image.pixels);
					CheckGLErrorMessages();

					DestroyGLFWimage(image);
				}
				else
				{
					Logger::LogError("Could not load cube map at " + filePaths[i]);
					DestroyGLFWimage(image);
				}
			}

			glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
			CheckGLErrorMessages();
		}


		bool LoadGLShaders(glm::uint program,
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

		void LinkProgram(glm::uint program)
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

		GLuint BufferTargetToGLTarget(Renderer::BufferTarget bufferTarget)
		{
			GLuint glTarget = 0;

			if (bufferTarget == Renderer::BufferTarget::ARRAY_BUFFER) glTarget = GL_ARRAY_BUFFER;
			else if (bufferTarget == Renderer::BufferTarget::ELEMENT_ARRAY_BUFFER) glTarget = GL_ELEMENT_ARRAY_BUFFER;
			else Logger::LogError("Unhandled BufferTarget passed to GLRenderer: " + std::to_string((int)bufferTarget));

			return glTarget;
		}

		GLenum TypeToGLType(Renderer::Type type)
		{
			GLenum glType = 0;

			if (type == Renderer::Type::BYTE) glType = GL_BYTE;
			else if (type == Renderer::Type::UNSIGNED_BYTE) glType = GL_UNSIGNED_BYTE;
			else if (type == Renderer::Type::SHORT) glType = GL_SHORT;
			else if (type == Renderer::Type::UNSIGNED_SHORT) glType = GL_UNSIGNED_SHORT;
			else if (type == Renderer::Type::INT) glType = GL_INT;
			else if (type == Renderer::Type::UNSIGNED_INT) glType = GL_UNSIGNED_INT;
			else if (type == Renderer::Type::FLOAT) glType = GL_FLOAT;
			else if (type == Renderer::Type::DOUBLE) glType = GL_DOUBLE;
			else Logger::LogError("Unhandled Type passed to GLRenderer: " + std::to_string((int)type));

			return glType;
		}

		GLenum UsageFlagToGLUsageFlag(Renderer::UsageFlag usage)
		{
			GLenum glUsage = 0;

			if (usage == Renderer::UsageFlag::STATIC_DRAW) glUsage = GL_STATIC_DRAW;
			else if (usage == Renderer::UsageFlag::DYNAMIC_DRAW) glUsage = GL_DYNAMIC_DRAW;
			else Logger::LogError("Unhandled usage flag passed to GLRenderer: " + std::to_string((int)usage));

			return glUsage;
		}

		GLenum TopologyModeToGLMode(Renderer::TopologyMode topology)
		{
			switch (topology)
			{
			case Renderer::TopologyMode::POINT_LIST: return GL_POINTS;
			case Renderer::TopologyMode::LINE_LIST: return GL_LINES;
			case Renderer::TopologyMode::LINE_LOOP: return GL_LINE_LOOP;
			case Renderer::TopologyMode::LINE_STRIP: return GL_LINE_STRIP;
			case Renderer::TopologyMode::TRIANGLE_LIST: return GL_TRIANGLES;
			case Renderer::TopologyMode::TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
			case Renderer::TopologyMode::TRIANGLE_FAN: return GL_TRIANGLE_FAN;
			default: return GL_INVALID_ENUM;
			}
		}

		glm::uint CullFaceToGLMode(Renderer::CullFace cullFace)
		{
			switch (cullFace)
			{
			case Renderer::CullFace::BACK: return GL_BACK;
			case Renderer::CullFace::FRONT: return GL_FRONT;
			case Renderer::CullFace::NONE: return GL_NONE; // TODO: This doesn't work, does it?
			default: return GL_FALSE;
			}
		}

		RenderObject::RenderObject(RenderID renderID, std::string name) :
			renderID(renderID)
		{
			info = {};
			info.name = name;
		}
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL