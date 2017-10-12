#include "stdafx.hpp"

#include "Graphics/Renderer.hpp"

namespace flex
{
	Renderer::Renderer()
	{
	}

	Renderer::~Renderer()
	{
	}

	inline bool Renderer::Uniforms::HasUniform(const std::string& name) const
	{
		return (types.find(name) != types.end());
	}

	// TODO: Remove second variable in place of extra function, RemoveUniform which removes pair from map
	void Renderer::Uniforms::AddUniform(const std::string& name, bool value)
	{
		types.insert(std::pair<std::string, bool>(name, value));
	}

	glm::uint Renderer::Uniforms::CalculateSize(int pointLightCount)
	{
		glm::uint size = 0;

		if (HasUniform("model")) size += sizeof(glm::mat4);
		if (HasUniform("modelInvTranspose")) size += sizeof(glm::mat4);
		if (HasUniform("modelViewProjection")) size += sizeof(glm::mat4);
		if (HasUniform("view")) size += sizeof(glm::mat4);
		if (HasUniform("viewInv")) size += sizeof(glm::mat4);
		if (HasUniform("viewProjection")) size += sizeof(glm::mat4);
		if (HasUniform("projection")) size += sizeof(glm::mat4);
		if (HasUniform("camPos")) size += sizeof(glm::vec4);
		if (HasUniform("dirLight")) size += sizeof(DirectionalLight);
		if (HasUniform("pointLights")) size += sizeof(PointLight) * pointLightCount;
		if (HasUniform("enableAlbedoSampler")) size += sizeof(glm::uint);
		if (HasUniform("constAlbedo")) size += sizeof(glm::vec4);
		if (HasUniform("enableMetallicSampler")) size += sizeof(glm::uint);
		if (HasUniform("constMetallic")) size += sizeof(float);
		if (HasUniform("enableRoughnessSampler")) size += sizeof(glm::uint);
		if (HasUniform("constRoughness")) size += sizeof(float);
		if (HasUniform("enableAOSampler")) size += sizeof(glm::uint);
		if (HasUniform("constAO")) size += sizeof(float);
		if (HasUniform("enableNormalSampler")) size += sizeof(glm::uint);
		if (HasUniform("enableDiffuseSampler")) size += sizeof(glm::uint);
		if (HasUniform("enableSpecularSampler")) size += sizeof(glm::uint);
		if (HasUniform("enableCubemapSampler")) size += sizeof(glm::uint);
		if (HasUniform("enableIrradianceSampler")) size += sizeof(glm::uint);

		return size;
	}

	Renderer::Shader::Shader()
	{
	}

	Renderer::Shader::Shader(const std::string& name, const std::string& vertexShaderFilePath, const std::string& fragmentShaderFilePath) :
		name(name),
		vertexShaderFilePath(vertexShaderFilePath),
		fragmentShaderFilePath(fragmentShaderFilePath)
	{
	}

	bool Renderer::GetShaderID(const std::string & shaderName, ShaderID & shaderID)
	{
		// TODO: Store shaders using sorted data structure?
		for (size_t i = 0; i < m_Shaders.size(); ++i)
		{
			if (m_Shaders[i].name.compare(shaderName) == 0)
			{
				shaderID = i;
				return true;
			}
		}

		return false;
	}

} // namespace flex