#include "stdafx.h"

#include "Graphics/Renderer.h"

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
		if (HasUniform("useAlbedoSampler")) size += sizeof(glm::uint);
		if (HasUniform("constAlbedo")) size += sizeof(glm::vec4);
		if (HasUniform("useMetallicSampler")) size += sizeof(glm::uint);
		if (HasUniform("constMetallic")) size += sizeof(float);
		if (HasUniform("useRoughnessSampler")) size += sizeof(glm::uint);
		if (HasUniform("constRoughness")) size += sizeof(float);
		if (HasUniform("useAOSampler")) size += sizeof(glm::uint);
		if (HasUniform("constAO")) size += sizeof(float);
		if (HasUniform("useNormalSampler")) size += sizeof(glm::uint);
		if (HasUniform("useDiffuseSampler")) size += sizeof(glm::uint);
		if (HasUniform("useSpecularSampler")) size += sizeof(glm::uint);
		if (HasUniform("useCubemapSampler")) size += sizeof(glm::uint);

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

} // namespace flex