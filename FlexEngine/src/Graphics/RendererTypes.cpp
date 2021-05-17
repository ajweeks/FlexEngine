#include "stdafx.hpp"

#include "Graphics/RendererTypes.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "ResourceManager.hpp"

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

	i32 DebugOverlayNameToID(const char* DebugOverlayName)
	{
		for (i32 i = 0; i < (i32)g_ResourceManager->debugOverlayNames.size(); ++i)
		{
			if (StrCmpCaseInsensitive(g_ResourceManager->debugOverlayNames[i].c_str(), DebugOverlayName) == 0)
			{
				return i + 1; // One-based
			}
		}

		return -1;
	}

	bool UniformList::HasUniform(const Uniform& uniform) const
	{
		return uniforms.find(uniform.id) != uniforms.end();
	}

	void UniformList::AddUniform(const Uniform& uniform)
	{
		if (!HasUniform(uniform))
		{
			uniforms.emplace(uniform.id);
			totalSizeInBytes += (u32)uniform.size;
		}
	}

	u32 UniformList::GetSizeInBytes() const
	{
		return totalSizeInBytes;
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
		material.TryGetString("name", createInfoOut.name);
		material.TryGetString("shader", createInfoOut.shaderName);

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

		material.TryGetBool("enable albedo sampler", createInfoOut.enableAlbedoSampler);
		material.TryGetBool("enable emissive sampler", createInfoOut.enableEmissiveSampler);
		material.TryGetBool("enable metallic sampler", createInfoOut.enableMetallicSampler);
		material.TryGetBool("enable roughness sampler", createInfoOut.enableRoughnessSampler);
		material.TryGetBool("enable normal sampler", createInfoOut.enableNormalSampler);
		material.TryGetBool("generate hdr equirectangular sampler", createInfoOut.generateHDREquirectangularSampler);
		material.TryGetBool("enable hdr equirectangular sampler", createInfoOut.enableHDREquirectangularSampler);
		material.TryGetBool("generate hdr cubemap sampler", createInfoOut.generateHDRCubemapSampler);
		material.TryGetBool("generate irradiance sampler", createInfoOut.generateIrradianceSampler);
		material.TryGetBool("enable brdf lut", createInfoOut.enableBRDFLUT);
		material.TryGetBool("render to cubemap", createInfoOut.renderToCubemap);
		material.TryGetBool("enable cubemap sampler", createInfoOut.enableCubemapSampler);
		material.TryGetBool("enable cubemap trilinear filtering", createInfoOut.enableCubemapTrilinearFiltering);
		material.TryGetBool("generate cubemap sampler", createInfoOut.generateCubemapSampler);
		material.TryGetBool("generate cubemap depth buffers", createInfoOut.generateCubemapDepthBuffers);
		material.TryGetBool("generate prefiltered map", createInfoOut.generatePrefilteredMap);
		material.TryGetBool("enable prefiltered map", createInfoOut.enablePrefilteredMap);
		material.TryGetBool("generate reflection probe maps", createInfoOut.generateReflectionProbeMaps);

		material.TryGetVec2("generated irradiance cubemap size", createInfoOut.generatedIrradianceCubemapSize);
		material.TryGetVec2("generated prefiltered map size", createInfoOut.generatedPrefilteredCubemapSize);
		material.TryGetVec2("generated cubemap size", createInfoOut.generatedCubemapSize);
		material.TryGetVec4("colour multiplier", createInfoOut.colourMultiplier);
		material.TryGetVec3("const albedo", createInfoOut.constAlbedo);
		material.TryGetVec3("const emissive", createInfoOut.constEmissive);
		material.TryGetFloat("const metallic", createInfoOut.constMetallic);
		material.TryGetFloat("const roughness", createInfoOut.constRoughness);

		material.TryGetFloat("texture scale", createInfoOut.textureScale);
	}

	std::vector<MaterialID> Material::ParseMaterialArrayJSON(const JSONObject& object, i32 fileVersion)
	{
		std::vector<MaterialID> matIDs;
		if (fileVersion >= 3)
		{
			std::vector<JSONField> materialNames;
			if (object.TryGetFieldArray("materials", materialNames))
			{
				for (const JSONField& materialNameField : materialNames)
				{
					std::string materialName = materialNameField.value.strValue;
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
			if (object.TryGetString("material", materialName))
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

		if (colourMultiplier != VEC4_ONE)
		{
			std::string colourMultiplierStr = VecToString(colourMultiplier, 3);
			materialObject.fields.emplace_back("colour multiplier", JSONValue(colourMultiplierStr));
		}
		return materialObject;
	}

	Texture::Texture(const std::string& name) :
		name(name)
	{
	}

} // namespace flex