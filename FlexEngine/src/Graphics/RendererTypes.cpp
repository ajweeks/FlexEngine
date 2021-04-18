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
				_u(LIGHTS)
				_u(CONST_ALBEDO)
				_u(CONST_METALLIC)
				_u(CONST_ROUGHNESS)
				_u(ENABLE_ALBEDO_SAMPLER)
				_u(ENABLE_METALLIC_SAMPLER)
				_u(ENABLE_ROUGHNESS_SAMPLER)
				_u(ENABLE_NORMAL_SAMPLER)
				_u(ENABLE_EMISSIVE_SAMPLER)
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
				_u(SHADOW_SAMPLING_DATA)
				_u(NEAR_FAR_PLANES)
				_u(POST_PROCESS_MAT)
				_u(LAST_FRAME_VIEWPROJ)
				_u(PARTICLE_BUFFER)
				_u(PARTICLE_SIM_DATA)
				_u(OCEAN_DATA)
				_u(SKYBOX_DATA)
				_u(CONST_EMISSIVE)
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
			vertexShaderFilePath = COMPILED_SHADERS_DIRECTORY + inVertexShaderFilePath;
		}
		if (!inFragmentShaderFilePath.empty())
		{
			fragmentShaderFilePath = COMPILED_SHADERS_DIRECTORY + inFragmentShaderFilePath;
		}
		if (!inGeometryShaderFilePath.empty())
		{
			geometryShaderFilePath = COMPILED_SHADERS_DIRECTORY + inGeometryShaderFilePath;
		}
		if (!inComputeShaderFilePath.empty())
		{
			computeShaderFilePath = COMPILED_SHADERS_DIRECTORY + inComputeShaderFilePath;
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
				constAlbedo == other.constAlbedo &&
				constEmissive == other.constEmissive &&
				constMetallic == other.constMetallic &&
				constRoughness == other.constRoughness &&
				enableAlbedoSampler == other.enableAlbedoSampler &&
				albedoTexturePath == other.albedoTexturePath &&
				enableEmissiveSampler == other.enableEmissiveSampler &&
				emissiveTexturePath == other.emissiveTexturePath &&
				enableMetallicSampler == other.enableMetallicSampler &&
				metallicTexturePath == other.metallicTexturePath &&
				enableRoughnessSampler == other.enableRoughnessSampler &&
				roughnessTexturePath == other.roughnessTexturePath &&
				generateHDREquirectangularSampler == other.generateHDREquirectangularSampler &&
				enableHDREquirectangularSampler == other.enableHDREquirectangularSampler &&
				hdrEquirectangularTexturePath == other.hdrEquirectangularTexturePath &&
				enableCubemapTrilinearFiltering == other.enableCubemapTrilinearFiltering &&
				generateHDRCubemapSampler == other.generateHDRCubemapSampler &&
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
			{ &createInfoOut.emissiveTexturePath, "emissive texture filepath" },
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
		material.SetBoolChecked("enable emissive sampler", createInfoOut.enableEmissiveSampler);
		material.SetBoolChecked("enable metallic sampler", createInfoOut.enableMetallicSampler);
		material.SetBoolChecked("enable roughness sampler", createInfoOut.enableRoughnessSampler);
		material.SetBoolChecked("enable normal sampler", createInfoOut.enableNormalSampler);
		material.SetBoolChecked("generate hdr equirectangular sampler", createInfoOut.generateHDREquirectangularSampler);
		material.SetBoolChecked("enable hdr equirectangular sampler", createInfoOut.enableHDREquirectangularSampler);
		material.SetBoolChecked("generate hdr cubemap sampler", createInfoOut.generateHDRCubemapSampler);
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
		material.SetVec3Checked("const emissive", createInfoOut.constEmissive);
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

		if (constAlbedo != VEC4_ONE)
		{
			std::string constAlbedoStr = VecToString(constAlbedo, 3);
			materialObject.fields.emplace_back("const albedo", JSONValue(constAlbedoStr));
		}
		if (constEmissive != VEC4_ONE)
		{
			std::string constEmissiveStr = VecToString(constEmissive, 3);
			materialObject.fields.emplace_back("const emissive", JSONValue(constEmissiveStr));
		}
		if (constMetallic != 0.0f)
		{
			materialObject.fields.emplace_back("const metallic", JSONValue(constMetallic));
		}
		if (constRoughness != 0.0f)
		{
			materialObject.fields.emplace_back("const roughness", JSONValue(constRoughness));
		}

		static const bool defaultEnableAlbedo = false;
		if (shader->textureUniforms.HasUniform(U_ALBEDO_SAMPLER) && enableAlbedoSampler != defaultEnableAlbedo)
		{
			materialObject.fields.emplace_back("enable albedo sampler", JSONValue(enableAlbedoSampler));
		}

		static const bool defaultEnableEmissive = false;
		if (shader->textureUniforms.HasUniform(U_EMISSIVE_SAMPLER) && enableEmissiveSampler != defaultEnableEmissive)
		{
			materialObject.fields.emplace_back("enable emissive sampler", JSONValue(enableEmissiveSampler));
		}

		static const bool defaultEnableMetallicSampler = false;
		if (shader->textureUniforms.HasUniform(U_METALLIC_SAMPLER) && enableMetallicSampler != defaultEnableMetallicSampler)
		{
			materialObject.fields.emplace_back("enable metallic sampler", JSONValue(enableMetallicSampler));
		}

		static const bool defaultEnableRoughness = false;
		if (shader->textureUniforms.HasUniform(U_ROUGHNESS_SAMPLER) && enableRoughnessSampler != defaultEnableRoughness)
		{
			materialObject.fields.emplace_back("enable roughness sampler", JSONValue(enableRoughnessSampler));
		}

		static const bool defaultEnableNormal = false;
		if (shader->textureUniforms.HasUniform(U_NORMAL_SAMPLER) && enableNormalSampler != defaultEnableNormal)
		{
			materialObject.fields.emplace_back("enable normal sampler", JSONValue(enableNormalSampler));
		}

		static const std::string texturePrefixStr = TEXTURE_DIRECTORY;

		if (shader->textureUniforms.HasUniform(U_ALBEDO_SAMPLER) && !albedoTexturePath.empty())
		{
			std::string shortAlbedoTexturePath = albedoTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("albedo texture filepath", JSONValue(shortAlbedoTexturePath));
		}

		if (shader->textureUniforms.HasUniform(U_EMISSIVE_SAMPLER) && !emissiveTexturePath.empty())
		{
			std::string shortEmissiveTexturePath = emissiveTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("emissive texture filepath", JSONValue(shortEmissiveTexturePath));
		}

		if (shader->textureUniforms.HasUniform(U_METALLIC_SAMPLER) && !metallicTexturePath.empty())
		{
			std::string shortMetallicTexturePath = metallicTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("metallic texture filepath", JSONValue(shortMetallicTexturePath));
		}

		if (shader->textureUniforms.HasUniform(U_ROUGHNESS_SAMPLER) && !roughnessTexturePath.empty())
		{
			std::string shortRoughnessTexturePath = roughnessTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("roughness texture filepath", JSONValue(shortRoughnessTexturePath));
		}

		if (shader->textureUniforms.HasUniform(U_NORMAL_SAMPLER) && !normalTexturePath.empty())
		{
			std::string shortNormalTexturePath = normalTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("normal texture filepath", JSONValue(shortNormalTexturePath));
		}

		if (generateHDRCubemapSampler)
		{
			materialObject.fields.emplace_back("generate hdr cubemap sampler", JSONValue(generateHDRCubemapSampler));
		}

		if (shader->textureUniforms.HasUniform(U_CUBEMAP_SAMPLER))
		{
			materialObject.fields.emplace_back("enable cubemap sampler", JSONValue(enableCubemapSampler));

			materialObject.fields.emplace_back("enable cubemap trilinear filtering", JSONValue(enableCubemapTrilinearFiltering));

			std::string cubemapSamplerSizeStr = VecToString(cubemapSamplerSize, 0);
			materialObject.fields.emplace_back("generated cubemap size", JSONValue(cubemapSamplerSizeStr));
		}

		if (shader->textureUniforms.HasUniform(U_IRRADIANCE_SAMPLER) || irradianceSamplerSize.x > 0)
		{
			materialObject.fields.emplace_back("generate irradiance sampler", JSONValue(generateIrradianceSampler));

			std::string irradianceSamplerSizeStr = VecToString(irradianceSamplerSize, 0);
			materialObject.fields.emplace_back("generated irradiance cubemap size", JSONValue(irradianceSamplerSizeStr));
		}

		if (shader->textureUniforms.HasUniform(U_PREFILTER_MAP) || prefilteredMapSize.x > 0)
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

	Texture::Texture(const std::string& name) :
		name(name)
	{
	}

} // namespace flex