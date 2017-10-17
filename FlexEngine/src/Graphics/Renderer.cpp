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

	void Renderer::Uniforms::AddUniform(const std::string& name)
	{
		types.insert(std::pair<std::string, bool>(name, true));
	}

	void Renderer::Uniforms::RemoveUniform(const std::string& name)
	{
		auto location = types.find(name);
		if (location == types.end())
		{
			Logger::LogWarning("Attempted to remove uniform that doesn't exist! " + name);
		}
		else
		{
			types.erase(location);
		}
	}

	glm::uint Renderer::Uniforms::CalculateSize(int pointLightCount, size_t pushConstantBlockSize)
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
		if (HasUniform("roughness")) size += sizeof(float);
		if (HasUniform("enableAOSampler")) size += sizeof(glm::uint);
		if (HasUniform("constAO")) size += sizeof(float);
		if (HasUniform("enableNormalSampler")) size += sizeof(glm::uint);
		if (HasUniform("enableDiffuseSampler")) size += sizeof(glm::uint);
		if (HasUniform("enableSpecularSampler")) size += sizeof(glm::uint);
		if (HasUniform("enableCubemapSampler")) size += sizeof(glm::uint);
		if (HasUniform("enableIrradianceSampler")) size += sizeof(glm::uint);
		// This isn't needed, right?
		//if (HasUniform("needPushConstantBlock")) size += pushConstantBlockSize;

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