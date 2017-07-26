#include "stdafx.h"

#include "Graphics/GL/GLHelpers.h"

void _CheckGLErrorMessages(const char* file, int line)
{
	GLenum errorType(glGetError());

	while (errorType != GL_NO_ERROR)
	{
		std::string errorName;

		switch (errorType)
		{
		case GL_INVALID_OPERATION:                errorName = "INVALID_OPERATION";                break;
		case GL_INVALID_ENUM:                    errorName = "INVALID_ENUM";                        break;
		case GL_INVALID_VALUE:                    errorName = "INVALID_VALUE";                    break;
		case GL_OUT_OF_MEMORY:                    errorName = "OUT_OF_MEMORY";                    break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:  errorName = "INVALID_FRAMEBUFFER_OPERATION";    break;
		}

		// Remove all directories from string
		std::string fileName(file);
		size_t lastBS = fileName.rfind("\\");
		if (lastBS == std::string::npos)
		{
			lastBS = fileName.rfind("/");
		}

		if (lastBS != std::string::npos)
		{
			fileName = fileName.substr(lastBS + 1);
		}

		Logger::LogError("GL_" + errorName + " - " + fileName + ":" + std::to_string(line));
		errorType = glGetError();
	}
}

GLFWimage LoadGLFWImage(const std::string filename)
{
	GLFWimage result = {};

	int channels;
	unsigned char* data = SOIL_load_image(filename.c_str(), &result.width, &result.height, &channels, SOIL_LOAD_AUTO);

	if (data == 0)
	{
		Logger::LogError("SOIL loading error: " + std::string(SOIL_last_result()) + "\nimage filepath: " + filename);
		return result;
	}
	else
	{
		result.pixels = static_cast<unsigned char*>(data);
	}

	return result;
}

void LoadGLTexture(const std::string filename)
{
	int width, height;
	unsigned char* imageData = SOIL_load_image(filename.c_str(), &width, &height, 0, SOIL_LOAD_RGB);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, imageData);
	SOIL_free_image_data(imageData);
}

void LoadAndBindGLTexture(const std::string filename, GLuint& textureHandle, int sWrap, int tWrap, int minFilter, int magFilter)
{
	glGenTextures(1, &textureHandle);
	glBindTexture(GL_TEXTURE_2D, textureHandle);

	LoadGLTexture(filename);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sWrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tWrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
}
