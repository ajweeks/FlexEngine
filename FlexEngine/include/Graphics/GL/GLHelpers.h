#pragma once

#include <string>
#include <array>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <SOIL.h>

#include "Logger.h"

namespace flex
{
	void _CheckGLErrorMessages(const char *file, int line);

#if _DEBUG
#define CheckGLErrorMessages() _CheckGLErrorMessages(__FILE__,__LINE__)
#else
#define CheckGLErrorMessages() 
#endif
	GLFWimage LoadGLFWimage(const std::string& filePath);
	void DestroyGLFWimage(const GLFWimage& image);

	void GenerateGLTexture(glm::uint VAO, glm::uint& textureID, const std::string& filePath,
		int sWrap = GL_REPEAT, int tWrap = GL_REPEAT, int minFilter = GL_LINEAR, int magFilter = GL_LINEAR);

	void GenerateCubemapTextures(glm::uint& textureID, const std::array<std::string, 6> filePaths);
} // namespace flex