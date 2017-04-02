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

	static size_t LoadShader(std::string filepath, ShaderType type);
	static size_t LoadShaders(std::string vertex, std::string fragment);

private:
	ShaderUtils(const ShaderUtils&) = delete;
	ShaderUtils& operator=(const ShaderUtils&) = delete;

};
