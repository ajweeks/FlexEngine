#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLHelpers.hpp"

IGNORE_WARNINGS_PUSH
#include "stb_image.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Profiler.hpp"

namespace flex
{
	namespace gl
	{
		bool GenerateGLTexture_Empty(u32& textureID, const glm::vec2u& dimensions, bool bGenerateMipMaps, GLenum internalFormat, GLenum format, GLenum type)
		{
			return GenerateGLTexture_EmptyWithParams(textureID, dimensions, bGenerateMipMaps, internalFormat, format, type, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
		}

		bool GenerateGLTexture_EmptyWithParams(u32& textureID, const glm::vec2u& dimensions, bool bGenerateMipMaps, GLenum internalFormat, GLenum format, GLenum type, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter)
		{
			assert(dimensions.x <= MAX_TEXTURE_DIM);
			assert(dimensions.y <= MAX_TEXTURE_DIM);


			glGenTextures(1, &textureID);
			glBindTexture(GL_TEXTURE_2D, textureID);

			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, dimensions.x, dimensions.y, 0, format, type, 0);

			if (bGenerateMipMaps)
			{
				glGenerateMipmap(GL_TEXTURE_2D);
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sWrap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tWrap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

			glBindTexture(GL_TEXTURE_2D, 0);
			glBindVertexArray(0);

			return true;
		}

		bool GenerateGLTexture(u32& textureID,
							   const std::string& filePath,
							   i32 requestedChannelCount,
							   bool flipVertically,
							   bool bGenerateMipMaps,
							   ImageInfo* infoOut /* = nullptr */)
		{
			return GenerateGLTextureWithParams(textureID,
											   filePath,
											   requestedChannelCount,
											   flipVertically,
											   bGenerateMipMaps,
											   GL_REPEAT,
											   GL_REPEAT,
											   GL_LINEAR,
											   GL_LINEAR,
											   infoOut);
		}

		bool GenerateGLTextureWithParams(u32& textureID,
										 const std::string& filePath,
										 i32 requestedChannelCount,
										 bool flipVertically,
										 bool bGenerateMipMaps,
										 i32 sWrap,
										 i32 tWrap,
										 i32 minFilter,
										 i32 magFilter,
										 ImageInfo* infoOut /* = nullptr */)
		{
			assert(requestedChannelCount == 3 ||
				   requestedChannelCount == 4);

			u32 channelCount = 0;
			GLFWimage image = LoadGLFWimage(filePath, requestedChannelCount, flipVertically, &channelCount);

			if (!image.pixels)
			{
				return false;
			}

			if (infoOut)
			{
				infoOut->width = image.width;
				infoOut->height = image.height;
				infoOut->channelCount = channelCount;
			}

			glGenTextures(1, &textureID);
			glBindTexture(GL_TEXTURE_2D, textureID);

			if (channelCount == 4)
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels);
			}
			else
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, image.pixels);
			}

			if (bGenerateMipMaps)
			{
				glGenerateMipmap(GL_TEXTURE_2D);
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sWrap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tWrap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

			glBindTexture(GL_TEXTURE_2D, 0);
			glBindVertexArray(0);
			DestroyGLFWimage(image);

			return true;
		}

		bool GenerateHDRGLTexture(u32& textureID, const std::string& filePath, i32 requestedChannelCount, bool flipVertically, bool bGenerateMipMaps, ImageInfo* infoOut /* = nullptr */)
		{
			return GenerateHDRGLTextureWithParams(textureID, filePath, requestedChannelCount, flipVertically, bGenerateMipMaps, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR, infoOut);
		}

		bool GenerateHDRGLTextureWithParams(u32& textureID, const std::string& filePath, i32 requestedChannelCount, bool flipVertically, bool bGenerateMipMaps, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter, ImageInfo* infoOut /* = nullptr */)
		{
			assert(requestedChannelCount == 3 ||
				   requestedChannelCount == 4);

			HDRImage image = {};
			if (!image.Load(filePath, requestedChannelCount, flipVertically))
			{
				return false;
			}

			if (infoOut)
			{
				infoOut->width = image.width;
				infoOut->height = image.height;
				infoOut->channelCount = image.channelCount;
			}

			glGenTextures(1, &textureID);
			glBindTexture(GL_TEXTURE_2D, textureID);

			if (requestedChannelCount == 4)
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, image.width, image.height, 0, GL_RGBA, GL_FLOAT, image.pixels);
			}
			else
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, image.width, image.height, 0, GL_RGB, GL_FLOAT, image.pixels);
			}

			if (bGenerateMipMaps)
			{
				glGenerateMipmap(GL_TEXTURE_2D);
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sWrap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tWrap);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

			glBindTexture(GL_TEXTURE_2D, 0);
			image.Free();

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

			const GLint internalFormat = createInfo.HDR ? GL_RGB16F : GL_RGB;
			const GLenum format = GL_RGB;
			const GLenum type = createInfo.HDR ? GL_FLOAT : GL_UNSIGNED_BYTE;

			if (createInfo.filePaths[0].empty()) // Don't generate pixel data
			{
				if (createInfo.textureSize.x <= 0 || createInfo.textureSize.y <= 0 ||
					createInfo.textureSize.x >= MAX_TEXTURE_DIM || createInfo.textureSize.y >= MAX_TEXTURE_DIM)
				{
					PrintError("Invalid cubemap dimensions: %.2fx%.2f\n",
						createInfo.textureSize.x, createInfo.textureSize.y);
					success = false;
				}
				else
				{
					for (size_t i = 0; i < 6; ++i)
					{
						glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat,
							(GLsizei)createInfo.textureSize.x, (GLsizei)createInfo.textureSize.y, 0, format, type, nullptr);
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

						DestroyGLFWimage(image);
					}
					else
					{
						PrintError("Could not load cube map at %s\n", createInfo.filePaths[i].c_str());
						success = false;
					}
				}
			}

			if (createInfo.textureGBufferIDs && !createInfo.textureGBufferIDs->empty())
			{
				i32 binding = 0;

				// Generate GBuffers
				for (gl::GLCubemapGBuffer& gbuffer : *createInfo.textureGBufferIDs)
				{
					glGenTextures(1, &gbuffer.id);
					glBindTexture(GL_TEXTURE_CUBE_MAP, gbuffer.id);

					for (i32 i = 0; i < 6; i++)
					{
						glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, gbuffer.internalFormat,
									 (GLsizei)createInfo.textureSize.x, (GLsizei)createInfo.textureSize.y, 0, gbuffer.format, GL_FLOAT, nullptr);
					}

					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // This parameter is *absolutely* necessary for sampling to work
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

					i32 uniformLocation = glGetUniformLocation(createInfo.program, gbuffer.name);
					if (uniformLocation == -1)
					{
						PrintWarn("%s was not found!\n", gbuffer.name);
					}
					else
					{
						glUniform1i(uniformLocation, binding);
					}

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
					(createInfo.generateMipmaps || createInfo.bEnableTrilinearFiltering) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

				if (createInfo.generateMipmaps)
				{
					glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
				}

				if (createInfo.generateDepthBuffers && createInfo.depthTextureID)
				{
					glGenTextures(1, createInfo.depthTextureID);
					glBindTexture(GL_TEXTURE_CUBE_MAP, *createInfo.depthTextureID);

					for (size_t i = 0; i < 6; ++i)
					{
						glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT16,
							(GLsizei)createInfo.textureSize.x, (GLsizei)createInfo.textureSize.y,
							0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
					}

					// Set this parameter when wanting to sample from this cubemap
					//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
				}
			}

			glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

			return success;
		}

		TextureParameters::TextureParameters(bool bGenMipMaps /* = false */, bool bDepthTex /* = false */) :
			borderColor(1.0f),
			bGenMipMaps(bGenMipMaps),
			bDepthTex(bDepthTex)
		{
			if (bGenMipMaps)
			{
				minFilter = GL_LINEAR_MIPMAP_LINEAR;
			}
		}

		GLTexture::GLTexture()
		{
		}

		GLTexture::GLTexture(const std::string& name, u32 width, u32 height, u32 channelCount, i32 internalFormat, GLenum format, GLenum type) :
			name(name),
			width(width),
			height(height),
			channelCount(channelCount),
			internalFormat(internalFormat),
			format(format),
			type(type)
		{
		}

		GLTexture::GLTexture(const std::string& relativeFilePath,
							 u32 channelCount,
							 bool bFlipVertically,
							 bool bGenerateMipMaps,
							 bool bHDR) :
			relativeFilePath(relativeFilePath),
			channelCount(channelCount),
			bHasMipMaps(bGenerateMipMaps),
			bFlipVerticallyOnLoad(bFlipVertically),
			bHDR(bHDR)
		{
			if (name.empty())
			{
				name = StripLeadingDirectories(relativeFilePath);
			}
		}

		GLTexture::~GLTexture()
		{
			if (asyncSave != nullptr)
			{
				if (!asyncSave->bComplete)
				{
					asyncSave->WaitToComplete();
				}
				delete asyncSave;
				asyncSave = nullptr;
			}
		}

		bool GLTexture::CreateEmpty()
		{
			assert(!bLoaded);

			bool bSucceeded = GenerateGLTexture_Empty(handle,
													  glm::vec2u(width, height),
													  bHasMipMaps,
													  internalFormat,
													  format,
													  type);

			if (bSucceeded)
			{
				bLoaded = true;
			}

			return bSucceeded;
		}

		bool GLTexture::LoadFromFile()
		{
			assert(!bLoaded);

			ImageInfo infoOut;
			// TODO: Also pass along bit depth succeeded
			bool bSucceeded = false;
			if (bHDR)
			{
				bSucceeded = GenerateHDRGLTexture(handle, relativeFilePath, channelCount, bFlipVerticallyOnLoad, bHasMipMaps, &infoOut);
			}
			else
			{
				bSucceeded = GenerateGLTexture(handle, relativeFilePath, channelCount, bFlipVerticallyOnLoad, bHasMipMaps, &infoOut);
			}

			if (bSucceeded)
			{
				width = infoOut.width;
				height = infoOut.height;
				channelCount = infoOut.channelCount;

				bLoaded = true;
			}
			else
			{
				PrintError("Failed to load GL texture at filepath: %s\n", relativeFilePath.c_str());
			}

			return bSucceeded;
		}

		bool GLTexture::CreateFromMemory(void* memory, bool bFloat, const TextureParameters& params)
		{
			if (memory == nullptr || width == 0 || height == 0)
			{
				return false;
			}

			glGenTextures(1, &handle);
			glBindTexture(GL_TEXTURE_2D, handle);

			if (channelCount == 4)
			{
				glTexImage2D(GL_TEXTURE_2D, 0, bFloat ? GL_RGBA16F : GL_RGBA, width, height, 0, GL_RGBA, bFloat ? GL_FLOAT : GL_UNSIGNED_BYTE, memory);
			}
			else
			{
				glTexImage2D(GL_TEXTURE_2D, 0, bFloat ? GL_RGB16F : GL_RGB, width, height, 0, GL_RGB, bFloat ? GL_FLOAT : GL_UNSIGNED_BYTE, memory);
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, params.minFilter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, params.magFilter);

			glBindTexture(GL_TEXTURE_2D, 0);
			glBindVertexArray(0);

			return true;
		}

		void GLTexture::Reload()
		{
			Destroy();

			if (!relativeFilePath.empty())
			{
				LoadFromFile();
			}
		}

		void GLTexture::Build(void* data)
		{
			glBindTexture(GL_TEXTURE_2D, handle);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, data);
		}

		void GLTexture::Destroy()
		{
			if (handle != 0)
			{
				glDeleteTextures(1, &handle);
			}

			bLoaded = false;
		}

		glm::vec2u GLTexture::GetResolution()
		{
			return glm::vec2u(width, height);
		}

		void GLTexture::SetParameters(TextureParameters params)
		{
			glBindTexture(GL_TEXTURE_2D, handle);
			if (m_Parameters.minFilter != params.minFilter)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, params.minFilter);
			}

			if (m_Parameters.magFilter != params.magFilter)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, params.magFilter);
			}

			if (m_Parameters.wrapS != params.wrapS)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, params.wrapS);
			}

			if (m_Parameters.wrapT != params.wrapT)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, params.wrapT);
			}

			if (m_Parameters.borderColor != params.borderColor)
			{
				glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &params.borderColor.r);
			}

			if (m_Parameters.bGenMipMaps == false && params.bGenMipMaps == true)
			{
				glGenerateMipmap(GL_TEXTURE_2D);
			}

			if (params.bDepthTex && m_Parameters.compareMode != params.compareMode)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, params.compareMode);//shadow map comp mode
			}

			m_Parameters = params;
		}

		bool GLTexture::Resize(const glm::vec2u& newSize)
		{
			if (newSize.x > width || newSize.y > height)
			{
				width = newSize.x;
				height = newSize.y;
				glDeleteTextures(1, &handle);
				glGenTextures(1, &handle);
				Build();

				TextureParameters tempParams = m_Parameters;
				m_Parameters = {};
				SetParameters(tempParams);

				return true;
			}
			else
			{
				width = newSize.x;
				height = newSize.y;
				Build();

				return false;
			}
		}

		std::string GLTexture::GetRelativeFilePath() const
		{
			return relativeFilePath;
		}

		std::string GLTexture::GetName() const
		{
			return name;
		}

		bool GLTexture::SaveToFile(const std::string& absoluteFilePath, ImageFormat inFormat, bool bFlipVertically)
		{
			return SaveTextureToFile(absoluteFilePath, inFormat, handle, width, height, channelCount, bFlipVertically);
		}

		bool GLTexture::SaveToFileAsync(const std::string& absoluteFilePath, ImageFormat inFormat, bool bFlipVertically)
		{
			if (asyncSave != nullptr)
			{
				if (asyncSave->bComplete)
				{
					delete asyncSave;
					asyncSave = nullptr;
				}
				else
				{
					PrintError("Attempted to save texture asynchronously before previous save has finished!\n");
					return false;
				}
			}
			StartAsyncTextureSaveToFile(absoluteFilePath, inFormat, handle, width, height, channelCount, bFlipVertically, &asyncSave, (void*)this,
				[](void* userData)
				{
					GLTexture* tex = reinterpret_cast<GLTexture*>(userData);
					delete tex->asyncSave;
					tex->asyncSave = nullptr;
				});
			return true;
		}

		// TODO: CLEANUP: Combine identical parts of SaveTextureToFile & StartAsyncTextureSaveToFile to reduce code duplication
		bool SaveTextureToFile(const std::string& absoluteFilePath, ImageFormat format, GLuint handle, i32 width, i32 height, i32 channelCount, bool bFlipVertically)
		{
			assert(channelCount == 3 || channelCount == 4);

			bool bResult = false;

			i32 pixelCount = width * height;

			i32 floatBufStride = channelCount * sizeof(real);
			i32 floatBufSize = floatBufStride * pixelCount;
			real* readBackTextureData = (real*)malloc((u32)floatBufSize);

			i32 u8BufStride = channelCount * sizeof(u8);
			i32 u8BufSize = u8BufStride * pixelCount;
			u8* u8Data = (u8*)malloc((u32)u8BufSize);

			if (readBackTextureData && u8Data)
			{
				const char* getTexImageBlockName = "glGetTexImage";
				PROFILE_BEGIN(getTexImageBlockName);
				glBindTexture(GL_TEXTURE_2D, handle);
				glGetTexImage(GL_TEXTURE_2D, 0, channelCount == 3 ? GL_RGB : GL_RGBA, GL_FLOAT, (void*)readBackTextureData);
				PROFILE_END(getTexImageBlockName);
				Profiler::PrintBlockDuration(getTexImageBlockName);

				for (i32 i = 0; i < pixelCount*channelCount; i++)
				{
					u8Data[i] = (u8)(readBackTextureData[i] * 255.0f);
				}

				bResult = SaveImage(absoluteFilePath, format, width, height, channelCount, u8Data, bFlipVertically);
			}
			else
			{
				PrintError("Failed to allocate %d bytes to save out to texture at %s\n", floatBufSize, absoluteFilePath.c_str());
			}

			free(u8Data);
			free(readBackTextureData);

			return bResult;
		}

		void StartAsyncTextureSaveToFile(const std::string& absoluteFilePath, ImageFormat format, GLuint handle, i32 width,
			i32 height, i32 channelCount, bool bFlipVertically, AsynchronousTextureSave** asyncTextureSave, void* userData, void(*callback)(void*))
		{
			assert(channelCount == 3 || channelCount == 4);
			assert(asyncTextureSave != nullptr);
			assert(*asyncTextureSave == nullptr);

			i32 pixelCount = width * height;

			i32 floatBufStride = channelCount * sizeof(real);
			i32 floatBufSize = floatBufStride * pixelCount;
			real* readBackTextureData = (real*)malloc((u32)floatBufSize);

			i32 u8BufStride = channelCount * sizeof(u8);
			i32 u8BufSize = u8BufStride * pixelCount;
			u8* u8Data = (u8*)malloc((u32)u8BufSize);

			if (readBackTextureData && u8Data)
			{
				const char* getTexImageBlockName = "glGetTexImage";
				PROFILE_BEGIN(getTexImageBlockName);

				glBindTexture(GL_TEXTURE_2D, handle);
				// TODO: Move readback to async thread as well (takes >~40ms)
				glGetTexImage(GL_TEXTURE_2D, 0, channelCount == 3 ? GL_RGB : GL_RGBA, GL_FLOAT, (void*)readBackTextureData);

				PROFILE_END(getTexImageBlockName);
				Profiler::PrintBlockDuration(getTexImageBlockName);

				for (i32 i = 0; i < pixelCount*channelCount; i++)
				{
					u8Data[i] = (u8)(readBackTextureData[i] * 255.0f);
				}

				*asyncTextureSave = new AsynchronousTextureSave(absoluteFilePath, format, width, height, channelCount, bFlipVertically, u8Data, u8BufSize, userData, callback);
			}
			else
			{
				PrintError("Failed to allocate %d bytes to save out to texture at %s\n", floatBufSize, absoluteFilePath.c_str());
			}

			free(u8Data);
			free(readBackTextureData);
		}

		bool LoadGLShaders(u32 program, GLShader& shader)
		{
			bool bSuccess = true;

			bool bLoadFragmentShader = (!shader.shader->fragmentShaderFilePath.empty());
			bool bLoadGeometryShader = (!shader.shader->geometryShaderFilePath.empty());

			GLuint vertexShaderID = glCreateShader(GL_VERTEX_SHADER);

			GLuint fragmentShaderID = 0;
			if (bLoadFragmentShader)
			{
				fragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);
			}

			GLuint geometryShaderID = 0;
			if (bLoadGeometryShader)
			{
				geometryShaderID = glCreateShader(GL_GEOMETRY_SHADER);
			}

			if (g_bEnableLogging_Loading)
			{
				const std::string vertFileName = StripLeadingDirectories(shader.shader->vertexShaderFilePath);
				const std::string fragFileName = StripLeadingDirectories(shader.shader->fragmentShaderFilePath);
				const std::string geomFileName = StripLeadingDirectories(shader.shader->geometryShaderFilePath);

				Print("Loading shaders %s", vertFileName.c_str());

				if (!fragFileName.empty())
				{
					Print(" & %s", fragFileName.c_str());
				}

				if (!geomFileName.empty())
				{
					Print(" & %s", geomFileName.c_str());
				}

				Print("\n");
			}

			if (!ReadFile(shader.shader->vertexShaderFilePath, shader.shader->vertexShaderCode, false))
			{
				PrintError("Could not find vertex shader: %s\n", shader.shader->name.c_str());
			}
			shader.shader->vertexShaderCode.push_back('\0'); // Null terminator

			if (bLoadFragmentShader)
			{
				if (!ReadFile(shader.shader->fragmentShaderFilePath, shader.shader->fragmentShaderCode, false))
				{
					PrintError("Could not find fragment shader: %s\n", shader.shader->name.c_str());
				}
				shader.shader->fragmentShaderCode.push_back('\0'); // Null terminator
			}

			if (bLoadGeometryShader)
			{
				if (!ReadFile(shader.shader->geometryShaderFilePath, shader.shader->geometryShaderCode, false))
				{
					PrintError("Could not find geometry shader: %s\n", shader.shader->name.c_str());
				}
				shader.shader->geometryShaderCode.push_back('\0'); // Null terminator
			}

			GLint result = GL_FALSE;
			i32 infoLogLength;

			// Compile vertex shader
			char const* vertexSourcepointer = shader.shader->vertexShaderCode.data(); // TODO: Test
			glShaderSource(vertexShaderID, 1, &vertexSourcepointer, NULL);
			glCompileShader(vertexShaderID);

			glGetShaderiv(vertexShaderID, GL_COMPILE_STATUS, &result);
			if (result == GL_FALSE)
			{
				glGetShaderiv(vertexShaderID, GL_INFO_LOG_LENGTH, &infoLogLength);
				std::string vertexShaderErrorMessage;
				vertexShaderErrorMessage.resize((size_t)infoLogLength);
				glGetShaderInfoLog(vertexShaderID, infoLogLength, NULL, (GLchar*)vertexShaderErrorMessage.data());
				PrintError("%s\n", vertexShaderErrorMessage.c_str());
				bSuccess = false;
			}

			if (bLoadFragmentShader)
			{
				// Compile fragment shader
				char const* fragmentSourcePointer = shader.shader->fragmentShaderCode.data();
				glShaderSource(fragmentShaderID, 1, &fragmentSourcePointer, NULL);
				glCompileShader(fragmentShaderID);

				glGetShaderiv(fragmentShaderID, GL_COMPILE_STATUS, &result);
				if (result == GL_FALSE)
				{
					glGetShaderiv(fragmentShaderID, GL_INFO_LOG_LENGTH, &infoLogLength);
					std::string fragmentShaderErrorMessage;
					fragmentShaderErrorMessage.resize((size_t)infoLogLength);
					glGetShaderInfoLog(fragmentShaderID, infoLogLength, NULL, (GLchar*)fragmentShaderErrorMessage.data());
					PrintError("%s\n", fragmentShaderErrorMessage.c_str());
					bSuccess = false;
				}
			}

			if (bLoadGeometryShader)
			{
				// Compile geometry shader
				char const* geometrySourcePointer = shader.shader->geometryShaderCode.data();
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
					PrintError("%s\n", geometryShaderErrorMessage.c_str());
					bSuccess = false;
				}
			}

			glAttachShader(program, vertexShaderID);
			if (bLoadFragmentShader)
			{
				glAttachShader(program, fragmentShaderID);
			}
			if (bLoadGeometryShader)
			{
				glAttachShader(program, geometryShaderID);
			}

			return bSuccess;
		}

		bool LinkProgram(u32 program)
		{
			glLinkProgram(program);

			GLint result = GL_FALSE;
			glGetProgramiv(program, GL_LINK_STATUS, &result);
			if (result == GL_FALSE)
			{
				return false;
			}

			return true;
		}

		bool IsProgramValid(u32 program)
		{
			glValidateProgram(program);
			i32 params = -1;
			glGetProgramiv(program, GL_VALIDATE_STATUS, &params);
			if (params != GL_TRUE)
			{

				return false;
			}

			return true;
		}

		void PrintProgramInfoLog(u32 program)
		{
			i32 max_length = 2048;
			i32 actual_length = 0;
			char program_log[2048];
			glGetProgramInfoLog(program, max_length, &actual_length, program_log);
			Print("Program info log for GL index %u:\n%s", program, program_log);
		}

		void PrintShaderInfo(u32 program, const char* shaderName /* = nullptr */)
		{
			if (shaderName)
			{
				Print("------------------\nShader %s (program %i) info:\n", shaderName, program);
			}
			else
			{
				Print("------------------\nShader program %i info:\n", program);
			}
			i32 params = -1;
			glGetProgramiv(program, GL_LINK_STATUS, &params);
			Print("GL_LINK_STATUS = %i\n", params);

			glGetProgramiv(program, GL_ATTACHED_SHADERS, &params);
			Print("GL_ATTACHED_SHADERS = %i\n", params);

			glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &params);
			Print("GL_ACTIVE_ATTRIBUTES = %i\n", params);
			for (i32 i = 0; i < params; i++)
			{
				char name[64];
				i32 max_length = 64;
				i32 actual_length = 0;
				i32 size = 0;
				GLenum glType;
				glGetActiveAttrib(
					program,
					i,
					max_length,
					&actual_length,
					&size,
					&glType,
					name
				);
				DataType dataType = GLTypeToDataType(glType);

				if (size > 1)
				{
					for (i32 j = 0; j < size; j++)
					{
						char long_name[64];
						snprintf(long_name, 64, "%s[%i]", name, j);
						i32 location = glGetAttribLocation(program, long_name);
						Print("  %i) type: %s, name: \"%s\", location: %i\n",
							i, DataTypeStrings[(u32)dataType], long_name, location);
					}
				}
				else
				{
					i32 location = glGetAttribLocation(program, name);
					Print("  %i) type: %s, name: \"%s\", location: %i\n",
						i, DataTypeStrings[(u32)dataType], name, location);
				}
			}

			glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &params);
			Print("GL_ACTIVE_UNIFORMS = %i\n", params);
			for (i32 i = 0; i < params; i++)
			{
				char name[64];
				i32 max_length = 64;
				i32 actual_length = 0;
				i32 size = 0;
				GLenum glType;
				glGetActiveUniform(
					program,
					i,
					max_length,
					&actual_length,
					&size,
					&glType,
					name
				);
				DataType dataType = GLTypeToDataType(glType);

				if (size > 1)
				{
					for (i32 j = 0; j < size; j++)
					{
						char long_name[64];
						snprintf(long_name, 64, "%s[%i]", name, j);
						i32 location = glGetUniformLocation(program, long_name);
						Print("  %i) type: %s, name: \"%s\", location: %i\n",
							i, DataTypeStrings[(u32)dataType], long_name, location);
					}
				}
				else
				{
					i32 location = glGetUniformLocation(program, name);
					Print("  %i) type: %s, name: \"%s\", location: %i\n",
						i, DataTypeStrings[(u32)dataType], name, location);
				}
			}
		}

		GLShader::GLShader(Shader* shader) :
			shader(shader)
		{
		}

		GLboolean BoolToGLBoolean(bool value)
		{
			return (GLboolean)value;
		}

		GLuint BufferTargetToGLTarget(BufferTarget bufferTarget)
		{
			switch (bufferTarget)
			{
			case BufferTarget::ARRAY_BUFFER:			return GL_ARRAY_BUFFER;
			case BufferTarget::ELEMENT_ARRAY_BUFFER:	return GL_ELEMENT_ARRAY_BUFFER;
			default:
				PrintError("Unhandled BufferTarget passed to BufferTargetToGLTarget: %i\n", (i32)bufferTarget);
				return GL_INVALID_ENUM;
			}
		}

		GLenum DataTypeToGLType(DataType dataType)
		{
			switch (dataType)
			{
			case DataType::BYTE:				return GL_BYTE;
			case DataType::BOOL:				return GL_BOOL;
			case DataType::UNSIGNED_BYTE:		return GL_UNSIGNED_BYTE;
			case DataType::SHORT:				return GL_SHORT;
			case DataType::UNSIGNED_SHORT:		return GL_UNSIGNED_SHORT;
			case DataType::INT:					return GL_INT;
			case DataType::UNSIGNED_INT:		return GL_UNSIGNED_INT;
			case DataType::FLOAT:				return GL_FLOAT;
			case DataType::DOUBLE:				return GL_DOUBLE;
			case DataType::FLOAT_VEC2:			return GL_FLOAT_VEC2;
			case DataType::FLOAT_VEC3:			return GL_FLOAT_VEC3;
			case DataType::FLOAT_VEC4:			return GL_FLOAT_VEC4;
			case DataType::FLOAT_MAT3:			return GL_FLOAT_MAT3;
			case DataType::FLOAT_MAT4:			return GL_FLOAT_MAT4;
			case DataType::INT_VEC2:			return GL_INT_VEC2;
			case DataType::INT_VEC3:			return GL_INT_VEC3;
			case DataType::INT_VEC4:			return GL_INT_VEC4;
			case DataType::SAMPLER_1D:			return GL_SAMPLER_1D;
			case DataType::SAMPLER_2D:			return GL_SAMPLER_2D;
			case DataType::SAMPLER_3D:			return GL_SAMPLER_3D;
			case DataType::SAMPLER_1D_SHADOW:	return GL_SAMPLER_1D_SHADOW;
			case DataType::SAMPLER_2D_SHADOW:	return GL_SAMPLER_2D_SHADOW;
			case DataType::SAMPLER_CUBE:		return GL_SAMPLER_CUBE;
			case DataType::SAMPLER_CUBE_SHADOW:	return GL_SAMPLER_CUBE_SHADOW;
			default:
				PrintError("Unhandled DataType passed to DataTypeToGLType: %i\n", (i32)dataType);
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
				PrintError("Unhandled usage flag passed to UsageFlagToGLUsageFlag: %i\n", (i32)usage);
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
				PrintError("Unhandled topology mode passed to TopologyModeToGLMode: %i\n", (i32)topology);
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
			case CullFace::NONE:			return GL_NONE;
			default:
				PrintError("Unhandled cull face passed to CullFaceToGLCullFace: %i\n", (i32)cullFace);
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
				PrintError("Unhandled depth test func face passed to DepthTestFuncToGlenum: %i\n", (i32)func);
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
				PrintError("Unhandled GLTarget passed to GLTargetToBufferTarget: %i\n", (i32)target);
				return BufferTarget::_NONE;
			}
		}

		DataType GLTypeToDataType(GLenum type)
		{
			switch (type)
			{
			case GL_BOOL:					return DataType::BOOL;
			case GL_BYTE:					return DataType::BYTE;
			case GL_UNSIGNED_BYTE:			return DataType::UNSIGNED_BYTE;
			case GL_SHORT:					return DataType::SHORT;
			case GL_UNSIGNED_SHORT:			return DataType::UNSIGNED_SHORT;
			case GL_INT:					return DataType::INT;
			case GL_UNSIGNED_INT:			return DataType::UNSIGNED_INT;
			case GL_FLOAT:					return DataType::FLOAT;
			case GL_DOUBLE:					return DataType::DOUBLE;
			case GL_FLOAT_VEC2:				return DataType::FLOAT_VEC2;
			case GL_FLOAT_VEC3:				return DataType::FLOAT_VEC3;
			case GL_FLOAT_VEC4:				return DataType::FLOAT_VEC4;
			case GL_FLOAT_MAT3:				return DataType::FLOAT_MAT3;
			case GL_FLOAT_MAT4:				return DataType::FLOAT_MAT4;
			case GL_INT_VEC2:				return DataType::INT_VEC2;
			case GL_INT_VEC3:				return DataType::INT_VEC3;
			case GL_INT_VEC4:				return DataType::INT_VEC4;
			case GL_SAMPLER_1D:				return DataType::SAMPLER_1D;
			case GL_SAMPLER_2D:				return DataType::SAMPLER_2D;
			case GL_SAMPLER_3D:				return DataType::SAMPLER_3D;
			case GL_SAMPLER_CUBE:			return DataType::SAMPLER_CUBE;
			case GL_SAMPLER_1D_SHADOW:		return DataType::SAMPLER_1D_SHADOW;
			case GL_SAMPLER_2D_SHADOW:		return DataType::SAMPLER_2D_SHADOW;
			case GL_SAMPLER_CUBE_SHADOW:	return DataType::SAMPLER_CUBE_SHADOW;
			default:
			{
				PrintError("Unhandled GLType passed to GLTypeToDataType: %i\n", type);
				return DataType::_NONE;
			}
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
				PrintError("Unhandled GL usage flag passed to GLUsageFlagToUsageFlag: %i\n", usage);
				return UsageFlag::_NONE;
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
				PrintError("Unhandled GL mode passed to GLModeToTopologyMode: %i\n", mode);
				return TopologyMode::_NONE;
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
			else if (cullFace == GL_NONE)
			{
				return CullFace::NONE;
			}
			else
			{
				PrintError("Unhandled GL cull face passed to GLCullFaceToCullFace: %i\n", cullFace);
				return CullFace::_INVALID;
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
				PrintError("Unhandled GL enum passed to GlenumToDepthTestFunc: %i\n", depthTestFunc);
				return DepthTestFunc::_NONE;
			}
		}

		AsynchronousTextureSave::AsynchronousTextureSave(const std::string& absoluteFilePath, ImageFormat format, i32 width, i32 height,
			i32 channelCount, bool bFlipVertically, u8* srcData, i32 numBytes, void* userData, void(*callback)(void*)) :
			absoluteFilePath(absoluteFilePath),
			userData(userData),
			callback(callback)
		{
			data = (u8*)malloc((u32)numBytes);
			if (!data)
			{
				PrintError("Failed to allocate %d bytes for asynchronous texture save\n", numBytes);
				return;
			}
			memcpy_s(data, (u32)numBytes, srcData, (u32)numBytes);
			taskThread = std::thread([=]
			{
				bSuccess = SaveImage(absoluteFilePath, format, width, height, channelCount, data, bFlipVertically);
				taskPromise.set_value(true);
			});

			taskFuture = taskPromise.get_future();
			secSinceStatusCheck = 0.0f;
			totalSecWaiting = 0.0f;
		}

		AsynchronousTextureSave::~AsynchronousTextureSave()
		{
			if (data)
			{
				if (taskThread.joinable())
				{
					taskThread.join();
				}
				bComplete = true;
				free(data);
				data = nullptr;
			}
		}

		bool AsynchronousTextureSave::TickStatus()
		{
			secSinceStatusCheck += g_DeltaTime;
			totalSecWaiting += g_DeltaTime;
			if (secSinceStatusCheck > secBetweenStatusChecks)
			{
				secSinceStatusCheck -= secBetweenStatusChecks;
				auto status = taskFuture.wait_for(std::chrono::milliseconds(0));

				if (status == std::future_status::ready)
				{
					bComplete = true;
					taskThread.join();

					if (callback != nullptr)
					{
						callback(userData);
					}
				}
			}

			return bComplete;
		}

		void AsynchronousTextureSave::WaitToComplete()
		{
			if (bComplete)
			{
				return;
			}
			taskFuture.get();
			bComplete = true;
		}

	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL