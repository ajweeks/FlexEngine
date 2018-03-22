#include "stdafx.hpp"

#include "Graphics/Renderer.hpp"

#include "Logger.hpp"

namespace flex
{
	Renderer::Renderer()
	{
	}

	Renderer::~Renderer()
	{
	}

	bool Renderer::Uniforms::HasUniform(const std::string& name) const
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

	u32 Renderer::Uniforms::CalculateSize(i32 PointLightCount)
	{
		u32 size = 0;

		if (HasUniform("model")) size += sizeof(glm::mat4);
		if (HasUniform("modelInvTranspose")) size += sizeof(glm::mat4);
		if (HasUniform("modelViewProjection")) size += sizeof(glm::mat4);
		if (HasUniform("view")) size += sizeof(glm::mat4);
		if (HasUniform("viewInv")) size += sizeof(glm::mat4);
		if (HasUniform("viewProjection")) size += sizeof(glm::mat4);
		if (HasUniform("projection")) size += sizeof(glm::mat4);
		if (HasUniform("camPos")) size += sizeof(glm::vec4);
		if (HasUniform("dirLight")) size += sizeof(DirectionalLight);
		if (HasUniform("pointLights")) size += sizeof(PointLight) * PointLightCount;
		if (HasUniform("enableAlbedoSampler")) size += sizeof(u32);
		if (HasUniform("constAlbedo")) size += sizeof(glm::vec4);
		if (HasUniform("enableMetallicSampler")) size += sizeof(u32);
		if (HasUniform("constMetallic")) size += sizeof(real);
		if (HasUniform("enableRoughnessSampler")) size += sizeof(u32);
		if (HasUniform("constRoughness")) size += sizeof(real);
		if (HasUniform("roughness")) size += sizeof(real);
		if (HasUniform("enableAOSampler")) size += sizeof(u32);
		if (HasUniform("constAO")) size += sizeof(real);
		if (HasUniform("enableNormalSampler")) size += sizeof(u32);
		if (HasUniform("enableDiffuseSampler")) size += sizeof(u32);
		if (HasUniform("enableCubemapSampler")) size += sizeof(u32);
		if (HasUniform("enableIrradianceSampler")) size += sizeof(u32);

		return size;
	}

	void Renderer::SetReflectionProbeMaterial(MaterialID reflectionProbeMaterialID)
	{
		m_ReflectionProbeMaterialID = reflectionProbeMaterialID;
	}

	void Renderer::SetDrawPhysicsDebugObjects(bool drawPhysicsDebugObjects)
	{
		m_DrawPhysicsDebugObjects = drawPhysicsDebugObjects;
	}

	void Renderer::ToggleDrawPhysicsDebugObjects()
	{
		SetDrawPhysicsDebugObjects(!m_DrawPhysicsDebugObjects);
	}
	
	Renderer::Shader::Shader(const std::string& name, const std::string& vertexShaderFilePath, const std::string& fragmentShaderFilePath) :
		name(name),
		vertexShaderFilePath(vertexShaderFilePath),
		fragmentShaderFilePath(fragmentShaderFilePath)
	{
	}
} // namespace flex