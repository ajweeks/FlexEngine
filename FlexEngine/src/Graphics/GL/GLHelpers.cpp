#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLHelpers.hpp"

#include <sstream>
#include <fstream>

#pragma warning(push, 0)
#include "stb_image.h"
#pragma warning(pop)

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
							(GLsizei)createInfo.textureSize.x, (GLsizei)createInfo.textureSize.y, 0, format, type, nullptr);
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
						glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, gbuffer.internalFormat, 
									 (GLsizei)createInfo.textureSize.x, (GLsizei)createInfo.textureSize.y, 0, gbuffer.format, gbufType, nullptr);
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
						glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT16,
							(GLsizei)createInfo.textureSize.x, (GLsizei)createInfo.textureSize.y, 
							0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
						CheckGLErrorMessages();
					}

					// Set this parameter when wanting to sample from this cubemap
					//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
				}
			}

			glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

			return success;
		}
		
		TextureParameters::TextureParameters(bool bGenMipMaps, bool bIsDepthTex) :
			bGenMipMaps(bGenMipMaps),
			bIsDepthTex(bIsDepthTex),
			borderColor(1.0f)
		{
			if (bGenMipMaps)
			{
				minFilter = GL_LINEAR_MIPMAP_LINEAR;
			}
		}

		GLTexture::GLTexture(GLuint handle, i32 width, i32 height, i32 depth):
			m_Handle(handle),
			m_Width(width),
			m_Height(height),
			m_Depth(depth)
		{
		}

		GLTexture::GLTexture(i32 width, i32 height, i32 internalFormat, GLenum format, GLenum type, i32 depth) :
			m_Width(width),
			m_Height(height),
			m_InternalFormat(internalFormat),
			m_Format(format),
			m_Type(type),
			m_Depth(depth)
		{
			glGenTextures(1, &m_Handle);
			CheckGLErrorMessages();
			m_Parameters = {};
		}

		GLTexture::~GLTexture()
		{
		}

		GLuint GLTexture::GetHandle()
		{
			return m_Handle;
		}

		glm::vec2i GLTexture::GetResolution()
		{
			return glm::vec2i(m_Width, m_Height);
		}

		void GLTexture::Build(void* data)
		{
			if (m_Depth == 1)
			{
				glBindTexture(GL_TEXTURE_2D, m_Handle);
				CheckGLErrorMessages();
				glTexImage2D(GL_TEXTURE_2D, 0, m_InternalFormat, m_Width, m_Height, 0, m_Format, m_Type, data);
				CheckGLErrorMessages();
			}
			else
			{
				glBindTexture(GL_TEXTURE_3D, m_Handle);
				CheckGLErrorMessages();
				glTexImage3D(GL_TEXTURE_3D, 0, m_InternalFormat, m_Width, m_Height, m_Depth, 0, m_Format, m_Type, data);
				CheckGLErrorMessages();
			}
		}

		void GLTexture::Destroy()
		{
			if (m_Handle != 0)
			{
				glDeleteTextures(1, &m_Handle);
				CheckGLErrorMessages();
			}
		}

		void GLTexture::SetParameters(TextureParameters params)
		{
			GLenum target = GetTarget();
			glBindTexture(target, m_Handle);
			if (m_Parameters.minFilter != params.minFilter)
			{
				glTexParameteri(target, GL_TEXTURE_MIN_FILTER, params.minFilter);
				CheckGLErrorMessages();
			}

			if (m_Parameters.magFilter != params.magFilter)
			{
				glTexParameteri(target, GL_TEXTURE_MAG_FILTER, params.magFilter);
				CheckGLErrorMessages();
			}

			if (m_Parameters.wrapS != params.wrapS)
			{
				glTexParameteri(target, GL_TEXTURE_WRAP_S, params.wrapS);
				CheckGLErrorMessages();
			}

			if (m_Parameters.wrapT != params.wrapT)
			{
				glTexParameteri(target, GL_TEXTURE_WRAP_T, params.wrapT);
				CheckGLErrorMessages();
			}

			if (m_Parameters.borderColor != params.borderColor)
			{
				glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, &params.borderColor.r);
				CheckGLErrorMessages();
			}

			if (m_Parameters.bGenMipMaps == false && params.bGenMipMaps == true)
			{
				glGenerateMipmap(target);
				CheckGLErrorMessages();
			}

			if (params.bIsDepthTex && m_Parameters.compareMode != params.compareMode)
			{
				glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, params.compareMode);//shadow map comp mode
				CheckGLErrorMessages();
			}

			if (m_Depth > 1)
			{
				if (m_Parameters.wrapR != params.wrapR)
				{
					glTexParameteri(target, GL_TEXTURE_WRAP_R, params.wrapR);
					CheckGLErrorMessages();
				}
			}

			m_Parameters = params;
		}

		GLenum GLTexture::GetTarget()
		{
			return (m_Depth == 1 ? GL_TEXTURE_2D : GL_TEXTURE_3D);
		}

		bool GLTexture::Resize(glm::vec2i newSize)
		{
			if (newSize.x > m_Width || newSize.y > m_Height)
			{
				m_Width = newSize.x; m_Height = newSize.y;
				glDeleteTextures(1, &m_Handle);
				glGenTextures(1, &m_Handle);
				CheckGLErrorMessages();
				Build();

				TextureParameters tempParams = m_Parameters;
				m_Parameters = {};
				SetParameters(tempParams);

				return true;
			}
			else
			{
				m_Width = newSize.x; m_Height = newSize.y;
				Build();

				return false;
			}
		}

		bool LoadGLShaders(u32 program, GLShader& shader)
		{
			CheckGLErrorMessages();

			bool bSuccess = true;

			bool bLoadGeometryShader = (!shader.shader.geometryShaderFilePath.empty());

			GLuint vertexShaderID = glCreateShader(GL_VERTEX_SHADER);
			CheckGLErrorMessages();

			GLuint fragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);
			CheckGLErrorMessages();

			GLuint geometryShaderID = 0;
			if (bLoadGeometryShader)
			{
				geometryShaderID = glCreateShader(GL_GEOMETRY_SHADER);
				CheckGLErrorMessages();
			}

			std::string vertFileName = shader.shader.vertexShaderFilePath;
			StripLeadingDirectories(vertFileName);
			std::string fragFileName = shader.shader.fragmentShaderFilePath;
			StripLeadingDirectories(fragFileName);
			std::string geomFileName;
			if (bLoadGeometryShader)
			{
				geomFileName = shader.shader.geometryShaderFilePath;
				StripLeadingDirectories(geomFileName);

				Logger::LogInfo("Loading shaders " + vertFileName + " & " + fragFileName + " & " + geomFileName);
			}
			else
			{
				Logger::LogInfo("Loading shaders " + vertFileName + " & " + fragFileName);
			}

			if (!ReadFile(shader.shader.vertexShaderFilePath, shader.shader.vertexShaderCode, false))
			{
				Logger::LogError("Could not find vertex shader: " + shader.shader.name);
			}
			shader.shader.vertexShaderCode.push_back('\0'); // Signal end of string with terminator character

			if (!ReadFile(shader.shader.fragmentShaderFilePath, shader.shader.fragmentShaderCode, false))
			{
				Logger::LogError("Could not find fragment shader: " + shader.shader.name);
			}
			shader.shader.fragmentShaderCode.push_back('\0'); // Signal end of string with terminator character

			if (bLoadGeometryShader)
			{
				if (!ReadFile(shader.shader.geometryShaderFilePath, shader.shader.geometryShaderCode, false))
				{
					Logger::LogError("Could not find geometry shader: " + shader.shader.name);
				}
				shader.shader.geometryShaderCode.push_back('\0'); // Signal end of string with terminator character
			}

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
				bSuccess = false;
			}

			// Compile fragment shader
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
				bSuccess = false;
			}
			
			if (bLoadGeometryShader)
			{
				// Compile geometry shader
				char const* geometrySourcePointer = shader.shader.geometryShaderCode.data();
				glShaderSource(geometryShaderID, 1, &geometrySourcePointer, NULL);
				glCompileShader(geometryShaderID);

				glGetShaderiv(geometryShaderID, GL_COMPILE_STATUS, &result);
				if (result == GL_FALSE)
				{
					glGetShaderiv(geometryShaderID, GL_INFO_LOG_LENGTH, &infoLogLength);
					std::string geometryShaderErrorMessage;
					geometryShaderErrorMessage.resize((size_t)infoLogLength);
					glGetShaderInfoLog(geometryShaderID, infoLogLength, NULL, 
						(GLchar*)geometryShaderErrorMessage.data());
					Logger::LogError(geometryShaderErrorMessage);
					bSuccess = false;
				}
			}

			glAttachShader(program, vertexShaderID);
			glAttachShader(program, fragmentShaderID);
			if (bLoadGeometryShader)
			{
				glAttachShader(program, geometryShaderID);
			}
			CheckGLErrorMessages();

			return bSuccess;
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

		GLShader::GLShader(const std::string& name,
						   const std::string& vertexShaderFilePath,
						   const std::string& fragmentShaderFilePath,
						   const std::string& geometryShaderFilePath) :
			shader(name, vertexShaderFilePath, fragmentShaderFilePath, geometryShaderFilePath)
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