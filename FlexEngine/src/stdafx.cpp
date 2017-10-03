#include "stdafx.h"

#include "Logger.h"

#if _DEBUG
void _CheckGLErrorMessages(const char *file, int line)
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

	flex::Logger::LogError("GL_" + errorName + " - " + fileName + ":" + std::to_string(line));
	errorType = glGetError();
}
}
#endif
