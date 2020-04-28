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

		{
#define _u(uniform) if (HasUniform(U_##uniform)) size += US_##uniform;
			_u(MODEL)
				_u(VIEW)
				_u(VIEW_INV)
				_u(VIEW_PROJECTION)
				_u(PROJECTION)
				_u(PROJECTION_INV)
				_u(BLEND_SHARPNESS)
				_u(COLOR_MULTIPLIER)
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
#undef _u
		}

		return size;
	}

	Shader::Shader(const std::string& name,
		const std::string& inVertexShaderFilePath,
		const std::string& inFragmentShaderFilePath /* = "" */,
		const std::string& inGeometryShaderFilePath /* = "" */,
		const std::string& inComputeShaderFilePath /* = "" */) :
		name(name)
	{
#if COMPILE_OPEN_GL
		vertexShaderFilePath = RESOURCE_LOCATION "shaders/" + inVertexShaderFilePath;
		if (!inFragmentShaderFilePath.empty())
		{
			fragmentShaderFilePath = RESOURCE_LOCATION "shaders/" + inFragmentShaderFilePath;
		}
		if (!inGeometryShaderFilePath.empty())
		{
			geometryShaderFilePath = RESOURCE_LOCATION "shaders/" + inGeometryShaderFilePath;
		}
#elif COMPILE_VULKAN
		if (!inVertexShaderFilePath.empty())
		{
			vertexShaderFilePath = RESOURCE_LOCATION "shaders/spv/" + inVertexShaderFilePath;
		}
		if (!inFragmentShaderFilePath.empty())
		{
			fragmentShaderFilePath = RESOURCE_LOCATION "shaders/spv/" + inFragmentShaderFilePath;
		}
		if (!inGeometryShaderFilePath.empty())
		{
			geometryShaderFilePath = RESOURCE_LOCATION "shaders/spv/" + inGeometryShaderFilePath;
		}
		if (!inComputeShaderFilePath.empty())
		{
			computeShaderFilePath = RESOURCE_LOCATION "shaders/spv/" + inComputeShaderFilePath;
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
				normalTexturePath == other.normalTexturePath &&
				sampledFrameBuffers.size() == other.sampledFrameBuffers.size() &&
				cubemapSamplerSize == other.cubemapSamplerSize &&
				cubeMapFilePaths[0] == other.cubeMapFilePaths[0] &&
				constAlbedo == other.constAlbedo &&
				constMetallic == other.constMetallic &&
				constRoughness == other.constRoughness &&
				albedoTexturePath == other.albedoTexturePath &&
				metallicTexturePath == other.metallicTexturePath &&
				roughnessTexturePath == other.roughnessTexturePath &&
				hdrEquirectangularTexturePath == other.hdrEquirectangularTexturePath &&
				irradianceSamplerSize == other.irradianceSamplerSize &&
				prefilteredMapSize == other.prefilteredMapSize &&
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
			{ &createInfoOut.normalTexturePath, "normal texture filepath" },
			{ &createInfoOut.hdrEquirectangularTexturePath, "hdr equirectangular texture filepath" },
			{ &createInfoOut.environmentMapPath, "environment map path" },
		};

		for (FilePathMaterialParam& param : filePathParams)
		{
			if (material.HasField(param.name))
			{
				*param.path = RESOURCE_LOCATION "textures/" + material.GetString(param.name);
			}
		}

		material.SetVec2Checked("generated irradiance cubemap size", createInfoOut.generatedIrradianceCubemapSize);
		material.SetVec2Checked("generated prefiltered map size", createInfoOut.generatedPrefilteredCubemapSize);
		material.SetVec2Checked("generated cubemap size", createInfoOut.generatedCubemapSize);
		material.SetVec4Checked("color multiplier", createInfoOut.colorMultiplier);
		material.SetVec3Checked("const albedo", createInfoOut.constAlbedo);
		material.SetFloatChecked("const metallic", createInfoOut.constMetallic);
		material.SetFloatChecked("const roughness", createInfoOut.constRoughness);

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
		std::string constAlbedoStr = VecToString(constAlbedo, 3);
		materialObject.fields.emplace_back("const albedo", JSONValue(constAlbedoStr));
		materialObject.fields.emplace_back("const metallic", JSONValue(constMetallic));
		materialObject.fields.emplace_back("const roughness", JSONValue(constRoughness));

		static const std::string texturePrefixStr = RESOURCE_LOCATION "textures/";

		if (shader.textureUniforms.HasUniform(U_ALBEDO_SAMPLER) && !albedoTexturePath.empty())
		{
			std::string shortAlbedoTexturePath = albedoTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("albedo texture filepath", JSONValue(shortAlbedoTexturePath));
		}

		if (shader.textureUniforms.HasUniform(U_METALLIC_SAMPLER) && !metallicTexturePath.empty())
		{
			std::string shortMetallicTexturePath = metallicTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("metallic texture filepath", JSONValue(shortMetallicTexturePath));
		}

		if (shader.textureUniforms.HasUniform(U_ROUGHNESS_SAMPLER) && !roughnessTexturePath.empty())
		{
			std::string shortRoughnessTexturePath = roughnessTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("roughness texture filepath", JSONValue(shortRoughnessTexturePath));
		}

		if (shader.textureUniforms.HasUniform(U_NORMAL_SAMPLER) && !normalTexturePath.empty())
		{
			std::string shortNormalTexturePath = normalTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("normal texture filepath", JSONValue(shortNormalTexturePath));
		}

		if (shader.textureUniforms.HasUniform(U_CUBEMAP_SAMPLER))
		{
			std::string cubemapSamplerSizeStr = VecToString(cubemapSamplerSize, 0);
			materialObject.fields.emplace_back("generated cubemap size", JSONValue(cubemapSamplerSizeStr));
		}

		if (shader.textureUniforms.HasUniform(U_IRRADIANCE_SAMPLER) || irradianceSamplerSize.x > 0)
		{
			std::string irradianceSamplerSizeStr = VecToString(irradianceSamplerSize, 0);
			materialObject.fields.emplace_back("generated irradiance cubemap size", JSONValue(irradianceSamplerSizeStr));
		}

		if (shader.textureUniforms.HasUniform(U_PREFILTER_MAP) || prefilteredMapSize.x > 0)
		{
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
} // namespace flex