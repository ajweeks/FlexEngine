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

	static bool LoadShaders(glm::uint program, 
		std::string vertexShaderFilePath, glm::uint& vertexShaderID,
		std::string fragmentShaderFilePath, glm::uint& fragmentShaderID);
	static void LinkProgram(glm::uint program);

private:
	ShaderUtils(const ShaderUtils&) = delete;
	ShaderUtils& operator=(const ShaderUtils&) = delete;

};
