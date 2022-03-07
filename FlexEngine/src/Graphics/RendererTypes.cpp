#include "stdafx.hpp"

#include "Graphics/RendererTypes.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"

namespace flex
{
	bool operator==(const RoadSegment_GPU& lhs, const RoadSegment_GPU& rhs)
	{
		return memcmp(&lhs, &rhs, sizeof(RoadSegment_GPU)) == 0;
	}

	Uniform::Uniform(const char* uniformName, StringID id, u64 size) :
		id(id),
		size((u32)size)
	{
		RegisterUniform(id, this);

#if DEBUG
		DBG_name = uniformName;
#else
		FLEX_UNUSED(uniformName);
#endif
	}

	static std::vector<Uniform const*>& GetAllUniforms()
	{
		static std::vector<Uniform const*> allUniforms;
		return allUniforms;
	}

	static Uniform const* GetUniform(const std::vector<Uniform const*>& uniforms, StringID uniformID)
	{
		PROFILE_AUTO("GetUniform");

		for (Uniform const* uniform : uniforms)
		{
			if (uniform->id == uniformID)
			{
				return uniform;
			}
		}
		return nullptr;
	}

	void RegisterUniform(StringID uniformNameSID, Uniform* uniform)
	{
		std::vector<Uniform const*>& allUniforms = GetAllUniforms();
		if (GetUniform(allUniforms, uniformNameSID) == nullptr)
		{
			allUniforms.emplace_back(uniform);
		}
	}

	Uniform const* UniformFromStringID(StringID uniformNameSID)
	{
		PROFILE_AUTO("UniformFromStringID");

		std::vector<Uniform const*>& allUniforms = GetAllUniforms();
		Uniform const* uniform = GetUniform(allUniforms, uniformNameSID);
		if (uniform != nullptr)
		{
			return uniform;
		}

		return nullptr;
	}

	i32 DebugOverlayNameToID(const char* DebugOverlayName)
	{
		for (i32 i = 0; i < (i32)g_ResourceManager->debugOverlayNames.size(); ++i)
		{
			if (StrCmpCaseInsensitive(g_ResourceManager->debugOverlayNames[i].c_str(), DebugOverlayName) == 0)
			{
				return i;
			}
		}

		return -1;
	}

	bool UniformList::HasUniform(Uniform const* uniform) const
	{
		return HasUniform(uniform->id);
	}

	bool UniformList::HasUniform(const StringID& uniformID) const
	{
		PROFILE_AUTO("UniformList HasUniform");

		for (Uniform const* uniform : uniforms)
		{
			if (uniform->id == uniformID)
			{
				return true;
			}
		}
		return false;
	}

	void UniformList::AddUniform(Uniform const* uniform)
	{
		PROFILE_AUTO("UniformList AddUniform");

		if (!HasUniform(uniform))
		{
			uniforms.emplace_back(uniform);
			totalSizeInBytes += (u32)uniform->size;
		}
	}

	u32 UniformList::GetSizeInBytes() const
	{
		return totalSizeInBytes;
	}

	GPUBufferList::~GPUBufferList()
	{
		for (GPUBuffer* buffer : bufferList)
		{
			g_Renderer->FreeGPUBuffer(buffer);
		}
		bufferList.clear();
	}

	void GPUBufferList::Add(GPUBufferType type, const std::string& debugName)
	{
		GPUBuffer* buffer = g_Renderer->AllocateGPUBuffer(type, debugName);
		bufferList.emplace_back(buffer);
	}

	const GPUBuffer* GPUBufferList::Get(GPUBufferType type) const
	{
		for (GPUBuffer* buffer : bufferList)
		{
			if (buffer->type == type)
			{
				return buffer;
			}
		}
		return nullptr;
	}

	bool GPUBufferList::Has(GPUBufferType type) const
	{
		for (GPUBuffer const* buffer : bufferList)
		{
			if (buffer->type == type)
			{
				return true;
			}
		}
		return false;
	}

	GPUBuffer* GPUBufferList::Get(GPUBufferType type)
	{
		CHECK_NE((u32)type, (u32)GPUBufferType::TERRAIN_VERTEX_BUFFER); // Terrain data should be retrieved via VulkanRenderer::m_Terrain, not through a uniform buffer list!
		for (GPUBuffer* buffer : bufferList)
		{
			if (buffer->type == type)
			{
				return buffer;
			}
		}
		return nullptr;
	}

	GPUBuffer::GPUBuffer(GPUBufferType type, const std::string& debugName) :
		type(type),
		debugName(debugName)
	{
		ID = g_Renderer->RegisterGPUBuffer(this);
	}

	GPUBuffer::~GPUBuffer()
	{
		g_Renderer->UnregisterGPUBuffer(ID);
		ID = InvalidGPUBufferID;
		FreeHostMemory();
	}

	void GPUBuffer::AllocHostMemory(u32 size, u32 alignment /* = u32_max */)
	{
		CHECK_EQ(data.data, nullptr);

		if (type == GPUBufferType::DYNAMIC ||
			type == GPUBufferType::PARTICLE_DATA ||
			type == GPUBufferType::TERRAIN_POINT_BUFFER ||
			type == GPUBufferType::TERRAIN_VERTEX_BUFFER)
		{
			CHECK_NE(alignment, u32_max);
			data.data = (u8*)flex_aligned_malloc(size, alignment);
		}
		else
		{
			data.data = (u8*)malloc(size);
		}

		CHECK_NE(data.data, nullptr);
	}

	void GPUBuffer::FreeHostMemory()
	{
		if (data.data != nullptr)
		{
			if (type == GPUBufferType::DYNAMIC ||
				type == GPUBufferType::PARTICLE_DATA ||
				type == GPUBufferType::TERRAIN_POINT_BUFFER ||
				type == GPUBufferType::TERRAIN_VERTEX_BUFFER)
			{
				flex_aligned_free(data.data);
			}
			else
			{
				free(data.data);
			}

			data.data = nullptr;
		}
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
	}

	Material::~Material()
	{
		if (pushConstantBlock != nullptr)
		{
			delete pushConstantBlock;
			pushConstantBlock = nullptr;
		}
	}

	Material::Material(const Material& rhs)
	{
		if (rhs.pushConstantBlock != nullptr)
		{
			pushConstantBlock = new PushConstantBlock(*rhs.pushConstantBlock);
		}
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

	void Material::ParseJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut, i32 fileVersion)
	{
		FLEX_UNUSED(fileVersion);

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
		material.TryGetVec4("const albedo", createInfoOut.constAlbedo);
		material.TryGetVec4("const emissive", createInfoOut.constEmissive);

		material.TryGetFloat("const metallic", createInfoOut.constMetallic);
		material.TryGetFloat("const roughness", createInfoOut.constRoughness);

		material.TryGetFloat("texture scale", createInfoOut.textureScale);

		material.TryGetBool("dynamic", createInfoOut.bDynamic);
	}

	std::vector<MaterialID> Material::ParseMaterialArrayJSON(const JSONObject& object, i32 fileVersion)
	{
		PROFILE_AUTO("Material ParseMaterialArrayJSON");

		std::vector<MaterialID> matIDs;
		if (fileVersion >= 3)
		{
			std::vector<JSONField> materialNames;
			if (object.TryGetFieldArray("materials", materialNames))
			{
				for (const JSONField& materialNameField : materialNames)
				{
					std::string materialName = materialNameField.value.AsString();
					bool bSuccess = false;
					if (!materialName.empty())
					{
						MaterialID materialID = InvalidMaterialID;
						if (g_Renderer->FindOrCreateMaterialByName(materialName, materialID))
						{
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
			std::string materialName;
			MaterialID materialID = InvalidMaterialID;
			if (object.TryGetString("material", materialName))
			{
				if (!materialName.empty())
				{
					if (g_Renderer->FindOrCreateMaterialByName(materialName, materialID))
					{
						if (materialID == InvalidMaterialID)
						{
							if (materialName.compare("placeholder") == 0)
							{
								materialID = g_Renderer->GetPlaceholderMaterialID();
							}
						}
					}
				}

				if (materialID == InvalidMaterialID)
				{
					PrintError("Invalid material name for object %s: %s\n", object.GetString("name").c_str(), materialName.c_str());
				}
			}

			if (materialID != InvalidMaterialID)
			{
				matIDs.push_back(materialID);
			}
		}

		return matIDs;
	}

	Material::PushConstantBlock::PushConstantBlock(i32 initialSize) :
		size(initialSize)
	{
		CHECK_NE(initialSize, 0);
	}

	Material::PushConstantBlock::PushConstantBlock(const PushConstantBlock& rhs)
	{
		data = rhs.data;
		size = rhs.size;
	}

	Material::PushConstantBlock::PushConstantBlock(PushConstantBlock&& rhs)
	{
		data = rhs.data;
		size = rhs.size;
	}

	Material::PushConstantBlock& Material::PushConstantBlock::operator=(const PushConstantBlock& rhs)
	{
		data = rhs.data;
		size = rhs.size;
		return *this;
	}

	Material::PushConstantBlock& Material::PushConstantBlock::operator=(PushConstantBlock&& rhs)
	{
		data = rhs.data;
		size = rhs.size;
		return *this;
	}

	Material::PushConstantBlock::~PushConstantBlock()
	{
		if (data != nullptr)
		{
			free(data);
			data = nullptr;
			size = 0;
		}
	}

	void Material::PushConstantBlock::InitWithSize(u32 dataSize)
	{
		PROFILE_AUTO("PushConstantBlock InitWithSize");

		if (data == nullptr)
		{
			if (size != dataSize && size != 0)
			{
				PrintError("Push constant block size changed! (%d to %d) Memory leak will occur\n", size, dataSize);
			}

			size = dataSize;
			if (dataSize != 0)
			{
				data = malloc(dataSize);
			}
		}
		else
		{
			// Push constant blocks must be reallocated when block size changes.
			CHECK_EQ(size, dataSize);
		}
	}

	void Material::PushConstantBlock::SetData(real* newData, u32 dataSize)
	{
		PROFILE_AUTO("PushConstantBlock SetData u32");

		InitWithSize(dataSize);
		memcpy(data, newData, size);
	}

	void Material::PushConstantBlock::SetData(const std::vector<Pair<void*, u32>>& dataList)
	{
		PROFILE_AUTO("PushConstantBlock SetData std::vector<Pair<void*, u32>>");

		u32 dataSize = 0;
		for (const auto& pair : dataList)
		{
			dataSize += pair.second;
		}
		InitWithSize(dataSize);

		real* dst = (real*)data;

		for (auto& pair : dataList)
		{
			memcpy(dst, pair.first, pair.second);
			dst += pair.second / sizeof(real);
		}
	}

	void Material::PushConstantBlock::SetData(const glm::mat4& viewProj)
	{
		PROFILE_AUTO("PushConstantBlock SetData glm::mat4");

		const u32 dataSize = sizeof(glm::mat4) * 1;
		InitWithSize(dataSize);

		real* dst = (real*)data;
		memcpy(dst, &viewProj, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
	}

	void Material::PushConstantBlock::SetData(const glm::mat4& view, const glm::mat4& proj)
	{
		PROFILE_AUTO("PushConstantBlock SetData glm::mat4 glm::mat4");

		const u32 dataSize = sizeof(glm::mat4) * 2;
		InitWithSize(dataSize);

		real* dst = (real*)data;
		memcpy(dst, &view, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
		memcpy(dst, &proj, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
	}

	void Material::PushConstantBlock::SetData(const glm::mat4& view, const glm::mat4& proj, i32 textureIndex)
	{
		PROFILE_AUTO("PushConstantBlock SetData glm::mat4 glm::mat4 i32");

		const u32 dataSize = sizeof(glm::mat4) * 2 + sizeof(i32);
		if (data == nullptr)
		{
			CHECK(size == dataSize || size == 0);

			size = dataSize;
			data = malloc(dataSize);
			CHECK_NE(data, nullptr);
		}
		else
		{
			// Push constant block must be reallocated when block size changes.
			CHECK_EQ(size, dataSize);
		}
		real* dst = (real*)data;
		memcpy(dst, &view, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
		memcpy(dst, &proj, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
		memcpy(dst, &textureIndex, sizeof(i32)); dst += sizeof(i32) / sizeof(real);
	}

	JSONObject Material::Serialize() const
	{
		PROFILE_AUTO("Material Serialize");

		// TODO: Make more generic key-value system

		CHECK(bSerializable);

		JSONObject parentObj = {};

		parentObj.fields.emplace_back("version", JSONValue(BaseScene::LATEST_MATERIALS_FILE_VERSION));

		JSONObject materialObject = {};

		materialObject.fields.emplace_back("name", JSONValue(name));

		const Shader* shader = g_Renderer->GetShader(shaderID);
		if (shader == nullptr)
		{
			materialObject.fields.emplace_back("shader", JSONValue("Invalid"));
			return materialObject;
		}

		materialObject.fields.emplace_back("shader", JSONValue(shader->name));

		if (constAlbedo != VEC4_ONE)
		{
			materialObject.fields.emplace_back("const albedo", JSONValue((glm::vec3)constAlbedo, 3));
		}
		if (constEmissive != VEC4_ZERO)
		{
			materialObject.fields.emplace_back("const emissive", JSONValue((glm::vec3)constEmissive, 3));
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
		if (shader->textureUniforms.HasUniform(&U_ALBEDO_SAMPLER) && enableAlbedoSampler != defaultEnableAlbedo)
		{
			materialObject.fields.emplace_back("enable albedo sampler", JSONValue(enableAlbedoSampler));
		}

		static const bool defaultEnableEmissive = false;
		if (shader->textureUniforms.HasUniform(&U_EMISSIVE_SAMPLER) && enableEmissiveSampler != defaultEnableEmissive)
		{
			materialObject.fields.emplace_back("enable emissive sampler", JSONValue(enableEmissiveSampler));
		}

		static const bool defaultEnableMetallicSampler = false;
		if (shader->textureUniforms.HasUniform(&U_METALLIC_SAMPLER) && enableMetallicSampler != defaultEnableMetallicSampler)
		{
			materialObject.fields.emplace_back("enable metallic sampler", JSONValue(enableMetallicSampler));
		}

		static const bool defaultEnableRoughness = false;
		if (shader->textureUniforms.HasUniform(&U_ROUGHNESS_SAMPLER) && enableRoughnessSampler != defaultEnableRoughness)
		{
			materialObject.fields.emplace_back("enable roughness sampler", JSONValue(enableRoughnessSampler));
		}

		static const bool defaultEnableNormal = false;
		if (shader->textureUniforms.HasUniform(&U_NORMAL_SAMPLER) && enableNormalSampler != defaultEnableNormal)
		{
			materialObject.fields.emplace_back("enable normal sampler", JSONValue(enableNormalSampler));
		}

		static const std::string texturePrefixStr = TEXTURE_DIRECTORY;

		if (shader->textureUniforms.HasUniform(&U_ALBEDO_SAMPLER) && !albedoTexturePath.empty())
		{
			std::string shortAlbedoTexturePath = albedoTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("albedo texture filepath", JSONValue(shortAlbedoTexturePath));
		}

		if (shader->textureUniforms.HasUniform(&U_EMISSIVE_SAMPLER) && !emissiveTexturePath.empty())
		{
			std::string shortEmissiveTexturePath = emissiveTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("emissive texture filepath", JSONValue(shortEmissiveTexturePath));
		}

		if (shader->textureUniforms.HasUniform(&U_METALLIC_SAMPLER) && !metallicTexturePath.empty())
		{
			std::string shortMetallicTexturePath = metallicTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("metallic texture filepath", JSONValue(shortMetallicTexturePath));
		}

		if (shader->textureUniforms.HasUniform(&U_ROUGHNESS_SAMPLER) && !roughnessTexturePath.empty())
		{
			std::string shortRoughnessTexturePath = roughnessTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("roughness texture filepath", JSONValue(shortRoughnessTexturePath));
		}

		if (shader->textureUniforms.HasUniform(&U_NORMAL_SAMPLER) && !normalTexturePath.empty())
		{
			std::string shortNormalTexturePath = normalTexturePath.substr(texturePrefixStr.length());
			materialObject.fields.emplace_back("normal texture filepath", JSONValue(shortNormalTexturePath));
		}

		if (generateHDRCubemapSampler)
		{
			materialObject.fields.emplace_back("generate hdr cubemap sampler", JSONValue(generateHDRCubemapSampler));
		}

		if (shader->textureUniforms.HasUniform(&U_CUBEMAP_SAMPLER))
		{
			materialObject.fields.emplace_back("enable cubemap sampler", JSONValue(enableCubemapSampler));
			materialObject.fields.emplace_back("enable cubemap trilinear filtering", JSONValue(enableCubemapTrilinearFiltering));
			materialObject.fields.emplace_back("generated cubemap size", JSONValue(cubemapSamplerSize, 0));
		}

		if (shader->textureUniforms.HasUniform(&U_IRRADIANCE_SAMPLER) || irradianceSamplerSize.x > 0)
		{
			materialObject.fields.emplace_back("generate irradiance sampler", JSONValue(generateIrradianceSampler));
			materialObject.fields.emplace_back("generated irradiance cubemap size", JSONValue(irradianceSamplerSize, 0));
		}

		if (shader->textureUniforms.HasUniform(&U_PREFILTER_MAP) || prefilteredMapSize.x > 0)
		{
			materialObject.fields.emplace_back("generate prefiltered map", JSONValue(generatePrefilteredMap));
			materialObject.fields.emplace_back("generated prefiltered map size", JSONValue(prefilteredMapSize, 0));
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
			materialObject.fields.emplace_back("colour multiplier", JSONValue(colourMultiplier, 3));
		}

		if (generateReflectionProbeMaps)
		{
			materialObject.fields.emplace_back("generate reflection probe maps", JSONValue(generateReflectionProbeMaps));
		}

		if (bDynamic)
		{
			materialObject.fields.emplace_back("dynamic", JSONValue(bDynamic));
		}

		parentObj.fields.emplace_back("material", JSONValue(materialObject));

		return parentObj;
	}

	Texture::Texture(const std::string& name) :
		name(name)
	{
	}

	void UniformOverrides::AddUniform(Uniform const* uniform, const MaterialPropertyOverride& propertyOverride)
	{
		PROFILE_AUTO("UniformOverrides AddUniform");

		overrides[uniform->id] = UniformPair(uniform, propertyOverride);
	}

	bool UniformOverrides::HasUniform(Uniform const* uniform) const
	{
		PROFILE_AUTO("UniformOverrides HasUniform");

		return Contains(overrides, uniform->id);
	}

	bool UniformOverrides::HasUniform(Uniform const* uniform, MaterialPropertyOverride& outPropertyOverride) const
	{
		PROFILE_AUTO("UniformOverrides HasUniform outPropertyOverride");

		auto iter = overrides.find(uniform->id);
		if (iter != overrides.end())
		{
			outPropertyOverride = iter->second.second;
			return true;
		}

		return false;
	}
} // namespace flex