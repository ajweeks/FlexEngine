#include "stdafx.hpp"

#include "Graphics/RendererTypes.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"

namespace flex
{
	CullFace StringToCullFace(const std::string& str)
	{
		std::string strLower(str);
		ToLower(strLower);

		if (strLower.compare("back") == 0)
		{
			return CullFace::BACK;
		}
		else if (strLower.compare("front") == 0)
		{
			return CullFace::FRONT;
		}
		else if (strLower.compare("front and back") == 0)
		{
			return CullFace::FRONT_AND_BACK;
		}
		else if (strLower.compare("none") == 0)
		{
			return CullFace::NONE;
		}
		else
		{
			PrintError("Unhandled cull face str: %s\n", str.c_str());
			return CullFace::_INVALID;
		}
	}

	std::string CullFaceToString(CullFace cullFace)
	{
		switch (cullFace)
		{
		case CullFace::BACK:			return "back";
		case CullFace::FRONT:			return "front";
		case CullFace::FRONT_AND_BACK:	return "front and back";
		case CullFace::NONE:			return "none";
		default:						return "UNHANDLED CULL FACE";
		}
	}

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

		{
#define _u(uniform) if (HasUniform(U_##uniform)) size += US_##uniform;
			_u(MODEL)
				_u(VIEW)
				_u(VIEW_INV)
				_u(VIEW_PROJECTION)
				_u(PROJECTION)
				_u(PROJECTION_INV)
				_u(BLEND_SHARPNESS)
				_u(COLOUR_MULTIPLIER)
				_u(CAM_POS)
				_u(DIR_LIGHT)
				_u(POINT_LIGHTS)
				_u(CONST_ALBEDO)
				_u(CONST_METALLIC)
				_u(CONST_ROUGHNESS)
				_u(ENABLE_ALBEDO_SAMPLER)
				_u(ENABLE_METALLIC_SAMPLER)
				_u(ENABLE_ROUGHNESS_SAMPLER)
				_u(ENABLE_NORMAL_SAMPLER)
				_u(ENABLE_IRRADIANCE_SAMPLER)
				_u(SHOW_EDGES)
				_u(LIGHT_VIEW_PROJS)
				_u(EXPOSURE)
				_u(TEX_SIZE)
				_u(TEXTURE_SCALE)
				_u(TIME)
				_u(SDF_DATA)
				_u(TEX_CHANNEL)
				_u(FONT_CHAR_DATA)
				_u(SSAO_GEN_DATA)
				_u(SSAO_BLUR_DATA_DYNAMIC)
				_u(SSAO_BLUR_DATA_CONSTANT)
				_u(SSAO_SAMPLING_DATA)
				_u(FXAA_DATA)
				_u(SHADOW_SAMPLING_DATA)
				_u(NEAR_FAR_PLANES)
				_u(POST_PROCESS_MAT)
				_u(LAST_FRAME_VIEWPROJ)
				_u(PARTICLE_BUFFER)
				_u(PARTICLE_SIM_DATA)
				_u(OCEAN_DATA)
				_u(SKYBOX_DATA)
#undef _u
		}

		return size;
	}

	Shader::Shader(const ShaderInfo& shaderInfo) :
		Shader(shaderInfo.name,
			shaderInfo.inVertexShaderFilePath,
			shaderInfo.inFragmentShaderFilePath,
			shaderInfo.inGeometryShaderFilePath,
			shaderInfo.inComputeShaderFilePath)
	{
	}

	Shader::Shader(const std::string& name,
		const std::string& inVertexShaderFilePath,
		const std::string& inFragmentShaderFilePath /* = "" */,
		const std::string& inGeometryShaderFilePath /* = "" */,
		const std::string& inComputeShaderFilePath /* = "" */) :
		name(name)
	{
#if COMPILE_OPEN_GL
		vertexShaderFilePath = SHADER_SOURCE_DIRECTORY + inVertexShaderFilePath;
		if (!inFragmentShaderFilePath.empty())
		{
			fragmentShaderFilePath = SHADER_SOURCE_DIRECTORY + inFragmentShaderFilePath;
		}
		if (!inGeometryShaderFilePath.empty())
		{
			geometryShaderFilePath = SHADER_SOURCE_DIRECTORY + inGeometryShaderFilePath;
		}
#elif COMPILE_VULKAN
		if (!inVertexShaderFilePath.empty())
		{
			vertexShaderFilePath = SPV_DIRECTORY + inVertexShaderFilePath;
		}
		if (!inFragmentShaderFilePath.empty())
		{
			fragmentShaderFilePath = SPV_DIRECTORY + inFragmentShaderFilePath;
		}
		if (!inGeometryShaderFilePath.empty())
		{
			geometryShaderFilePath = SPV_DIRECTORY + inGeometryShaderFilePath;
		}
		if (!inComputeShaderFilePath.empty())
		{
			computeShaderFilePath = SPV_DIRECTORY + inComputeShaderFilePath;
		}
#endif
	}

	bool Material::Equals(const Material& other)
	{
		// TODO: FIXME: Pls don't do this :(
		// memcmp instead ???

		bool equal =
			(name == other.name &&
				shaderID == other.shaderID &&
				enableNormalSampler == other.enableNormalSampler &&
				normalTexturePath == other.normalTexturePath &&
				sampledFrameBuffers.size() == other.sampledFrameBuffers.size() &&
				generateCubemapSampler == other.generateCubemapSampler &&
				enableCubemapSampler == other.enableCubemapSampler &&
				cubemapSamplerSize == other.cubemapSamplerSize &&
				cubeMapFilePaths[0] == other.cubeMapFilePaths[0] &&
				constAlbedo == other.constAlbedo &&
				constMetallic == other.constMetallic &&
				constRoughness == other.constRoughness &&
				enableAlbedoSampler == other.enableAlbedoSampler &&
				albedoTexturePath == other.albedoTexturePath &&
				enableMetallicSampler == other.enableMetallicSampler &&
				metallicTexturePath == other.metallicTexturePath &&
				enableRoughnessSampler == other.enableRoughnessSampler &&
				roughnessTexturePath == other.roughnessTexturePath &&
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
				colourMultiplier == other.colourMultiplier &&
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
			{ &createInfoOut.normalTexturePath, "normal texture filepath" },
			{ &createInfoOut.hdrEquirectangularTexturePath, "hdr equirectangular texture filepath" },
			{ &createInfoOut.environmentMapPath, "environment map path" },
		};

		for (FilePathMaterialParam& param : filePathParams)
		{
			if (material.HasField(param.name))
			{
				*param.path = TEXTURE_DIRECTORY + material.GetString(param.name);
			}
		}

		material.SetBoolChecked("enable albedo sampler", createInfoOut.enableAlbedoSampler);
		material.SetBoolChecked("enable metallic sampler", createInfoOut.enableMetallicSampler);
		material.SetBoolChecked("enable roughness sampler", createInfoOut.enableRoughnessSampler);
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
		material.SetVec4Checked("colour multiplier", createInfoOut.colourMultiplier);
		material.SetVec3Checked("const albedo", createInfoOut.constAlbedo);
		material.SetFloatChecked("const metallic", createInfoOut.constMetallic);
		material.SetFloatChecked("const roughness", createInfoOut.constRoughness);

		material.SetFloatChecked("texture scale", createInfoOut.textureScale);
	}

	std::vector<MaterialID> Material::ParseMaterialArrayJSON(const JSONObject& object, i32 fileVersion)
	{
		std::vector<MaterialID> matIDs;
		if (fileVersion >= 3)
		{
			std::vector<JSONField> materialNames;
			if (object.SetFieldArrayChecked("materials", materialNames))
			{
				for (const JSONField& materialNameField : materialNames)
				{
					std::string materialName = materialNameField.label;
					bool bSuccess = false;
					if (!materialName.empty())
					{
						MaterialID materialID = g_Renderer->GetMaterialID(materialName);

						if (materialID == InvalidMaterialID)
						{
							if (materialName.compare("placeholder") == 0)
							{
								materialID = g_Renderer->GetPlaceholderMaterialID();
							}
						}

						if (materialID != InvalidMaterialID)
						{
							bSuccess = true;
						}

						matIDs.push_back(materialID);
					}

					if (!bSuccess)
					{
						PrintError("Invalid material name for object %s: %s\n", object.GetString("name").c_str(), materialName.c_str());
					}
				}
			}
		}
		else // fileVersion < 3
		{
			MaterialID materialID = InvalidMaterialID;
			std::string materialName;
			if (object.SetStringChecked("material", materialName))
			{
				if (!materialName.empty())
				{
					materialID = g_Renderer->GetMaterialID(materialName);
					if (materialID == InvalidMaterialID)
					{
						if (materialName.compare("placeholder") == 0)
						{
							materialID = g_Renderer->GetPlaceholderMaterialID();
						}
					}
				}

				if (materialID == InvalidMaterialID)
				{
					PrintError("Invalid material name for object %s: %s\n", object.GetString("name").c_str(), materialName.c_str());
				}
			}

			matIDs.push_back(materialID);
		}

		return matIDs;
	}

	JSONObject Material::Serialize() const
	{
		JSONObject materialObject = {};

		materialObject.fields.emplace_back("name", JSONValue(name));

		const Shader* shader = g_Renderer->GetShader(shaderID);
		materialObject.fields.emplace_back("shader", JSONValue(shader->name));

		// TODO: Find out way of determining if the following four  values
		// are used by the shader (only currently used by PBR I think)
		std::string constAlbedoStr = VecToString(constAlbedo, 3);
		materialObject.fields.emplace_back("const albedo", JSONValue(constAlbedoStr));
		materialObject.fields.emplace_back("const metallic", JSONValue(constMetallic));
		materialObject.fields.emplace_back("const roughness", JSONValue(constRoughness));

		static const bool defaultEnableAlbedo = false;
		if (shader->bNeedAlbedoSampler && enableAlbedoSampler != defaultEnableAlbedo)
		{
			materialObject.fields.emplace_back("enable albedo sampler", JSONValue(enableAlbedoSampler));
		}

		static const bool defaultEnableMetallicSampler = false;
		if (shader->bNeedMetallicSampler && enableMetallicSampler != defaultEnableMetallicSampler)
		{
			materialObject.fields.emplace_back("enable metallic sampler", JSONValue(enableMetallicSampler));
		}

		static const bool defaultEnableRoughness = false;
		if (shader->bNeedRoughnessSampler && enableRoughnessSampler != defaultEnableRoughness)
		{
			materialObject.fields.emplace_back("enable roughness sampler", JSONValue(enableRoughnessSampler));
		}

		static const bool defaultEnableNormal = false;
		if (shader->bNeedNormalSampler && enableNormalSampler != defaultEnableNormal)
		{
			materialObject.fields.emplace_back("enable normal sampler", JSONValue(enableNormalSampler));
		}

		static const std::string texturePrefixStr = TEXTURE_DIRECTORY;

		if (shader->bNeedAlbedoSampler && !albedoTexturePath.empty())
		{
			std::string shortAlbedoTexturePath = albedoTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("albedo texture filepath", JSONValue(shortAlbedoTexturePath));
		}

		if (shader->bNeedMetallicSampler && !metallicTexturePath.empty())
		{
			std::string shortMetallicTexturePath = metallicTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("metallic texture filepath", JSONValue(shortMetallicTexturePath));
		}

		if (shader->bNeedRoughnessSampler && !roughnessTexturePath.empty())
		{
			std::string shortRoughnessTexturePath = roughnessTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("roughness texture filepath", JSONValue(shortRoughnessTexturePath));
		}

		if (shader->bNeedNormalSampler && !normalTexturePath.empty())
		{
			std::string shortNormalTexturePath = normalTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("normal texture filepath", JSONValue(shortNormalTexturePath));
		}

		if (generateHDRCubemapSampler)
		{
			materialObject.fields.emplace_back("generate hdr cubemap sampler", JSONValue(generateHDRCubemapSampler));
		}

		if (shader->bNeedCubemapSampler)
		{
			materialObject.fields.emplace_back("enable cubemap sampler", JSONValue(enableCubemapSampler));

			materialObject.fields.emplace_back("enable cubemap trilinear filtering", JSONValue(enableCubemapTrilinearFiltering));

			std::string cubemapSamplerSizeStr = VecToString(cubemapSamplerSize, 0);
			materialObject.fields.emplace_back("generated cubemap size", JSONValue(cubemapSamplerSizeStr));
		}

		if (shader->bNeedIrradianceSampler || irradianceSamplerSize.x > 0)
		{
			materialObject.fields.emplace_back("generate irradiance sampler", JSONValue(generateIrradianceSampler));

			std::string irradianceSamplerSizeStr = VecToString(irradianceSamplerSize, 0);
			materialObject.fields.emplace_back("generated irradiance cubemap size", JSONValue(irradianceSamplerSizeStr));
		}

		if (shader->bNeedPrefilteredMap || prefilteredMapSize.x > 0)
		{
			materialObject.fields.emplace_back("generate prefiltered map", JSONValue(generatePrefilteredMap));

			std::string prefilteredMapSizeStr = VecToString(prefilteredMapSize, 0);
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

	Texture::Texture()
	{
	}

	Texture::Texture(const std::string& name, u32 width, u32 height, u32 channelCount) :
		width(width),
		height(height),
		channelCount(channelCount),
		name(name)
	{
	}

	Texture::Texture(const std::string& relativeFilePath, u32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) :
		channelCount(channelCount),
		relativeFilePath(relativeFilePath),
		fileName(StripLeadingDirectories(relativeFilePath)),
		bFlipVertically(bFlipVertically),
		bGenerateMipMaps(bGenerateMipMaps),
		bHDR(bHDR)
	{
	}

	Texture::Texture(const std::array<std::string, 6>& relativeCubemapFilePaths, u32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR) :
		channelCount(channelCount),
		relativeCubemapFilePaths(relativeCubemapFilePaths),
		bFlipVertically(bFlipVertically),
		bGenerateMipMaps(bGenerateMipMaps),
		bHDR(bHDR)
	{
	}

} // namespace flex