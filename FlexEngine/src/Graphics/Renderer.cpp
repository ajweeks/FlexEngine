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
		if (HasUniform("camPos")) size += sizeof(glm::vec3);
		if (HasUniform("viewDir")) size += sizeof(glm::vec3);
		if (HasUniform("dirLight")) size += sizeof(DirectionalLight);
		if (HasUniform("pointLights")) size += sizeof(PointLight) * pointLightCount;
		if (HasUniform("useDiffuseSampler")) size += sizeof(glm::uint);
		if (HasUniform("useNormalSampler")) size += sizeof(glm::uint);
		if (HasUniform("useSpecularSampler")) size += sizeof(glm::uint);
		if (HasUniform("useCubemapSampler")) size += sizeof(glm::uint);
		if (HasUniform("useAlbedoSampler")) size += sizeof(glm::uint);
		if (HasUniform("material")) size += sizeof(Material);

		return size;
	}

	Renderer::Shader::Shader()
	{
	}

	Renderer::Shader::Shader(const std::string& vertexShaderFilePath, const std::string& fragmentShaderFilePath) :
		vertexShaderFilePath(vertexShaderFilePath),
		fragmentShaderFilePath(fragmentShaderFilePath)
	{
	}

} // namespace flex