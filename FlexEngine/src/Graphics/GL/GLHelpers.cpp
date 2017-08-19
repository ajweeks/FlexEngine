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
		case GL_INVALID_OPERATION:              errorName = "INVALID_OPERATION";                break;
		case GL_INVALID_ENUM:                   errorName = "INVALID_ENUM";                     break;
		case GL_INVALID_VALUE:                  errorName = "INVALID_VALUE";                    break;
		case GL_OUT_OF_MEMORY:                  errorName = "OUT_OF_MEMORY";                    break;
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

GLFWimage LoadGLFWimage(const std::string filePath)
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

void GenerateGLTexture(glm::uint VAO, glm::uint& textureID, const std::string filePath, int sWrap, int tWrap, int minFilter, int magFilter)
{
	glBindVertexArray(VAO);

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
