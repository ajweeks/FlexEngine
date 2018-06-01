#include "stdafx.hpp"

#include "Graphics/RendererTypes.hpp"

#include "Logger.hpp"

namespace flex
{
	bool Uniforms::HasUniform(const std::string& name) const
	{
		return (types.find(name) != types.end());
	}

	void Uniforms::AddUniform(const std::string& name)
	{
		types.insert(std::pair<std::string, bool>(name, true));
	}

	void Uniforms::RemoveUniform(const std::string& name)
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

	u32 Uniforms::CalculateSize(i32 PointLightCount)
	{
		u32 size = 0;

		if (HasUniform("model")) size += sizeof(glm::mat4);
		if (HasUniform("modelInvTranspose")) size += sizeof(glm::mat4);
		if (HasUniform("modelViewProjection")) size += sizeof(glm::mat4);
		if (HasUniform("view")) size += sizeof(glm::mat4);
		if (HasUniform("viewInv")) size += sizeof(glm::mat4);
		if (HasUniform("viewProjection")) size += sizeof(glm::mat4);
		if (HasUniform("projection")) size += sizeof(glm::mat4);
		if (HasUniform("colorMultiplier")) size += sizeof(glm::vec4);
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
	
	Shader::Shader(const std::string& name,
				   const std::string& vertexShaderFilePath,
				   const std::string& fragmentShaderFilePath,
				   const std::string& geometryShaderFilePath) :
		name(name),
		vertexShaderFilePath(vertexShaderFilePath),
		fragmentShaderFilePath(fragmentShaderFilePath),
		geometryShaderFilePath(geometryShaderFilePath)
	{
	}

	bool Material::Equals(const Material& other, const GameContext& gameContext)
	{
		// TODO: FIXME: Pls don't do this :(
		// memcmp instead ???

		bool equal =
			(name == other.name &&
				shaderID == other.shaderID &&
				generateDiffuseSampler == other.generateDiffuseSampler &&
				enableDiffuseSampler == other.enableDiffuseSampler &&
				diffuseTexturePath == other.diffuseTexturePath &&
				generateNormalSampler == other.generateNormalSampler &&
				enableNormalSampler == other.enableNormalSampler &&
				normalTexturePath == other.normalTexturePath &&
				frameBuffers.size() == other.frameBuffers.size() &&
				generateCubemapSampler == other.generateCubemapSampler &&
				enableCubemapSampler == other.enableCubemapSampler &&
				cubemapSamplerSize == other.cubemapSamplerSize &&
				cubeMapFilePaths[0] == other.cubeMapFilePaths[0] &&
				constAlbedo == other.constAlbedo &&
				constMetallic == other.constMetallic &&
				constRoughness == other.constRoughness &&
				constAO == other.constAO &&
				generateAlbedoSampler == other.generateAlbedoSampler &&
				enableAlbedoSampler == other.enableAlbedoSampler &&
				albedoTexturePath == other.albedoTexturePath &&
				generateMetallicSampler == other.generateMetallicSampler &&
				enableMetallicSampler == other.enableMetallicSampler &&
				metallicTexturePath == other.metallicTexturePath &&
				generateRoughnessSampler == other.generateRoughnessSampler &&
				enableRoughnessSampler == other.enableRoughnessSampler &&
				roughnessTexturePath == other.roughnessTexturePath &&
				albedoTexturePath == other.albedoTexturePath &&
				generateAOSampler == other.generateAOSampler &&
				enableAOSampler == other.enableAOSampler &&
				aoTexturePath == other.aoTexturePath &&
				generateHDREquirectangularSampler == other.generateHDREquirectangularSampler &&
				enableHDREquirectangularSampler == other.enableHDREquirectangularSampler &&
				hdrEquirectangularTexturePath == other.hdrEquirectangularTexturePath &&
				enableCubemapTrilinearFiltering == other.enableCubemapTrilinearFiltering &&
				generateHDRCubemapSampler == other.generateHDRCubemapSampler &&
				enableIrradianceSampler == other.enableIrradianceSampler &&
				generateIrradianceSampler == other.generateIrradianceSampler &&
				irradianceSamplerSize == other.irradianceSamplerSize &&
				environmentMapPath == other.environmentMapPath &&
				enablePrefilteredMap == other.enablePrefilteredMap &&
				generatePrefilteredMap == other.generatePrefilteredMap &&
				prefilteredMapSize == other.prefilteredMapSize &&
				enableBRDFLUT == other.enableBRDFLUT &&
				renderToCubemap == other.renderToCubemap &&
				generateReflectionProbeMaps == other.generateReflectionProbeMaps &&
				colorMultiplier == other.colorMultiplier
				//pushConstantBlock.mvp == other.pushConstantBlock.mvp &&
				);

		return equal;
	}
} // namespace flex