#pragma once

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

	static size_t LoadShader(std::string filePath, ShaderType type);
	static size_t LoadShaders(std::string vertShaderFilePath, std::string fragShaderFilePath);

private:
	ShaderUtils(const ShaderUtils&) = delete;
	ShaderUtils& operator=(const ShaderUtils&) = delete;

};
