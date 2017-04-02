#pragma once

#include "Logger.h"

#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <SOIL.h>

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

// Attempts to load image at "filename" into an OpenGL texture
// If repeats, texture wrapping will be set to repeat, otherwise to clamp
// returns the OpenGL texture handle, or 0 on fail
void LoadAndBindGLTexture(const std::string filename, GLuint& textureHandle,
	int sWrap = GL_REPEAT, int tWrap = GL_REPEAT, int minFilter = GL_LINEAR, int magFilter = GL_LINEAR)
{
	glGenTextures(1, &textureHandle);
	glBindTexture(GL_TEXTURE_2D, textureHandle);

	LoadGLTexture(filename);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sWrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tWrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
}
