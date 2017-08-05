#include "stdafx.h"

#include "ShaderUtils.h"
#include "Graphics/GL/GLHelpers.h"

#include <vector>
#include <fstream>
#include <iostream>

#include <glad\glad.h>

ShaderUtils::ShaderUtils()
{
}


ShaderUtils::~ShaderUtils()
{
}

GLuint ShaderUtils::LoadShader(std::string filePath, ShaderType type)
{
	// TODO: Load one shader and use default for other
	return (GLuint)0;
}

GLuint ShaderUtils::LoadShaders(std::string vertShaderFilePath, std::string fragShaderFilePath)
{
	// Create the shaders
	CheckGLErrorMessages();
	CheckGLErrorMessages();
	CheckGLErrorMessages();
	CheckGLErrorMessages();
	GLuint vertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	CheckGLErrorMessages();

	GLuint fragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);
	CheckGLErrorMessages();

	std::string vertexShaderCode;
	std::ifstream vertexShaderStream(vertShaderFilePath, std::ios::in);
	if (vertexShaderStream.is_open())
	{
		std::string line;
		while (getline(vertexShaderStream, line))
		{
			vertexShaderCode += "\n" + line;
		}
		vertexShaderStream.close();
	}
	else
	{
		Logger::LogError("Could not open " + fragShaderFilePath);
		return 0;
	}

	std::string fragmentShaderCode;
	std::ifstream fragmentShaderStream(fragShaderFilePath, std::ios::in);
	if (fragmentShaderStream.is_open())
	{
		std::string line;
		while (getline(fragmentShaderStream, line))
		{
			fragmentShaderCode += "\n" + line;
		}
		fragmentShaderStream.close();
	}
	else
	{
		Logger::LogError("Could not open " + fragShaderFilePath);
		return 0;
	}

	GLint result = GL_FALSE;
	int infoLogLength;

	// Compile vertex shader
	Logger::LogInfo("Compiling shader: " + vertShaderFilePath);
	char const* vertexSourcePointer = vertexShaderCode.c_str();
	glShaderSource(vertexShaderID, 1, &vertexSourcePointer, NULL);
	glCompileShader(vertexShaderID);

	glGetShaderiv(vertexShaderID, GL_COMPILE_STATUS, &result);
	glGetShaderiv(vertexShaderID, GL_INFO_LOG_LENGTH, &infoLogLength);
	if (infoLogLength > 1)
	{
		std::string vertexShaderErrorMessage;
		vertexShaderErrorMessage.resize(infoLogLength);
		glGetShaderInfoLog(vertexShaderID, infoLogLength, NULL, (GLchar*)vertexShaderErrorMessage.data());
		Logger::LogError(vertexShaderErrorMessage);
	}


	// Compile Fragment Shader
	Logger::LogInfo("Compiling shader: " + fragShaderFilePath);
	char const* fragmentSourcePointer = fragmentShaderCode.c_str();
	glShaderSource(fragmentShaderID, 1, &fragmentSourcePointer, NULL);
	glCompileShader(fragmentShaderID);

	glGetShaderiv(fragmentShaderID, GL_COMPILE_STATUS, &result);
	glGetShaderiv(fragmentShaderID, GL_INFO_LOG_LENGTH, &infoLogLength);
	if (infoLogLength > 1)
	{
		std::string fragmentShaderErrorMessage;
		fragmentShaderErrorMessage.resize(infoLogLength);
		glGetShaderInfoLog(fragmentShaderID, infoLogLength, NULL,(GLchar*)fragmentShaderErrorMessage.data());
		Logger::LogError(fragmentShaderErrorMessage);
	}


	// Link the program
	GLuint programID = glCreateProgram();
	glAttachShader(programID, vertexShaderID);
	glAttachShader(programID, fragmentShaderID);
	glLinkProgram(programID);

	// Check the program
	glGetProgramiv(programID, GL_LINK_STATUS, &result);
	glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &infoLogLength);
	if (infoLogLength > 1)
	{
		std::string programErrorMessage;
		programErrorMessage.resize(infoLogLength);
		glGetProgramInfoLog(programID, infoLogLength, NULL, (GLchar*)programErrorMessage.data());
		Logger::LogError(programErrorMessage);
	}

	glDetachShader(programID, vertexShaderID);
	glDetachShader(programID, fragmentShaderID);

	glDeleteShader(vertexShaderID);
	glDeleteShader(fragmentShaderID);

	return programID;
}
