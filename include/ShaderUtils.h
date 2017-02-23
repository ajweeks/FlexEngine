#pragma once

#include <glad\glad.h>

#include <string>

class ShaderUtils final
{
public:
	ShaderUtils();
	~ShaderUtils();

	enum class ShaderType
	{
		FRAGMENT, VERTEX
	};

	static GLuint LoadShader(std::string filepath, ShaderType type);
	static GLuint LoadShaders(std::string vertex, std::string fragment);

private:
	ShaderUtils(const ShaderUtils&) = delete;
	ShaderUtils& operator=(const ShaderUtils&) = delete;

};
