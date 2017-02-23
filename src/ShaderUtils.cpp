
#include "ShaderUtils.h"

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

GLuint ShaderUtils::LoadShader(std::string filepath, ShaderType type)
{
	// TODO: Load one shader and use default for other
	return (GLuint)0;
}

GLuint ShaderUtils::LoadShaders(std::string vertex, std::string fragment)
{
	// Create the shaders
	GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

	// Read the Vertex Shader code from the file
	std::string VertexShaderCode;
	std::ifstream VertexShaderStream(vertex, std::ios::in);
	if (VertexShaderStream.is_open())
	{
		std::string line;
		while (getline(VertexShaderStream, line))
		{
			VertexShaderCode += "\n" + line;
		}
		VertexShaderStream.close();
	}
	else
	{
		std::cout << "Could not open " << vertex << std::endl;
		return 0;
	}

	// Read the Fragment Shader code from the file
	std::string FragmentShaderCode;
	std::ifstream FragmentShaderStream(fragment, std::ios::in);
	if (FragmentShaderStream.is_open())
	{
		std::string line;
		while (getline(FragmentShaderStream, line))
		{
			FragmentShaderCode += "\n" + line;
		}
		FragmentShaderStream.close();
	}
	else
	{
		std::cout << "Could not open " << fragment << std::endl;
		return 0;
	}

	GLint Result = GL_FALSE;
	int InfoLogLength;

	// Compile Vertex Shader
	std::cout << "Compiling shader: " << vertex << std::endl;
	char const * VertexSourcePointer = VertexShaderCode.c_str();
	glShaderSource(VertexShaderID, 1, &VertexSourcePointer, NULL);
	glCompileShader(VertexShaderID);

	// Check Vertex Shader
	glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0)
	{
		std::string vertexShaderErrorMessage;
		vertexShaderErrorMessage.resize(InfoLogLength);
		glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, (GLchar*)vertexShaderErrorMessage.data());
		std::cout << vertexShaderErrorMessage << std::endl;
	}


	// Compile Fragment Shader
	std::cout << "Compiling shader: " << fragment << std::endl;
	char const * FragmentSourcePointer = FragmentShaderCode.c_str();
	glShaderSource(FragmentShaderID, 1, &FragmentSourcePointer, NULL);
	glCompileShader(FragmentShaderID);

	// Check Fragment Shader
	glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0)
	{
		std::string fragmentShaderErrorMessage;
		fragmentShaderErrorMessage.resize(InfoLogLength);
		glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL,(GLchar*)fragmentShaderErrorMessage.data());
		std::cout << fragmentShaderErrorMessage << std::endl;
	}


	// Link the program
	std::cout << "Linking program" << std::endl;
	GLuint programID = glCreateProgram();
	glAttachShader(programID, VertexShaderID);
	glAttachShader(programID, FragmentShaderID);
	glLinkProgram(programID);

	// Check the program
	glGetProgramiv(programID, GL_LINK_STATUS, &Result);
	glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0)
	{
		std::string programErrorMessage;
		programErrorMessage.resize(InfoLogLength);
		glGetProgramInfoLog(programID, InfoLogLength, NULL, (GLchar*)programErrorMessage.data());
		std::cout << programErrorMessage << std::endl;
	}

	glDetachShader(programID, VertexShaderID);
	glDetachShader(programID, FragmentShaderID);

	glDeleteShader(VertexShaderID);
	glDeleteShader(FragmentShaderID);

	return programID;
}
