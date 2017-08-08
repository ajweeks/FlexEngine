#pragma once

#include "Logger.h"

#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <SOIL.h>

void _CheckGLErrorMessages(const char *file, int line);

#define CheckGLErrorMessages() _CheckGLErrorMessages(__FILE__,__LINE__)

GLFWimage LoadGLFWimage(const std::string filePath);
void DestroyGLFWimage(const GLFWimage& image);

void LoadGLTexture(const std::string filePath);

// Attempts to load image at "filename" into an OpenGL texture
// If repeats, texture wrapping will be set to repeat, otherwise to clamp
// returns the OpenGL texture handle, or 0 on fail
void LoadAndBindGLTexture(const std::string filePath, GLuint& textureHandle,
	int sWrap = GL_REPEAT, int tWrap = GL_REPEAT, int minFilter = GL_LINEAR, int magFilter = GL_LINEAR);

