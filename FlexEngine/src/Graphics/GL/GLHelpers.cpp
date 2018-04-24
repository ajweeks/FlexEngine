#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLHelpers.hpp"

#include <sstream>
#include <fstream>

#include "stb_image.h"

#include "Helpers.hpp"

namespace flex
{
	namespace gl
	{
		bool GenerateGLTexture_Empty(u32& textureID, const glm::vec2i& dimensions, bool generateMipMaps, GLenum internalFormat, GLenum format, GLenum type)
		{
			return GenerateGLTexture_EmptyWithParams(textureID, dimensions, generateMipMaps, internalFormat, format, type, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
		}

		bool GenerateGLTexture_EmptyWithParams(u32& textureID, const glm::vec2i& dimensions, bool generateMipMaps, GLenum internalFormat, GLenum format, GLenum type, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter)
		{
			assert(dimensions.x <= Renderer::MAX_TEXTURE_DIM);
			assert(dimensions.y <= Renderer::MAX_TEXTURE_DIM);


			glGenTextures(1, &textureID);
			glBindTexture(GL_TEXTURE_2D, textureID);
			CheckGLErrorMessages();

			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, dimensions.x, dimensions.y, 0, format, type, 0);
			CheckGLErrorMessages();
			if (generateMipMaps)
			{
				glGenerateMipmap(GL_TEXTURE_2D);
				CheckGLErrorMessages();
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sWrap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tWrap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
			CheckGLErrorMessages();

			glBindTexture(GL_TEXTURE_2D, 0);

			glBindVertexArray(0);

			CheckGLErrorMessages();

			return true;
		}

		bool GenerateGLTexture(u32& textureID, const std::string& filePath, bool flipVertically, bool generateMipMaps)
		{
			return GenerateGLTextureWithParams(textureID, filePath, flipVertically, generateMipMaps, GL_REPEAT, GL_REPEAT, GL_LINEAR, GL_LINEAR);
		}

		bool GenerateGLTextureWithParams(u32& textureID, const std::string& filePath, bool flipVertically, bool generateMipMaps, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter)
		{
			GLFWimage image = LoadGLFWimage(filePath, false, flipVertically);

			if (!image.pixels)
			{
				return false;
			}

			glGenTextures(1, &textureID);
			glBindTexture(GL_TEXTURE_2D, textureID);
			CheckGLErrorMessages();

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, image.pixels);
			CheckGLErrorMessages();
			if (generateMipMaps)
			{
				glGenerateMipmap(GL_TEXTURE_2D);
				CheckGLErrorMessages();
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sWrap);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tWrap);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
			CheckGLErrorMessages();

			glBindTexture(GL_TEXTURE_2D, 0);

			glBindVertexArray(0);

			DestroyGLFWimage(image);

			CheckGLErrorMessages();

			return true;
		}

		bool GenerateHDRGLTexture(u32& textureID, const std::string& filePath, bool flipVertically, bool generateMipMaps)
		{
			return GenerateHDRGLTextureWithParams(textureID, filePath, flipVertically, generateMipMaps, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
		}

		bool GenerateHDRGLTextureWithParams(u32& textureID, const std::string& filePath, bool flipVertically, bool generateMipMaps, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter)
		{
			HDRImage image = {};
			if (!image.Load(filePath, flipVertically))
			{
				return false;
			}

			glGenTextures(1, &textureID);
			glBindTexture(GL_TEXTURE_2D, textureID);
			CheckGLErrorMessages();

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, image.width, image.height, 0, GL_RGBA, GL_FLOAT, image.pixels);
			CheckGLErrorMessages();
			if (generateMipMaps)
			{
				glGenerateMipmap(GL_TEXTURE_2D);
				CheckGLErrorMessages();
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sWrap);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tWrap);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
			CheckGLErrorMessages();

			glBindTexture(GL_TEXTURE_2D, 0);

			image.Free();

			CheckGLErrorMessages();

			return true;
		}

		bool GenerateGLCubemap(GLCubemapCreateInfo& createInfo)
		{
			bool success = true;

			if (createInfo.textureID == nullptr)
			{
				return false;
			}

			glGenTextures(1, createInfo.textureID);
			glBindTexture(GL_TEXTURE_CUBE_MAP, *createInfo.textureID);
			CheckGLErrorMessages();

			const GLint internalFormat = createInfo.HDR ? GL_RGB16F : GL_RGB;
			const GLenum format = GL_RGB;
			const GLenum type = createInfo.HDR ? GL_FLOAT : GL_UNSIGNED_BYTE;

			if (createInfo.filePaths[0].empty()) // Don't generate pixel data
			{
				if (createInfo.textureSize.x <= 0 || createInfo.textureSize.y <= 0 ||
					createInfo.textureSize.x >= Renderer::MAX_TEXTURE_DIM || createInfo.textureSize.y >= Renderer::MAX_TEXTURE_DIM)
				{
					Logger::LogError("Invalid cubemap dimensions: " + 
						std::to_string(createInfo.textureSize.x) + "x" + std::to_string(createInfo.textureSize.y));
					success = false;
				}
				else
				{
					for (size_t i = 0; i < 6; ++i)
					{
						glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat,
							createInfo.textureSize.x, createInfo.textureSize.y, 0, format, type, nullptr);
						CheckGLErrorMessages();
					}
				}
			}
			else // Load in 6 images to the generated cubemap
			{
				for (size_t i = 0; i < createInfo.filePaths.size(); ++i)
				{
					GLFWimage image = LoadGLFWimage(createInfo.filePaths[i]);

					if (image.pixels)
					{
						glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat, image.width, image.height, 0, format, type, image.pixels);
						CheckGLErrorMessages();

						DestroyGLFWimage(image);
					}
					else
					{
						Logger::LogError("Could not load cube map at " + createInfo.filePaths[i]);
						success = false;
					}
				}
			}

			if (createInfo.textureGBufferIDs && !createInfo.textureGBufferIDs->empty())
			{
				const GLint gbufInternalFormat = GL_RGBA16F;
				const GLenum gbufFormat = GL_RGBA;
				const GLenum gbufType = GL_FLOAT;

				i32 binding = 0;

				// Generate GBuffers
				for (auto& gbuffer : *createInfo.textureGBufferIDs)
				{
					glGenTextures(1, &gbuffer.id);
					glBindTexture(GL_TEXTURE_CUBE_MAP, gbuffer.id);
					CheckGLErrorMessages();
					for (i32 i = 0; i < 6; i++)
					{
						glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, gbuffer.internalFormat, createInfo.textureSize.x, createInfo.textureSize.y, 0, gbuffer.format, gbufType, nullptr);
						CheckGLErrorMessages();
					}

					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // This parameter is *absolutely* necessary for sampling to work
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					CheckGLErrorMessages();

					
					i32 uniformLocation = glGetUniformLocation(createInfo.program, gbuffer.name);
					CheckGLErrorMessages();
					if (uniformLocation == -1)
					{
						Logger::LogWarning(std::string(gbuffer.name) + " was not found!");
					}
					else
					{
						glUniform1i(uniformLocation, binding);
					}
					CheckGLErrorMessages();
					++binding;
				}

				glBindTexture(GL_TEXTURE_CUBE_MAP, *createInfo.textureID);
			}

			if (success) // Only proceed when generation succeeded
			{
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
					(createInfo.generateMipmaps || createInfo.enableTrilinearFiltering) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				CheckGLErrorMessages();

				if (createInfo.generateMipmaps)
				{
					glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
					CheckGLErrorMessages();
				}

				if (createInfo.generateDepthBuffers && createInfo.depthTextureID)
				{
					glGenTextures(1, createInfo.depthTextureID);
					glBindTexture(GL_TEXTURE_CUBE_MAP, *createInfo.depthTextureID);
					CheckGLErrorMessages();

					for (size_t i = 0; i < 6; ++i)
					{
						glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT24,
							createInfo.textureSize.x, createInfo.textureSize.y, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
						CheckGLErrorMessages();
					}

					// Set this parameter when wanting to sample from this cubemap
					//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
				}
			}

			glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

			return success;
		}

		bool LoadGLShaders(u32 program, GLShader& shader)
		{
			CheckGLErrorMessages();

			bool success = true;

			GLuint vertexShaderID = glCreateShader(GL_VERTEX_SHADER);
			CheckGLErrorMessages();

			GLuint fragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);
			CheckGLErrorMessages();

			std::string vertFileName = shader.shader.vertexShaderFilePath;
			StripLeadingDirectories(vertFileName);
			std::string fragFileName = shader.shader.fragmentShaderFilePath;
			StripLeadingDirectories(fragFileName);
			Logger::LogInfo("Loading shaders " + vertFileName + " & " + fragFileName);

			if (!ReadFile(shader.shader.vertexShaderFilePath, shader.shader.vertexShaderCode))
			{
				Logger::LogError("Could not find vertex shader " + shader.shader.name);
			}
			shader.shader.vertexShaderCode.push_back('\0'); // Signal end of string with terminator character

			if (!ReadFile(shader.shader.fragmentShaderFilePath, shader.shader.fragmentShaderCode))
			{
				Logger::LogError("Could not find fragment shader " + shader.shader.name);
			}
			shader.shader.fragmentShaderCode.push_back('\0'); // Signal end of string with terminator character

			GLint result = GL_FALSE;
			i32 infoLogLength;

			// Compile vertex shader
			char const* vertexSourcepointer = shader.shader.vertexShaderCode.data(); // TODO: Test
			glShaderSource(vertexShaderID, 1, &vertexSourcepointer, NULL);
			glCompileShader(vertexShaderID);

			glGetShaderiv(vertexShaderID, GL_COMPILE_STATUS, &result);
			if (result == GL_FALSE)
			{
				glGetShaderiv(vertexShaderID, GL_INFO_LOG_LENGTH, &infoLogLength);
				std::string vertexShaderErrorMessage;
				vertexShaderErrorMessage.resize((size_t)infoLogLength);
				glGetShaderInfoLog(vertexShaderID, infoLogLength, NULL, (GLchar*)vertexShaderErrorMessage.data());
				Logger::LogError(vertexShaderErrorMessage);
				success = false;
			}

			// Compile Fragment Shader
			char const* fragmentSourcePointer = shader.shader.fragmentShaderCode.data();
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
				success = false;
			}

			glAttachShader(program, vertexShaderID);
			glAttachShader(program, fragmentShaderID);
			CheckGLErrorMessages();

			return success;
		}

		bool LinkProgram(u32 program)
		{
			glLinkProgram(program);

			GLint result = GL_FALSE;
			glGetProgramiv(program, GL_LINK_STATUS, &result);
			if (result == GL_FALSE)
			{
				i32 infoLogLength;
				glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
				std::string programErrorMessage;
				programErrorMessage.resize((size_t)infoLogLength);
				glGetProgramInfoLog(program, infoLogLength, NULL, (GLchar*)programErrorMessage.data());
				Logger::LogError(programErrorMessage);

				return false;
			}

			return true;
		}

		GLShader::GLShader(const std::string& name, const std::string& vertexShaderFilePath, const std::string& fragmentShaderFilePath) :
			shader(name, vertexShaderFilePath, fragmentShaderFilePath)
		{
		}

		GLboolean BoolToGLBoolean(bool value)
		{
			return (value ? GL_TRUE : GL_FALSE);
		}

		GLuint BufferTargetToGLTarget(BufferTarget bufferTarget)
		{
			switch (bufferTarget)
			{
			case BufferTarget::ARRAY_BUFFER:			return GL_ARRAY_BUFFER;
			case BufferTarget::ELEMENT_ARRAY_BUFFER:	return GL_ELEMENT_ARRAY_BUFFER;
			default:
				Logger::LogError("Unhandled BufferTarget passed to BufferTargetToGLTarget: " + std::to_string((i32)bufferTarget));
				return GL_INVALID_ENUM;
			}
		}

		GLenum DataTypeToGLType(DataType dataType)
		{
			switch (dataType)
			{
			case DataType::BYTE:			return GL_BYTE;
			case DataType::UNSIGNED_BYTE:	return GL_UNSIGNED_BYTE;
			case DataType::SHORT:			return GL_SHORT;
			case DataType::UNSIGNED_SHORT:	return GL_UNSIGNED_SHORT;
			case DataType::INT:				return GL_INT;
			case DataType::UNSIGNED_INT:	return GL_UNSIGNED_INT;
			case DataType::FLOAT:			return GL_FLOAT;
			case DataType::DOUBLE:			return GL_DOUBLE;
			default:
				Logger::LogError("Unhandled DataType passed to DataTypeToGLType: " + std::to_string((i32)dataType));
				return GL_INVALID_ENUM;
			}
		}

		GLenum UsageFlagToGLUsageFlag(UsageFlag usage)
		{
			switch (usage)
			{
			case UsageFlag::STATIC_DRAW:	return GL_STATIC_DRAW;
			case UsageFlag::DYNAMIC_DRAW:	return GL_DYNAMIC_DRAW;
			default:
				Logger::LogError("Unhandled usage flag passed to UsageFlagToGLUsageFlag: " + std::to_string((i32)usage));
				return GL_INVALID_ENUM;
			}
		}

		GLenum TopologyModeToGLMode(TopologyMode topology)
		{
			switch (topology)
			{
			case TopologyMode::POINT_LIST:		return GL_POINTS;
			case TopologyMode::LINE_LIST:		return GL_LINES;
			case TopologyMode::LINE_LOOP:		return GL_LINE_LOOP;
			case TopologyMode::LINE_STRIP:		return GL_LINE_STRIP;
			case TopologyMode::TRIANGLE_LIST:	return GL_TRIANGLES;
			case TopologyMode::TRIANGLE_STRIP:	return GL_TRIANGLE_STRIP;
			case TopologyMode::TRIANGLE_FAN:	return GL_TRIANGLE_FAN;
			default:
				Logger::LogError("Unhandled topology mode passed to TopologyModeToGLMode: " + std::to_string((i32)topology));
				return GL_INVALID_ENUM;
			}
		}

		u32 CullFaceToGLCullFace(CullFace cullFace)
		{
			switch (cullFace)
			{
			case CullFace::BACK:			return GL_BACK;
			case CullFace::FRONT:			return GL_FRONT;
			case CullFace::FRONT_AND_BACK:	return GL_FRONT_AND_BACK;
			default:
				Logger::LogError("Unhandled cull face passed to CullFaceToGLCullFace: " + std::to_string((i32)cullFace));
				return GL_INVALID_ENUM;
			}
		}

		GLenum DepthTestFuncToGlenum(DepthTestFunc func)
		{
			switch (func)
			{
			case DepthTestFunc::ALWAYS:		return GL_ALWAYS;
			case DepthTestFunc::NEVER:		return GL_NEVER;
			case DepthTestFunc::LESS:		return GL_LESS;
			case DepthTestFunc::LEQUAL:		return GL_LEQUAL;
			case DepthTestFunc::GREATER:	return GL_GREATER;
			case DepthTestFunc::GEQUAL:		return GL_GEQUAL;
			case DepthTestFunc::EQUAL:		return GL_EQUAL;
			case DepthTestFunc::NOTEQUAL:	return GL_NOTEQUAL;
			default:
				Logger::LogError("Unhandled depth test func face passed to DepthTestFuncToGlenum: " + std::to_string((i32)func));
				return GL_INVALID_ENUM;
			}
		}

		bool GLBooleanToBool(GLboolean boolean)
		{
			return (boolean == GL_TRUE);
		}

		BufferTarget GLTargetToBufferTarget(GLuint target)
		{
			if (target == GL_ARRAY_BUFFER)
			{
				return BufferTarget::ARRAY_BUFFER;
			}
			else if (target == GL_ELEMENT_ARRAY_BUFFER)
			{
				return BufferTarget::ELEMENT_ARRAY_BUFFER;
			}
			else
			{
				Logger::LogError("Unhandled GLTarget passed to GLTargetToBufferTarget: " + std::to_string((i32)target));
				return BufferTarget::NONE;
			}
		}

		DataType GLTypeToDataType(GLenum type)
		{
			if (type == GL_BYTE)
			{
				return DataType::BYTE;
			}
			else if (type == GL_UNSIGNED_BYTE)
			{
				return DataType::UNSIGNED_BYTE;
			}
			else if (type == GL_SHORT)
			{
				return DataType::SHORT;
			}
			else if (type == GL_UNSIGNED_SHORT)
			{
				return DataType::UNSIGNED_SHORT;
			}
			else if (type == GL_INT)
			{
				return DataType::INT;
			}
			else if (type == GL_UNSIGNED_INT)
			{
				return DataType::UNSIGNED_INT;
			}
			else if (type == GL_FLOAT)
			{
				return DataType::FLOAT;
			}
			else if (type == GL_DOUBLE)
			{
				return DataType::DOUBLE;
			}
			else
			{
				Logger::LogError("Unhandled GLType passed to GLTypeToDataType: " + std::to_string(type));
				return DataType::NONE;
			}
		}

		UsageFlag GLUsageFlagToUsageFlag(GLenum usage)
		{
			if (usage == GL_STATIC_DRAW)
			{
				return UsageFlag::STATIC_DRAW;
			}
			else if (usage == GL_DYNAMIC_DRAW)
			{
				return UsageFlag::DYNAMIC_DRAW;
			}
			else
			{
				Logger::LogError("Unhandled GL usage flag passed to GLUsageFlagToUsageFlag: " + std::to_string(usage));
				return UsageFlag::NONE;
			}
		}

		TopologyMode GLModeToTopologyMode(GLenum mode)
		{
			if (mode == GL_POINTS)
			{
				return TopologyMode::POINT_LIST;
			}
			else if (mode == GL_LINES)
			{
				return TopologyMode::LINE_LIST;
			}
			else if (mode == GL_LINE_LOOP)
			{
				return TopologyMode::LINE_LOOP;
			}
			else if (mode == GL_LINE_STRIP)
			{
				return TopologyMode::LINE_STRIP;
			}
			else if (mode == GL_TRIANGLES)
			{
				return TopologyMode::TRIANGLE_LIST;
			}
			else if (mode == GL_TRIANGLE_STRIP)
			{
				return TopologyMode::TRIANGLE_STRIP;
			}
			else if (mode == GL_TRIANGLE_FAN)
			{
				return TopologyMode::TRIANGLE_FAN;
			}
			else
			{
				Logger::LogError("Unhandled GL mode passed to GLModeToTopologyMode: " + std::to_string(mode));
				return TopologyMode::NONE;
			}
		}

		CullFace GLCullFaceToCullFace(u32 cullFace)
		{
			if (cullFace == GL_BACK)
			{
				return CullFace::BACK;
			}
			else if (cullFace == GL_FRONT)
			{
				return CullFace::FRONT;
			}
			else if (cullFace == GL_FRONT_AND_BACK)
			{
				return CullFace::FRONT_AND_BACK;
			}
			else
			{
				Logger::LogError("Unhandled GL cull face passed to GLCullFaceToCullFace: " + std::to_string(cullFace));
				return CullFace::NONE;
			}
		}

		DepthTestFunc GlenumToDepthTestFunc(GLenum depthTestFunc)
		{
			if (depthTestFunc == GL_ALWAYS)
			{
				return DepthTestFunc::ALWAYS;
			}
			else if (depthTestFunc == GL_NEVER)
			{
				return DepthTestFunc::NEVER;
			}
			else if (depthTestFunc == GL_LESS)
			{
				return DepthTestFunc::LESS;
			}
			else if (depthTestFunc == GL_LEQUAL)
			{
				return DepthTestFunc::LEQUAL;
			}
			else if (depthTestFunc == GL_GREATER)
			{
				return DepthTestFunc::GREATER;
			}
			else if (depthTestFunc == GL_GEQUAL)
			{
				return DepthTestFunc::GEQUAL;
			}
			else if (depthTestFunc == GL_EQUAL)
			{
				return DepthTestFunc::EQUAL;
			}
			else if (depthTestFunc == GL_NOTEQUAL)
			{
				return DepthTestFunc::NOTEQUAL;
			}
			else
			{
				Logger::LogError("Unhandled GL enum passed to GlenumToDepthTestFunc: " + std::to_string(depthTestFunc));
				return DepthTestFunc::NONE;
			}
		}
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL