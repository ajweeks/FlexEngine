#include "stdafx.hpp"

#include "Graphics/RendererTypes.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"

namespace flex
{
	bool Uniforms::HasUniform(u64 uniform) const
	{
		return (uniforms & uniform) != 0;
	}

	void Uniforms::AddUniform(u64 uniform)
	{
		uniforms = (uniforms | uniform);
	}

	u32 Uniforms::CalculateSizeInBytes() const
	{
		u32 size = 0;

		if (HasUniform(U_MODEL)) size += US_MODEL;
		if (HasUniform(U_MODEL_INV_TRANSPOSE)) size += US_MODEL_INV_TRANSPOSE;
		if (HasUniform(U_VIEW)) size += US_VIEW;
		if (HasUniform(U_VIEW_INV)) size += US_VIEW_INV;
		if (HasUniform(U_VIEW_PROJECTION)) size += US_VIEW_PROJECTION;
		if (HasUniform(U_MODEL_VIEW_PROJ)) size += US_MODEL_VIEW_PROJ;
		if (HasUniform(U_PROJECTION)) size += US_PROJECTION;
		if (HasUniform(U_BLEND_SHARPNESS)) size += US_BLEND_SHARPNESS;
		if (HasUniform(U_COLOR_MULTIPLIER)) size += US_COLOR_MULTIPLIER;
		if (HasUniform(U_CAM_POS)) size += US_CAM_POS;
		if (HasUniform(U_DIR_LIGHT)) size += US_DIR_LIGHT;
		if (HasUniform(U_POINT_LIGHTS)) size += US_POINT_LIGHTS * Renderer::MAX_NUM_POINT_LIGHTS;
		if (HasUniform(U_CONST_ALBEDO)) size += US_CONST_ALBEDO;
		if (HasUniform(U_CONST_METALLIC)) size += US_CONST_METALLIC;
		if (HasUniform(U_CONST_ROUGHNESS)) size += US_CONST_ROUGHNESS;
		if (HasUniform(U_CONST_AO)) size += US_CONST_AO;
		if (HasUniform(U_ENABLE_CUBEMAP_SAMPLER)) size += US_ENABLE_CUBEMAP_SAMPLER;
		if (HasUniform(U_ENABLE_ALBEDO_SAMPLER)) size += US_ENABLE_ALBEDO_SAMPLER;
		if (HasUniform(U_ENABLE_METALLIC_SAMPLER)) size += US_ENABLE_METALLIC_SAMPLER;
		if (HasUniform(U_ENABLE_ROUGHNESS_SAMPLER)) size += US_ENABLE_ROUGHNESS_SAMPLER;
		if (HasUniform(U_ENABLE_AO_SAMPLER)) size += US_ENABLE_AO_SAMPLER;
		if (HasUniform(U_ENABLE_NORMAL_SAMPLER)) size += US_ENABLE_NORMAL_SAMPLER;
		if (HasUniform(U_ENABLE_IRRADIANCE_SAMPLER)) size += US_ENABLE_IRRADIANCE_SAMPLER;
		if (HasUniform(U_TEXEL_STEP)) size += US_TEXEL_STEP;
		if (HasUniform(U_SHOW_EDGES)) size += US_SHOW_EDGES;
		if (HasUniform(U_LIGHT_VIEW_PROJ)) size += US_LIGHT_VIEW_PROJ;
		if (HasUniform(U_EXPOSURE)) size += US_EXPOSURE;
		if (HasUniform(U_TRANSFORM_MAT)) size += US_TRANSFORM_MAT;
		if (HasUniform(U_TEX_SIZE)) size += US_TEX_SIZE;
		if (HasUniform(U_TEXTURE_SCALE)) size += US_TEXTURE_SCALE;
		if (HasUniform(U_TIME)) size += US_TIME;

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

	bool Material::Equals(const Material& other)
	{
		// TODO: FIXME: Pls don't do this :(
		// memcmp instead ???

		bool equal =
			(name == other.name &&
				shaderID == other.shaderID &&
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
				colorMultiplier == other.colorMultiplier &&
				textureScale == other.textureScale
				);

		return equal;
	}

	void Material::ParseJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut)
	{
		material.SetStringChecked("name", createInfoOut.name);
		material.SetStringChecked("shader", createInfoOut.shaderName);

		struct FilePathMaterialParam
		{
			std::string* path;
			std::string name;
		};

		std::vector<FilePathMaterialParam> filePathParams =
		{
			{ &createInfoOut.albedoTexturePath, "albedo texture filepath" },
			{ &createInfoOut.metallicTexturePath, "metallic texture filepath" },
			{ &createInfoOut.roughnessTexturePath, "roughness texture filepath" },
			{ &createInfoOut.aoTexturePath, "ao texture filepath" },
			{ &createInfoOut.normalTexturePath, "normal texture filepath" },
			{ &createInfoOut.hdrEquirectangularTexturePath, "hdr equirectangular texture filepath" },
			{ &createInfoOut.environmentMapPath, "environment map path" },
		};

		for (FilePathMaterialParam& param : filePathParams)
		{
			if (material.HasField(param.name))
			{
				*param.path = RESOURCE_LOCATION  "textures/" + material.GetString(param.name);
			}
		}

		material.SetBoolChecked("generate albedo sampler", createInfoOut.generateAlbedoSampler);
		material.SetBoolChecked("enable albedo sampler", createInfoOut.enableAlbedoSampler);
		material.SetBoolChecked("generate metallic sampler", createInfoOut.generateMetallicSampler);
		material.SetBoolChecked("enable metallic sampler", createInfoOut.enableMetallicSampler);
		material.SetBoolChecked("generate roughness sampler", createInfoOut.generateRoughnessSampler);
		material.SetBoolChecked("enable roughness sampler", createInfoOut.enableRoughnessSampler);
		material.SetBoolChecked("generate ao sampler", createInfoOut.generateAOSampler);
		material.SetBoolChecked("enable ao sampler", createInfoOut.enableAOSampler);
		material.SetBoolChecked("generate normal sampler", createInfoOut.generateNormalSampler);
		material.SetBoolChecked("enable normal sampler", createInfoOut.enableNormalSampler);
		material.SetBoolChecked("generate hdr equirectangular sampler", createInfoOut.generateHDREquirectangularSampler);
		material.SetBoolChecked("enable hdr equirectangular sampler", createInfoOut.enableHDREquirectangularSampler);
		material.SetBoolChecked("generate hdr cubemap sampler", createInfoOut.generateHDRCubemapSampler);
		material.SetBoolChecked("enable irradiance sampler", createInfoOut.enableIrradianceSampler);
		material.SetBoolChecked("generate irradiance sampler", createInfoOut.generateIrradianceSampler);
		material.SetBoolChecked("enable brdf lut", createInfoOut.enableBRDFLUT);
		material.SetBoolChecked("render to cubemap", createInfoOut.renderToCubemap);
		material.SetBoolChecked("enable cubemap sampler", createInfoOut.enableCubemapSampler);
		material.SetBoolChecked("enable cubemap trilinear filtering", createInfoOut.enableCubemapTrilinearFiltering);
		material.SetBoolChecked("generate cubemap sampler", createInfoOut.generateCubemapSampler);
		material.SetBoolChecked("generate cubemap depth buffers", createInfoOut.generateCubemapDepthBuffers);
		material.SetBoolChecked("generate prefiltered map", createInfoOut.generatePrefilteredMap);
		material.SetBoolChecked("enable prefiltered map", createInfoOut.enablePrefilteredMap);
		material.SetBoolChecked("generate reflection probe maps", createInfoOut.generateReflectionProbeMaps);

		material.SetVec2Checked("generated irradiance cubemap size", createInfoOut.generatedIrradianceCubemapSize);
		material.SetVec2Checked("generated prefiltered map size", createInfoOut.generatedPrefilteredCubemapSize);
		material.SetVec2Checked("generated cubemap size", createInfoOut.generatedCubemapSize);
		material.SetVec4Checked("color multiplier", createInfoOut.colorMultiplier);
		material.SetVec3Checked("const albedo", createInfoOut.constAlbedo);
		material.SetFloatChecked("const metallic", createInfoOut.constMetallic);
		material.SetFloatChecked("const roughness", createInfoOut.constRoughness);
		material.SetFloatChecked("const ao", createInfoOut.constAO);

		material.SetFloatChecked("texture scale", createInfoOut.textureScale);
	}

	JSONObject Material::Serialize() const
	{
		JSONObject materialObject = {};

		materialObject.fields.emplace_back("name", JSONValue(name));

		const Shader& shader = g_Renderer->GetShader(shaderID);
		materialObject.fields.emplace_back("shader", JSONValue(shader.name));

		// TODO: Find out way of determining if the following four  values
		// are used by the shader (only currently used by PBR I think)
		std::string constAlbedoStr = Vec3ToString(constAlbedo, 3);
		materialObject.fields.emplace_back("const albedo", JSONValue(constAlbedoStr));
		materialObject.fields.emplace_back("const metallic", JSONValue(constMetallic));
		materialObject.fields.emplace_back("const roughness", JSONValue(constRoughness));
		materialObject.fields.emplace_back("const ao", JSONValue(constAO));

		static const bool defaultEnableAlbedo = false;
		if (shader.bNeedAlbedoSampler && enableAlbedoSampler != defaultEnableAlbedo)
		{
			materialObject.fields.emplace_back("enable albedo sampler", JSONValue(enableAlbedoSampler));
		}

		static const bool defaultEnableMetallicSampler = false;
		if (shader.bNeedMetallicSampler && enableMetallicSampler != defaultEnableMetallicSampler)
		{
			materialObject.fields.emplace_back("enable metallic sampler", JSONValue(enableMetallicSampler));
		}

		static const bool defaultEnableRoughness = false;
		if (shader.bNeedRoughnessSampler && enableRoughnessSampler != defaultEnableRoughness)
		{
			materialObject.fields.emplace_back("enable roughness sampler", JSONValue(enableRoughnessSampler));
		}

		static const bool defaultEnableAO = false;
		if (shader.bNeedAOSampler && enableAOSampler != defaultEnableAO)
		{
			materialObject.fields.emplace_back("enable ao sampler", JSONValue(enableAOSampler));
		}

		static const bool defaultEnableNormal = false;
		if (shader.bNeedNormalSampler && enableNormalSampler != defaultEnableNormal)
		{
			materialObject.fields.emplace_back("enable normal sampler", JSONValue(enableNormalSampler));
		}

		static const bool defaultGenerateAlbedo = false;
		if (shader.bNeedAlbedoSampler && generateAlbedoSampler != defaultGenerateAlbedo)
		{
			materialObject.fields.emplace_back("generate albedo sampler", JSONValue(generateAlbedoSampler));
		}

		static const bool defaultGenerateMetallicSampler = false;
		if (shader.bNeedMetallicSampler && generateMetallicSampler != defaultGenerateMetallicSampler)
		{
			materialObject.fields.emplace_back("generate metallic sampler", JSONValue(generateMetallicSampler));
		}

		static const bool defaultGenerateRoughness = false;
		if (shader.bNeedRoughnessSampler && generateRoughnessSampler != defaultGenerateRoughness)
		{
			materialObject.fields.emplace_back("generate roughness sampler", JSONValue(generateRoughnessSampler));
		}

		static const bool defaultGenerateAO = false;
		if (shader.bNeedAOSampler && generateAOSampler != defaultGenerateAO)
		{
			materialObject.fields.emplace_back("generate ao sampler", JSONValue(generateAOSampler));
		}

		static const bool defaultGenerateNormal = false;
		if (shader.bNeedNormalSampler && generateNormalSampler != defaultGenerateNormal)
		{
			materialObject.fields.emplace_back("generate normal sampler", JSONValue(generateNormalSampler));
		}

		static const std::string texturePrefixStr = RESOURCE_LOCATION  "textures/";

		if (shader.bNeedAlbedoSampler && !albedoTexturePath.empty())
		{
			std::string shortAlbedoTexturePath = albedoTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("albedo texture filepath", JSONValue(shortAlbedoTexturePath));
		}

		if (shader.bNeedMetallicSampler && !metallicTexturePath.empty())
		{
			std::string shortMetallicTexturePath = metallicTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("metallic texture filepath", JSONValue(shortMetallicTexturePath));
		}

		if (shader.bNeedRoughnessSampler && !roughnessTexturePath.empty())
		{
			std::string shortRoughnessTexturePath = roughnessTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("roughness texture filepath", JSONValue(shortRoughnessTexturePath));
		}

		if (shader.bNeedAOSampler && !aoTexturePath.empty())
		{
			std::string shortAOTexturePath = aoTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("ao texture filepath", JSONValue(shortAOTexturePath));
		}

		if (shader.bNeedNormalSampler && !normalTexturePath.empty())
		{
			std::string shortNormalTexturePath = normalTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("normal texture filepath", JSONValue(shortNormalTexturePath));
		}

		if (generateHDRCubemapSampler)
		{
			materialObject.fields.emplace_back("generate hdr cubemap sampler", JSONValue(generateHDRCubemapSampler));
		}

		if (shader.bNeedCubemapSampler)
		{
			materialObject.fields.emplace_back("enable cubemap sampler", JSONValue(enableCubemapSampler));

			materialObject.fields.emplace_back("enable cubemap trilinear filtering", JSONValue(enableCubemapTrilinearFiltering));

			std::string cubemapSamplerSizeStr = Vec2ToString(cubemapSamplerSize, 0);
			materialObject.fields.emplace_back("generated cubemap size", JSONValue(cubemapSamplerSizeStr));
		}

		if (shader.bNeedIrradianceSampler || irradianceSamplerSize.x > 0)
		{
			materialObject.fields.emplace_back("generate irradiance sampler", JSONValue(generateIrradianceSampler));

			std::string irradianceSamplerSizeStr = Vec2ToString(irradianceSamplerSize, 0);
			materialObject.fields.emplace_back("generated irradiance cubemap size", JSONValue(irradianceSamplerSizeStr));
		}

		if (shader.bNeedPrefilteredMap || prefilteredMapSize.x > 0)
		{
			materialObject.fields.emplace_back("generate prefiltered map", JSONValue(generatePrefilteredMap));

			std::string prefilteredMapSizeStr = Vec2ToString(prefilteredMapSize, 0);
			materialObject.fields.emplace_back("generated prefiltered map size", JSONValue(prefilteredMapSizeStr));
		}

		if (!environmentMapPath.empty())
		{
			std::string cleanedEnvMapPath = environmentMapPath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("environment map path", JSONValue(cleanedEnvMapPath));
		}

		if (textureScale != 1.0f)
		{
			materialObject.fields.emplace_back("texture scale", JSONValue(textureScale));
		}

		return materialObject;
	}
} // namespace flex