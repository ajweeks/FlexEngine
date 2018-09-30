#include "stdafx.hpp"

#include "Graphics/RendererTypes.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"

namespace flex
{
	static const char* DataTypeStrs[(i32)DataType::NONE + 1] = {
		"bool",
		"byte",
		"unsigned byte",
		"short",
		"unsigned short",
		"int",
		"unsigned int",
		"float",
		"double",
		"float vector 2",
		"float vector 3",
		"float vector 4",
		"float matrix 3",
		"float matrix 4",
		"int vector 2",
		"int vector 3",
		"int vector 4",
		"sampler 1D",
		"sampler 2D",
		"sampler 3D",
		"sampler cube",
		"sampler 1D shadow",
		"sampler 2D shadow",
		"sampler cube shadow",
		"NONE",
	};

	const char* DataTypeToString(DataType dataType)
	{
		if ((i32)dataType >= 0 && (i32)dataType <= (i32)DataType::NONE)
		{
			return DataTypeStrs[(i32)dataType];
		}
		return nullptr;
	}

	bool Uniforms::HasUniform(Uniform uniform) const
	{
		return ((u64)uniforms & (u64)uniform) != 0;
	}

	void Uniforms::AddUniform(Uniform uniform)
	{
		uniforms = (Uniform)((u64)uniforms | (u64)uniform);
	}

	//void Uniforms::RemoveUniform(Uniform uniform)
	//{
	//	uniforms = (Uniform)((u64)uniforms & ~(u64)uniform);
	//}

	u32 Uniforms::CalculateSize(i32 pointLightCount)
	{
		UNREFERENCED_PARAMETER(pointLightCount);

		// NOTE: This function is only needed for Vulkan buffer initialization calculations

		u32 size = 0;

		//if (HasUniform(Uniform::MODEL)) size += sizeof(glm::mat4);
		//if (HasUniform(Uniform::MODEL_INV_TRANSPOSE)) size += sizeof(glm::mat4);
		//if (HasUniform(Uniform::VIEW)) size += sizeof(glm::mat4);
		//if (HasUniform(Uniform::VIEW_INV)) size += sizeof(glm::mat4);
		//if (HasUniform(Uniform::VIEW_PROJECTION)) size += sizeof(glm::mat4);
		//if (HasUniform(Uniform::PROJECTION)) size += sizeof(glm::mat4);
		//if (HasUniform(Uniform::COLOR_MULTIPLIER)) size += sizeof(glm::vec4);
		//if (HasUniform(Uniform::CAM_POS)) size += sizeof(glm::vec4);
		//if (HasUniform(Uniform::DIR_LIGHT)) size += sizeof(DirectionalLight);
		//if (HasUniform(Uniform::POINT_LIGHTS)) size += sizeof(PointLight) * pointLightCount;
		//if (HasUniform(Uniform::ALBEDO_SAMPLER)) size += sizeof(u32);
		//if (HasUniform(Uniform::CONST_ALBEDO)) size += sizeof(glm::vec4);
		//if (HasUniform(Uniform::METALLIC_SAMPLER)) size += sizeof(u32);
		//if (HasUniform(Uniform::CONST_METALLIC)) size += sizeof(real);
		//if (HasUniform(Uniform::ROUGHNESS_SAMPLER)) size += sizeof(u32);
		//if (HasUniform(Uniform::CONST_ROUGHNESS)) size += sizeof(real);
		////if (HasUniform(Uniform::ROUGHNESS)) size += sizeof(real);
		//if (HasUniform(Uniform::AO_SAMPLER)) size += sizeof(u32);
		//if (HasUniform(Uniform::CONST_AO)) size += sizeof(real);
		//if (HasUniform(Uniform::NORMAL_SAMPLER)) size += sizeof(u32);
		//if (HasUniform(Uniform::ENABLE_CUBEMAP_SAMPLER)) size += sizeof(u32);
		//if (HasUniform(Uniform::IRRADIANCE_SAMPLER)) size += sizeof(u32);

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
				colorMultiplier == other.colorMultiplier
				//pushConstantBlock.mvp == other.pushConstantBlock.mvp &&
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
				*param.path = RESOURCE_LOCATION + "textures/" + material.GetString(param.name);
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
	}

	JSONObject Material::SerializeToJSON() const
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
		if (shader.needAlbedoSampler && enableAlbedoSampler != defaultEnableAlbedo)
		{
			materialObject.fields.emplace_back("enable albedo sampler", JSONValue(enableAlbedoSampler));
		}

		static const bool defaultEnableMetallicSampler = false;
		if (shader.needMetallicSampler && enableMetallicSampler != defaultEnableMetallicSampler)
		{
			materialObject.fields.emplace_back("enable metallic sampler", JSONValue(enableMetallicSampler));
		}

		static const bool defaultEnableRoughness = false;
		if (shader.needRoughnessSampler && enableRoughnessSampler != defaultEnableRoughness)
		{
			materialObject.fields.emplace_back("enable roughness sampler", JSONValue(enableRoughnessSampler));
		}

		static const bool defaultEnableAO = false;
		if (shader.needAOSampler && enableAOSampler != defaultEnableAO)
		{
			materialObject.fields.emplace_back("enable ao sampler", JSONValue(enableAOSampler));
		}

		static const bool defaultEnableNormal = false;
		if (shader.needNormalSampler && enableNormalSampler != defaultEnableNormal)
		{
			materialObject.fields.emplace_back("enable normal sampler", JSONValue(enableNormalSampler));
		}

		static const bool defaultGenerateAlbedo = false;
		if (shader.needAlbedoSampler && generateAlbedoSampler != defaultGenerateAlbedo)
		{
			materialObject.fields.emplace_back("generate albedo sampler", JSONValue(generateAlbedoSampler));
		}

		static const bool defaultGenerateMetallicSampler = false;
		if (shader.needMetallicSampler && generateMetallicSampler != defaultGenerateMetallicSampler)
		{
			materialObject.fields.emplace_back("generate metallic sampler", JSONValue(generateMetallicSampler));
		}

		static const bool defaultGenerateRoughness = false;
		if (shader.needRoughnessSampler && generateRoughnessSampler != defaultGenerateRoughness)
		{
			materialObject.fields.emplace_back("generate roughness sampler", JSONValue(generateRoughnessSampler));
		}

		static const bool defaultGenerateAO = false;
		if (shader.needAOSampler && generateAOSampler != defaultGenerateAO)
		{
			materialObject.fields.emplace_back("generate ao sampler", JSONValue(generateAOSampler));
		}

		static const bool defaultGenerateNormal = false;
		if (shader.needNormalSampler && generateNormalSampler != defaultGenerateNormal)
		{
			materialObject.fields.emplace_back("generate normal sampler", JSONValue(generateNormalSampler));
		}

		static const std::string texturePrefixStr = RESOURCE_LOCATION + "textures/";

		if (shader.needAlbedoSampler && !albedoTexturePath.empty())
		{
			std::string shortAlbedoTexturePath = albedoTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("albedo texture filepath", JSONValue(shortAlbedoTexturePath));
		}

		if (shader.needMetallicSampler && !metallicTexturePath.empty())
		{
			std::string shortMetallicTexturePath = metallicTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("metallic texture filepath", JSONValue(shortMetallicTexturePath));
		}

		if (shader.needRoughnessSampler && !roughnessTexturePath.empty())
		{
			std::string shortRoughnessTexturePath = roughnessTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("roughness texture filepath", JSONValue(shortRoughnessTexturePath));
		}

		if (shader.needAOSampler && !aoTexturePath.empty())
		{
			std::string shortAOTexturePath = aoTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("ao texture filepath", JSONValue(shortAOTexturePath));
		}

		if (shader.needNormalSampler && !normalTexturePath.empty())
		{
			std::string shortNormalTexturePath = normalTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("normal texture filepath", JSONValue(shortNormalTexturePath));
		}

		if (generateHDRCubemapSampler)
		{
			materialObject.fields.emplace_back("generate hdr cubemap sampler", JSONValue(generateHDRCubemapSampler));
		}

		if (shader.needCubemapSampler)
		{
			materialObject.fields.emplace_back("enable cubemap sampler", JSONValue(enableCubemapSampler));

			materialObject.fields.emplace_back("enable cubemap trilinear filtering", JSONValue(enableCubemapTrilinearFiltering));

			std::string cubemapSamplerSizeStr = Vec2ToString(cubemapSamplerSize, 0);
			materialObject.fields.emplace_back("generated cubemap size", JSONValue(cubemapSamplerSizeStr));
		}

		if (shader.needIrradianceSampler || irradianceSamplerSize.x > 0)
		{
			materialObject.fields.emplace_back("generate irradiance sampler", JSONValue(generateIrradianceSampler));

			std::string irradianceSamplerSizeStr = Vec2ToString(irradianceSamplerSize, 0);
			materialObject.fields.emplace_back("generated irradiance cubemap size", JSONValue(irradianceSamplerSizeStr));
		}

		if (shader.needPrefilteredMap || prefilteredMapSize.x > 0)
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

		return materialObject;
	}
} // namespace flex