#pragma once

#include <array>
#include <map>

IGNORE_WARNINGS_PUSH
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
IGNORE_WARNINGS_POP

#include "Functors.hpp"
#include "Helpers.hpp" // For Hash
#include "JSONTypes.hpp"
#include "Pair.hpp"

namespace flex
{
	class VertexBufferData;
	class GameObject;

	static const i32 MAX_POINT_LIGHT_COUNT = 8;
	static const i32 MAX_SPOT_LIGHT_COUNT = 8;
	static const i32 MAX_AREA_LIGHT_COUNT = 8;
	static const i32 MAX_SHADOW_CASCADE_COUNT = 4;

	// 48 bytes
	struct DirLightData
	{
		glm::vec3 dir;       // 0
		i32 enabled;         // 12
		glm::vec3 colour;    // 16
		real brightness;     // 28
		i32 castShadows;     // 32
		real shadowDarkness; // 36
		real pad[2];         // 40
	};

	// 32 bytes
	struct PointLightData
	{
		glm::vec3 pos;    // 0
		i32 enabled;      // 12
		glm::vec3 colour; // 16
		real brightness;  // 28
	};

	// 48 bytes
	struct SpotLightData
	{
		glm::vec3 pos;		// 0
		i32 enabled;		// 12
		glm::vec3 colour;	// 16
		real brightness;	// 28
		glm::vec3 dir;		// 32
		real angle;			// 44
	};

	// 96 bytes
	struct AreaLightData
	{
		glm::vec3 colour;		// 0
		real brightness;		// 12
		real pad[3];			// 20
		i32 enabled;			// 16
		glm::vec4 points[4];	// 32
	};

	const i32 MAX_SSAO_KERNEL_SIZE = 64;
	// 1028 bytes
	struct SSAOGenData
	{
		glm::vec4 samples[MAX_SSAO_KERNEL_SIZE]; // 0
		real radius;                             // 1024
	};

	// 4 bytes
	struct SSAOBlurDataConstant
	{
		i32 radius; // 0
	};

	// 8 bytes
	struct SSAOBlurDataDynamic
	{
		glm::vec2 ssaoTexelOffset; // 0
	};

	// 16 bytes
	struct SSAOSamplingData
	{
		i32 enabled; // 0
		real powExp; // 4
		real pad[2]; // 8
	};

	// 32 bytes
	struct FXAAData
	{
		real lumaThresholdMin; // 0
		real lumaThresholdMax; // 4
		real mulReduce;        // 8
		real minReduce;        // 12
		real maxSpan;          // 16
		glm::vec2 texelStep;   // 20
		i32 bDEBUGShowEdges;   // 28
	};

	// 272 bytes
	struct ShadowSamplingData
	{
		glm::mat4 cascadeViewProjMats[MAX_SHADOW_CASCADE_COUNT];	// 0
		glm::vec4 cascadeDepthSplits;								// 256
	};

	struct SHCoeffs
	{
		glm::vec4 r0;
		glm::vec4 g0;
		glm::vec4 b0;
		glm::vec4 r1;
		glm::vec4 g1;
		glm::vec4 b1;
		glm::vec4 rgb2;
	};

#pragma pack(push, 1)
	// 54 bytes
	struct ParticleBufferData
	{
		glm::vec3 pos;		// 12
		glm::vec4 colour;	// 16
		glm::vec3 vel;		// 12
		glm::vec4 extraVec4;// 16
	};
#pragma pack(pop)

	// 44 bytes
	struct ParticleSimData
	{
		glm::vec4 colour0;	// 16
		glm::vec4 colour1;	// 16
		real dt;			// 4
		real speed;			// 4
		u32 particleCount;	// 4
	};

	// 80 bytes
	struct OceanData
	{
		glm::vec4 top;				// 16
		glm::vec4 mid;				// 16
		glm::vec4 btm;				// 16
		real fresnelFactor;			// 4
		real fresnelPower;			// 4
		real skyReflectionFactor;	// 4
		real fogFalloff;			// 4
		real fogDensity;			// 4
		real pad[3];				// 12
	};

	// 64 bytes
	struct SkyboxData
	{
		glm::vec4 top; // 16
		glm::vec4 mid; // 16
		glm::vec4 btm; // 16
		glm::vec4 fog; // 16
	};

	struct Uniform
	{
		Uniform(StringID id, u64 size = 0) :
			id(id),
			size((u32)size)
		{
		}

		StringID id;
		u32 size;
	};

	const Uniform U_MODEL(SID("model"), sizeof(glm::mat4));
	const Uniform U_VIEW(SID("view"), sizeof(glm::mat4));
	const Uniform U_VIEW_INV(SID("invView"), sizeof(glm::mat4));
	const Uniform U_VIEW_PROJECTION(SID("viewProjection"), sizeof(glm::mat4));
	const Uniform U_PROJECTION(SID("projection"), sizeof(glm::mat4));
	const Uniform U_PROJECTION_INV(SID("invProjection"), sizeof(glm::mat4));
	const Uniform U_BLEND_SHARPNESS(SID("blendSharpness"), sizeof(real));
	const Uniform U_COLOUR_MULTIPLIER(SID("colourMultiplier"), sizeof(glm::vec4));
	const Uniform U_CAM_POS(SID("camPos"), sizeof(glm::vec4));
	const Uniform U_LIGHTS(SID("lights"),
			(sizeof(PointLightData) * MAX_POINT_LIGHT_COUNT +
			sizeof(SpotLightData) * MAX_SPOT_LIGHT_COUNT +
			sizeof(AreaLightData) * MAX_AREA_LIGHT_COUNT));
	const Uniform U_DIR_LIGHT(SID("dirLight"), (u32)sizeof(DirLightData));
	const Uniform U_ALBEDO_SAMPLER(SID("albedoSampler"));
	const Uniform U_CONST_ALBEDO(SID("constAlbedo"), (u32)sizeof(glm::vec4));
	const Uniform U_METALLIC_SAMPLER(SID("metallicSampler"));
	const Uniform U_CONST_METALLIC(SID("constMetallic"), sizeof(real));
	const Uniform U_ROUGHNESS_SAMPLER(SID("roughnessSampler"));
	const Uniform U_CONST_ROUGHNESS(SID("constRoughness"), sizeof(real));
	const Uniform U_CONST_EMISSIVE(SID("constEmissive"), sizeof(glm::vec4));
	const Uniform U_NORMAL_SAMPLER(SID("normalSampler"));
	const Uniform U_ENABLE_ALBEDO_SAMPLER(SID("enableAlbedoSampler"), sizeof(i32));
	const Uniform U_ENABLE_METALLIC_SAMPLER(SID("enableMetallicSampler"), sizeof(i32));
	const Uniform U_ENABLE_ROUGHNESS_SAMPLER(SID("enableRoughnessSampler"), sizeof(i32));
	const Uniform U_ENABLE_NORMAL_SAMPLER(SID("enableNormalSampler"), sizeof(i32));
	const Uniform U_EMISSIVE_SAMPLER(SID("emissiveSampler"));
	const Uniform U_CUBEMAP_SAMPLER(SID("cubemapSampler"));
	const Uniform U_IRRADIANCE_SAMPLER(SID("irradianceSampler"));
	const Uniform U_FB_0_SAMPLER(SID("normalRoughnessTex"));
	const Uniform U_FB_1_SAMPLER(SID("albedoMetallicTex"));
	const Uniform U_ENABLE_EMISSIVE_SAMPLER(SID("enableEmissiveSampler"), sizeof(i32));
	const Uniform U_HDR_EQUIRECTANGULAR_SAMPLER(SID("hdrEquirectangular"));
	const Uniform U_BRDF_LUT_SAMPLER(SID("brdfLUT"));
	const Uniform U_PREFILTER_MAP(SID("prefilterMap"));
	const Uniform U_EXPOSURE(SID("exposure"), sizeof(real));
	const Uniform U_FONT_CHAR_DATA(SID("fontCharData"), sizeof(glm::vec4));
	const Uniform U_TEX_SIZE(SID("texSize"), sizeof(glm::vec2));
	const Uniform U_TEXTURE_SCALE(SID("textureScale"), sizeof(real));
	const Uniform U_TEX_CHANNEL(SID("texChannel"), sizeof(i32));
	const Uniform U_UNIFORM_BUFFER_CONSTANT(SID("uniformBufferConstant"));
	const Uniform U_UNIFORM_BUFFER_DYNAMIC(SID("uniformBufferDynamic"));
	const Uniform U_TIME(SID("time"), sizeof(glm::vec4));
	const Uniform U_SDF_DATA(SID("sdfData"), sizeof(glm::vec4));
	const Uniform U_HIGH_RES_TEX(SID("highResTex"));
	const Uniform U_DEPTH_SAMPLER(SID("depthSampler"));
	const Uniform U_NOISE_SAMPLER(SID("noiseSampler"));
	const Uniform U_SSAO_RAW_SAMPLER(SID("ssaoRawSampler"));
	const Uniform U_SSAO_FINAL_SAMPLER(SID("ssaoFinalSampler"));
	const Uniform U_SSAO_NORMAL_SAMPLER(SID("ssaoNormalSampler")); // TODO: Use normalSampler uniform?
	const Uniform U_SSAO_GEN_DATA(SID("ssaoGenData"), sizeof(SSAOGenData));
	const Uniform U_SSAO_BLUR_DATA_DYNAMIC(SID("ssaoBlurDataDynamic"), sizeof(SSAOBlurDataDynamic));
	const Uniform U_SSAO_BLUR_DATA_CONSTANT(SID("ssaoBlurDataConstant"), sizeof(SSAOBlurDataConstant));
	const Uniform U_SSAO_SAMPLING_DATA(SID("ssaoSamplingData"), sizeof(SSAOSamplingData));
	const Uniform U_LTC_SAMPLER_0(SID("ltcSampler0"));
	const Uniform U_LTC_SAMPLER_1(SID("ltcSampler1"));
	const Uniform U_SHADOW_SAMPLER(SID("shadowSampler")); // TODO: Rename to cascade
	const Uniform U_SHADOW_SAMPLING_DATA(SID("shadowSamplingData"), sizeof(ShadowSamplingData));
	const Uniform U_NEAR_FAR_PLANES(SID("nearFarPlanes"), sizeof(glm::vec2));
	const Uniform U_POST_PROCESS_MAT(SID("postProcessMatrix"), sizeof(glm::mat4));
	const Uniform U_SCENE_SAMPLER(SID("sceneSampler"));
	const Uniform U_HISTORY_SAMPLER(SID("historySampler"));
	const Uniform U_LAST_FRAME_VIEWPROJ(SID("lastFrameViewProj"), sizeof(glm::mat4));
	const Uniform U_PARTICLE_BUFFER(SID("particleBuffer"), sizeof(ParticleBufferData));
	const Uniform U_PARTICLE_SIM_DATA(SID("particleSimData"), sizeof(ParticleSimData));
	const Uniform U_OCEAN_DATA(SID("oceanData"), sizeof(OceanData));
	const Uniform U_SKYBOX_DATA(SID("skyboxData"), sizeof(SkyboxData));

	enum class ClearFlag
	{
		COLOUR = (1 << 0),
		DEPTH = (1 << 1),
		STENCIL = (1 << 2),

		_NONE
	};

	enum class CullFace
	{
		BACK,
		FRONT,
		FRONT_AND_BACK,
		NONE,

		_INVALID
	};

	enum class DepthTestFunc
	{
		ALWAYS,
		NEVER,
		LESS,
		LEQUAL,
		GREATER,
		GEQUAL,
		EQUAL,
		NOTEQUAL,

		_NONE
	};

	enum class BufferTarget
	{
		ARRAY_BUFFER,
		ELEMENT_ARRAY_BUFFER,

		_NONE
	};

	enum class UsageFlag
	{
		STATIC_DRAW,
		DYNAMIC_DRAW,

		_NONE
	};

	enum class DataType
	{
		BOOL,
		BYTE,
		UNSIGNED_BYTE,
		SHORT,
		UNSIGNED_SHORT,
		INT,
		UNSIGNED_INT,
		FLOAT,
		DOUBLE,
		FLOAT_VEC2,
		FLOAT_VEC3,
		FLOAT_VEC4,
		FLOAT_MAT3,
		FLOAT_MAT4,
		INT_VEC2,
		INT_VEC3,
		INT_VEC4,
		SAMPLER_1D,
		SAMPLER_2D,
		SAMPLER_3D,
		SAMPLER_CUBE,
		SAMPLER_1D_SHADOW,
		SAMPLER_2D_SHADOW,
		SAMPLER_CUBE_SHADOW,

		_NONE
	};

	static const char* DataTypeStrings[] =
	{
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

	static_assert(ARRAY_LENGTH(DataTypeStrings) == (u32)DataType::_NONE + 1, "DataTypeStrings length must match DataType enum");

	enum class TopologyMode
	{
		POINT_LIST,
		LINE_LIST,
		LINE_LOOP,
		LINE_STRIP,
		TRIANGLE_LIST,
		TRIANGLE_STRIP,
		TRIANGLE_FAN,

		_NONE
	};

	CullFace StringToCullFace(const std::string& str);
	std::string CullFaceToString(CullFace cullFace);

	// TODO: Remove
	enum RenderBatchDirtyFlag : u32
	{
		CLEAN = 0,
		STATIC_DATA = 1 << 0,
		DYNAMIC_DATA = 1 << 1,
		SHADOW_DATA = 1 << 2,

		MAX_VALUE = 1 << 30,
		_NONE
	};

	using RenderBatchDirtyFlags = u32;

	// TODO: Is setting all the members to false necessary?
	// TODO: Straight up copy most of these with a memcpy?
	struct MaterialCreateInfo
	{
		std::string shaderName;
		std::string name;

		std::string normalTexturePath;
		std::string albedoTexturePath;
		std::string emissiveTexturePath;
		std::string metallicTexturePath;
		std::string roughnessTexturePath;
		std::string hdrEquirectangularTexturePath;

		glm::vec4 colourMultiplier = VEC4_ONE;
		std::vector<Pair<std::string, void*>> sampledFrameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs
		glm::vec2 generatedIrradianceCubemapSize = VEC2_ZERO;
		MaterialID irradianceSamplerMatID = InvalidMaterialID; // The id of the material who has an irradiance sampler object (generateIrradianceSampler must be false)
		std::string environmentMapPath;
		glm::vec2 generatedCubemapSize = VEC2_ZERO;
		glm::vec2 generatedPrefilteredCubemapSize = VEC2_ZERO;
		MaterialID prefilterMapSamplerMatID = InvalidMaterialID;

		// PBR Constant values
		glm::vec3 constAlbedo = VEC3_ONE;
		glm::vec3 constEmissive = VEC3_ONE;
		real constMetallic = 0.0f;
		real constRoughness = 0.0f;

		real textureScale = 1.0f;

		bool enableNormalSampler = false;
		bool enableAlbedoSampler = false;
		bool enableEmissiveSampler = false;
		bool enableMetallicSampler = false;
		bool enableRoughnessSampler = false;
		bool generateHDREquirectangularSampler = false;
		bool enableHDREquirectangularSampler = false;
		bool generateHDRCubemapSampler = false;

		bool generateIrradianceSampler = false;

		bool enableBRDFLUT = false;
		bool renderToCubemap = true;

		bool enableCubemapSampler = false;
		bool enableCubemapTrilinearFiltering = false;
		bool generateCubemapSampler = false;
		bool generateCubemapDepthBuffers = false;

		bool generatePrefilteredMap = false;
		bool enablePrefilteredMap = false;

		bool generateReflectionProbeMaps = false;

		bool bDynamic = false; // True if vertex data is uploaded to the GPU often
		bool bSerializable = true;

		bool persistent = false;
		bool visibleInEditor = true;
	};

	struct Texture
	{
		Texture();
		Texture(const std::string& name);

		virtual ~Texture() {}

		Texture(const Texture&) = delete;
		Texture(const Texture&&) = delete;
		Texture& operator=(const Texture&) = delete;
		Texture& operator=(const Texture&&) = delete;

		// TODO: Add Reload

		u32 width = 0;
		u32 height = 0;
		u32 channelCount = 0;
		std::string name;
		std::string relativeFilePath;
		std::string fileName;
		std::array<std::string, 6> relativeCubemapFilePaths;
		u32 mipLevels = 1;
		bool bFlipVertically = false;
		bool bGenerateMipMaps = false;
		bool bHDR = false;
		bool bIsArray = false;
		bool bSamplerClampToBorder = false;
	};

	template<typename UniformType>
	struct ShaderUniformContainer
	{
		struct TexPair
		{
			TexPair(StringID uniformID, UniformType object) :
				uniformID(uniformID),
				object(object)
			{}

			StringID uniformID;
			UniformType object;
		};

		using iter = typename std::vector<TexPair>::iterator;
		using const_iter = typename std::vector<TexPair>::const_iterator;

		void SetUniform(const Uniform& uniform, const UniformType object, std::string slotName = "")
		{
			for (auto value_iter = values.begin(); value_iter != values.end(); ++value_iter)
			{
				if (value_iter->uniformID == uniform.id)
				{
					value_iter->object = object;
					slotNames[value_iter - values.begin()] = slotName;
					return;
				}
			}

			values.emplace_back(uniform.id, object);
			slotNames.emplace_back(slotName);
		}

		u32 Count()
		{
			return (u32)values.size();
		}

		iter begin()
		{
			return values.begin();
		}

		iter end()
		{
			return values.end();
		}

		const_iter cbegin()
		{
			return values.cbegin();
		}

		const_iter cend()
		{
			return values.cend();
		}

		bool Contains(const Uniform& uniform) const
		{
			for (const auto& pair : values)
			{
				if (pair.uniformID == uniform.id)
				{
					return true;
				}
			}
			return false;
		}

		UniformType operator[](const Uniform& uniform)
		{
			for (const auto& pair : values)
			{
				if (pair.uniformID == uniform.id)
				{
					return pair.object;
				}
			}
			return nullptr;
		}

		std::vector<TexPair> values;
		std::vector<std::string> slotNames;
	};

	struct Material
	{
		struct PushConstantBlock;

		Material() {};

		virtual ~Material()
		{
			if (pushConstantBlock)
			{
				delete pushConstantBlock;
				pushConstantBlock = nullptr;
			}
		}

		Material(const Material& rhs)
		{
			if (rhs.pushConstantBlock)
			{
				pushConstantBlock = new PushConstantBlock(*rhs.pushConstantBlock);
			}
		}

		Material(const Material&&) = delete;
		Material& operator=(const Material&) = delete;
		Material& operator=(const Material&&) = delete;

		bool Equals(const Material& other);

		static void ParseJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut);
		JSONObject Serialize() const;

		static std::vector<MaterialID> ParseMaterialArrayJSON(const JSONObject& object, i32 fileVersion);

		std::string name;
		ShaderID shaderID = InvalidShaderID;
		std::string normalTexturePath;

		// GBuffer samplers
		std::vector<Pair<std::string, void*>> sampledFrameBuffers;

		glm::vec2 cubemapSamplerSize = VEC2_ZERO;

		// PBR constants
		glm::vec4 constAlbedo = VEC4_ONE;
		glm::vec4 constEmissive = VEC4_ONE;
		real constMetallic = 0.0f;
		real constRoughness = 0.0f;
		std::string albedoTexturePath;
		std::string emissiveTexturePath;
		std::string metallicTexturePath;
		std::string roughnessTexturePath;
		std::string hdrEquirectangularTexturePath;
		glm::vec2 irradianceSamplerSize = VEC2_ZERO;
		std::string environmentMapPath;
		glm::vec2 prefilteredMapSize = VEC2_ZERO;
		glm::vec4 colourMultiplier = VEC4_ONE;

		bool enableNormalSampler = false;

		bool generateCubemapSampler = false;
		bool enableCubemapSampler = false;

		// PBR samplers
		bool enableAlbedoSampler = false;
		bool enableEmissiveSampler = false;
		bool enableMetallicSampler = false;
		bool enableRoughnessSampler = false;

		bool generateHDREquirectangularSampler = false;
		bool enableHDREquirectangularSampler = false;

		bool enableCubemapTrilinearFiltering = false;
		bool generateHDRCubemapSampler = false;

		bool generateIrradianceSampler = false;

		bool enablePrefilteredMap = false;
		bool generatePrefilteredMap = false;

		bool enableBRDFLUT = false;
		bool renderToCubemap = false; // NOTE: This flag is currently ignored by GL renderer!

		bool generateReflectionProbeMaps = false;

		bool persistent = false;
		bool visibleInEditor = false;

		bool bSerializable = true;

		bool bDynamic = false;
		u32 dynamicVertexIndexBufferIndex = 0;

		real textureScale = 1.0f;
		real blendSharpness = 1.0f;

		glm::vec4 fontCharData;
		glm::vec2 texSize;

		// TODO: Store TextureIDs here
		ShaderUniformContainer<Texture*> textures;

		struct PushConstantBlock
		{
			PushConstantBlock(i32 initialSize) : size(initialSize) { assert(initialSize != 0); }
			PushConstantBlock() {}

			PushConstantBlock(const PushConstantBlock& rhs)
			{
				data = rhs.data;
				size = rhs.size;
			}
			PushConstantBlock(const PushConstantBlock&& rhs)
			{
				data = rhs.data;
				size = rhs.size;
			}
			PushConstantBlock& operator=(const PushConstantBlock& rhs)
			{
				data = rhs.data;
				size = rhs.size;
				return *this;
			}
			PushConstantBlock& operator=(const PushConstantBlock&& rhs)
			{
				data = rhs.data;
				size = rhs.size;
				return *this;
			}

			~PushConstantBlock()
			{
				if (data)
				{
					free(data);
					data = nullptr;
					size = 0;
				}
			}

			void InitWithSize(u32 dataSize)
			{
				if (data == nullptr)
				{
					assert(size == dataSize || size == 0);

					size = dataSize;
					if (dataSize != 0)
					{
						data = malloc(dataSize);
					}
				}
				else
				{
					assert(size == dataSize && "Attempted to initialize push constant data with differing size. Block must be reallocated when size changes.");
				}
			}

			void SetData(real* newData, u32 dataSize)
			{
				InitWithSize(dataSize);
				memcpy(data, newData, size);
			}

			void SetData(const std::vector<Pair<void*, u32>>& dataList)
			{
				i32 dataSize = 0;
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

			void SetData(const glm::mat4& viewProj)
			{
				const i32 dataSize = sizeof(glm::mat4) * 1;
				InitWithSize(dataSize);

				real* dst = (real*)data;
				memcpy(dst, &viewProj, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
			}

			void SetData(const glm::mat4& view, const glm::mat4& proj)
			{
				const i32 dataSize = sizeof(glm::mat4) * 2;
				InitWithSize(dataSize);

				real* dst = (real*)data;
				memcpy(dst, &view, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
				memcpy(dst, &proj, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
			}

			void SetData(const glm::mat4& view, const glm::mat4& proj, i32 textureIndex)
			{
				const i32 dataSize = sizeof(glm::mat4) * 2 + sizeof(i32);
				if (data == nullptr)
				{
					assert(size == dataSize || size == 0);

					size = dataSize;
					data = malloc(dataSize);
					assert(data != nullptr);
				}
				else
				{
					assert(size == dataSize && "Attempted to set push constant data with differing size. Block must be reallocated.");
				}
				real* dst = (real*)data;
				memcpy(dst, &view, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
				memcpy(dst, &proj, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
				memcpy(dst, &textureIndex, sizeof(i32)); dst += sizeof(i32) / sizeof(real);
			}

			void* data = nullptr;
			u32 size = 0;
			// TODO: Store stage flags
		};
		PushConstantBlock* pushConstantBlock = nullptr;
	};

	enum class RenderPassType
	{
		SHADOW,
		DEFERRED,
		DEFERRED_COMBINE,
		FORWARD,
		SSAO,
		SSAO_BLUR,
		POST_PROCESS,
		TAA_RESOLVE,
		GAMMA_CORRECT,
		UI,

		COMPUTE_PARTICLES,

		_NONE
	};

	struct RenderObjectCreateInfo
	{
		MaterialID materialID = InvalidMaterialID;

		VertexBufferData* vertexBufferData = nullptr;
		std::vector<u32>* indices = nullptr;

		GameObject* gameObject = nullptr;

		DepthTestFunc depthTestReadFunc = DepthTestFunc::GEQUAL;
		CullFace cullFace = CullFace::BACK;
		RenderPassType renderPassOverride = RenderPassType::_NONE;

		bool visible = true;
		bool visibleInSceneExplorer = true;
		bool bEditorObject = false;
		bool bSetDynamicStates = false;
		bool bIndexed = false;
		bool bAllowDynamicBufferShrinking = true;
	};

	struct UniformList
	{
		bool HasUniform(const Uniform& uniform) const;
		void AddUniform(const Uniform& uniform);
		u32 GetSizeInBytes() const;

		std::set<StringID> uniforms;
		u32 totalSizeInBytes = 0;
	};

	struct ShaderInfo
	{
		std::string name;
		std::string inVertexShaderFilePath;
		std::string inFragmentShaderFilePath;
		std::string inGeometryShaderFilePath;
		std::string inComputeShaderFilePath;
	};

	struct Shader
	{
		Shader(const ShaderInfo& shaderInfo);
		Shader(const std::string& name,
			const std::string& inVertexShaderFilePath,
			const std::string& inFragmentShaderFilePath = "",
			const std::string& inGeometryShaderFilePath = "",
			const std::string& inComputeShaderFilePath = "");

		virtual ~Shader() {};

		Shader(const Shader&) = delete;
		Shader(const Shader&&) = delete;
		Shader& operator=(const Shader&) = delete;
		Shader& operator=(const Shader&&) = delete;

		std::string name;

		std::string vertexShaderFilePath = "";
		std::string fragmentShaderFilePath = "";
		std::string geometryShaderFilePath = "";
		std::string computeShaderFilePath = "";

		std::vector<char> vertexShaderCode = {};
		std::vector<char> fragmentShaderCode = {};
		std::vector<char> geometryShaderCode = {};
		std::vector<char> computeShaderCode = {};

		UniformList constantBufferUniforms = {};
		UniformList dynamicBufferUniforms = {};
		UniformList additionalBufferUniforms = {};
		UniformList textureUniforms = {};

		VertexAttributes vertexAttributes = 0;
		i32 numAttachments = 1;

		// Specifies how many objects to allocate dynamic uniform buffer room for (per material)
		i32 maxObjectCount = -1;

		i32 subpass = 0;
		bool bDepthWriteEnable = true;
		bool bTranslucent = false;
		bool bCompute = false;

		bool bNeedPushConstantBlock = false;
		bool bGenerateVertexBufferForAll = false;
		bool bTextureArr = false;
		u32 pushConstantBlockSize = 0;

		u32 dynamicVertexBufferSize = 0; // TODO: Define through materials

		RenderPassType renderPassType = RenderPassType::_NONE;

		u32 staticVertexBufferIndex = 0;
	};

	struct SpriteQuadDrawInfo
	{
		TextureID textureID = InvalidTextureID; // If left invalid, blankTextureID will be used
		u32 textureLayer = 0;
		u32 FBO = 0; // 0 for rendering to final RT
		u32 RBO = 0; // 0 for rendering to final RT
		MaterialID materialID = InvalidMaterialID;
		glm::vec3 pos = VEC3_ZERO;
		glm::quat rotation = QUAT_IDENTITY;
		glm::vec3 scale = VEC3_ONE;
		AnchorPoint anchor = AnchorPoint::TOP_LEFT;
		glm::vec4 colour = VEC4_ONE;

		bool bScreenSpace = true;
		bool bReadDepth = true;
		bool bEnableAlbedoSampler = true;
		bool bRaw = false; // If true no further pos/scale processing is down, values are directly uploaded to GPU
		bool bFullscreen = false;
	};

	// TODO: OPTIMIZE: Shrink these bad boys
	// 52 bytes
	struct TextVertex2D
	{
		glm::vec2 pos;                        // 0
		glm::vec2 uv;                         // 8
		glm::vec4 colour;                      // 16
		glm::vec4 charSizePixelsCharSizeNorm; // 32 - RG: char size in pixels, BA: char size in [0, 1] in screen space
		i32 channel;                          // 48 - Uses extra int slot
	};

	// 68 bytes
	struct TextVertex3D
	{
		glm::vec3 pos;                        // 0
											  // + 4
		glm::vec2 uv;                         // 16
		glm::vec4 colour;                      // 24
		glm::vec3 tangent;                    // 32
											  // + 4
		glm::vec4 charSizePixelsCharSizeNorm; // 48 - RG: char size in pixels, BA: char size in [0, 1] in screen space
		i32 channel;                          // 64 - uses extra int slot
	};

	struct RenderObjectBatch
	{
		std::vector<RenderID> objects;
	};

	struct MaterialBatchPair
	{
		MaterialID materialID = InvalidMaterialID;
		RenderObjectBatch batch;
	};

	struct MaterialBatch
	{
		// One per material
		std::vector<MaterialBatchPair> batches;
	};

	struct ShaderBatchPair
	{
		ShaderID shaderID = InvalidShaderID;
		bool bDynamic = false;
		MaterialBatch batch;
	};

	struct ShaderBatch
	{
		// One per shader
		std::vector<ShaderBatchPair> batches;
	};

	struct UniformOverrides
	{
		UniformList overridenUniforms;

		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 viewProjection;
		glm::vec4 camPos;
		glm::mat4 model;
		glm::mat4 modelInvTranspose;
		u32 enableAlbedoSampler;
		u32 enableMetallicSampler;
		u32 enableRoughnessSampler;
		u32 enableNormalSampler;
		i32 texChannel;
		glm::vec4 sdfData;
		glm::vec4 fontCharData;
		glm::vec2 texSize;
		glm::vec4 colourMultiplier;
		bool bSSAOVerticalPass;
		ParticleSimData* particleSimData = nullptr;
	};

	struct DeviceDiagnosticCheckpoint
	{
		char name[48];
	};
} // namespace flex
