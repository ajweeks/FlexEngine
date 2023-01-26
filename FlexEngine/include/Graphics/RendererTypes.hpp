#pragma once

#include <array>
#include <map>

IGNORE_WARNINGS_PUSH
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
IGNORE_WARNINGS_POP

#include "Helpers.hpp"
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
	static const i32 MAX_SSAO_KERNEL_SIZE = 64;
	static const i32 MAX_NUM_ROAD_SEGMENTS = 64;
	static const i32 MAX_NUM_OVERLAPPING_SEGMENTS_PER_CHUNK = 8;
	static const i32 MAX_BIOME_COUNT = 16; // Must be multiple of 16
	static const u32 BIOME_NOISE_FUNCTION_INT4_COUNT = MAX_BIOME_COUNT / 16;
	static const i32 MAX_NUM_NOISE_FUNCTIONS_PER_BIOME = 4;
	static const u32 TERRAIN_THREAD_GROUP_SIZE = 4;
	static const i32 MAX_NUM_PARTICLE_PARAMS = 16;

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

	// 1028 bytes
	struct SSAOGenData
	{
		glm::vec4 samples[MAX_SSAO_KERNEL_SIZE];	// 0
		real radius;								// 1024
	};

	// 4 bytes
	struct SSAOBlurDataConstant
	{
		i32 radius;	// 0
	};

	// 8 bytes
	struct SSAOBlurDataDynamic
	{
		glm::vec2 ssaoTexelOffset;	// 0
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

	// 288 bytes
	struct ShadowSamplingData
	{
		glm::mat4 cascadeViewProjMats[MAX_SHADOW_CASCADE_COUNT];
		glm::vec4 cascadeDepthSplits;
		real pad[3];
		real baseBias;
	};

	// 112 bytes
	struct SHCoeffs
	{
		glm::vec4 r0;	// 0
		glm::vec4 g0;	// 16
		glm::vec4 b0;	// 32
		glm::vec4 r1;	// 48
		glm::vec4 g1;	// 64
		glm::vec4 b1;	// 80
		glm::vec4 rgb2;	// 96
	};

#pragma pack(push, 1)
	// 56 bytes
	struct ParticleBufferData
	{
		glm::vec3 pos;
		glm::vec3 vel;
		glm::vec4 colour;
		glm::vec3 scale;
		glm::vec4 extraVec4;
	};
#pragma pack(pop)

	struct ParticleParamGPU
	{
		glm::vec4 valueMin;
		glm::vec4 valueMax;
		u32 sampleType;
		u32 pad0, pad1, pad2;
	};

	struct ParticleSpawnParams
	{
		ParticleParamGPU params[MAX_NUM_PARTICLE_PARAMS];
	};

	struct ParticleSimData
	{
		u32 bufferOffset;
		u32 particleCount;
		real dt;
		u32 enableSpawning;
		ParticleSpawnParams spawnParams;
	};

	// 80 bytes
	struct OceanData
	{
		glm::vec4 top;				// 0
		glm::vec4 mid;				// 16
		glm::vec4 btm;				// 32
		real fresnelFactor;			// 48
		real fresnelPower;			// 52
		real skyReflectionFactor;	// 56
		real fogFalloff;			// 60
		real fogDensity;			// 64
		real pad[3];				// 68
	};

	// 64 bytes
	struct SkyboxData
	{
		glm::vec4 top;	// 0
		glm::vec4 mid;	// 16
		glm::vec4 btm;	// 32
		glm::vec4 fog;	// 48
	};

	// 64 bytes
	struct BezierCurve3D_GPU
	{
		glm::vec4 points[4]; // 0
	};

	// 96 bytes
	struct RoadSegment_GPU
	{
		BezierCurve3D_GPU curve;	// 0
		AABB aabb;					//
		real widthStart;			//
		real widthEnd;				//
	};

	bool operator==(const RoadSegment_GPU& lhs, const RoadSegment_GPU& rhs);

	// 32 bytes
	struct NoiseFunction_GPU
	{
		u32 type;					 // 0
		real baseFeatureSize;		 // 4
		i32 numOctaves;				 // 8
		real H;						 // 12
		real heightScale;			 // 16
		real lacunarity;			 // 20
		real wavelength;			 // 24
		real sharpness;				 // 28
		//i32 isolateOctave = -1;		 // 32
		// TODO: Seed
	};

	// 128 bytes
	struct Biome_GPU
	{
		NoiseFunction_GPU noiseFunctions[MAX_NUM_NOISE_FUNCTIONS_PER_BIOME];	// 0
	};

	struct TerrainGenConstantData
	{
		real chunkSize;											// 0
		real maxHeight;											// 4
		real roadBlendDist;										// 8
		real roadBlendThreshold;								// 12
		u32 vertCountPerChunkAxis;								// 16
		i32 isolateNoiseLayer; // default: -1					// 20
		u32 biomeCount;											// 24
		u32 randomTablesSize;									// 28
		u32 numPointsPerAxis;
		real isoLevel;
		real _pad0, _pad1;
		Biome_GPU biomes[MAX_BIOME_COUNT];						// (2,048 bytes)
		NoiseFunction_GPU biomeNoise;							//
		glm::uvec4 biomeNoiseFunctionCounts[BIOME_NOISE_FUNCTION_INT4_COUNT]; // Each element stores 16 values (one per byte)
		//RoadSegment_GPU roadSegments[MAX_NUM_ROAD_SEGMENTS];	// (6,144 bytes)
		//i32 overlappingRoadSegmentIndices[MAX_NUM_ROAD_SEGMENTS][MAX_NUM_OVERLAPPING_SEGMENTS_PER_CHUNK]; // (8,192 bytes)
	};

	struct TerrainVertex
	{
		glm::vec3 posA;
		glm::vec4 colourA;
		glm::vec3 normA;
		glm::vec3 posB;
		glm::vec4 colourB;
		glm::vec3 normB;
		glm::vec3 posC;
		glm::vec4 colourC;
		glm::vec3 normC;
	};

	// 16 bytes
	struct TerrainGenDynamicData
	{
		glm::ivec3 chunkIndex;
		u32 linearIndex;
	};

	// 16 bytes
	//struct TerrainGenPostProcessConstantData
	//{
	//	real chunkSize;				// 0
	//	real blendRadius;			// 4
	//	u32 vertCountPerChunkAxis;	// 8
	//	u32 vertexBufferSize;		// 12
	//};

	// 16 bytes
	struct TerrainGenPostProcessDynamicData
	{
		u32 linearIndexN;	// 0
		u32 linearIndexE;	// 4
		u32 linearIndexS;	// 8
		u32 linearIndexW;	// 12
	};

	struct Uniform
	{
		explicit Uniform(const char* uniformName, StringID id, u64 size = 0);

		Uniform(const Uniform&) = delete;
		Uniform(Uniform&&) = delete;
		Uniform& operator=(const Uniform&) = delete;
		Uniform& operator=(Uniform&&) = delete;

		StringID id;
		u32 size;
		const char* DBG_name;
	};

	void RegisterUniform(StringID uniformNameSID, Uniform* uniform);
	Uniform const* UniformFromStringID(StringID uniformNameSID);

#define UNIFORM(val) val, SID(val)

	static const Uniform U_MODEL(UNIFORM("model"), sizeof(glm::mat4)); // TODO: Rename to OBJECT_TO_WORLD
	static const Uniform U_VIEW(UNIFORM("view"), sizeof(glm::mat4));
	static const Uniform U_VIEW_INV(UNIFORM("invView"), sizeof(glm::mat4));
	static const Uniform U_VIEW_PROJECTION(UNIFORM("viewProjection"), sizeof(glm::mat4));
	static const Uniform U_PROJECTION(UNIFORM("projection"), sizeof(glm::mat4));
	static const Uniform U_PROJECTION_INV(UNIFORM("invProj"), sizeof(glm::mat4));
	static const Uniform U_BLEND_SHARPNESS(UNIFORM("blendSharpness"), sizeof(real));
	static const Uniform U_COLOUR_MULTIPLIER(UNIFORM("colourMultiplier"), sizeof(glm::vec4));
	static const Uniform U_CAM_POS(UNIFORM("camPos"), sizeof(glm::vec4));
	static const Uniform U_POINT_LIGHTS(UNIFORM("pointLights"), sizeof(PointLightData)* MAX_POINT_LIGHT_COUNT);
	static const Uniform U_SPOT_LIGHTS(UNIFORM("spotLights"), sizeof(SpotLightData)* MAX_SPOT_LIGHT_COUNT);
	static const Uniform U_AREA_LIGHTS(UNIFORM("areaLights"), sizeof(AreaLightData)* MAX_AREA_LIGHT_COUNT);
	static const Uniform U_DIR_LIGHT(UNIFORM("dirLight"), (u32)sizeof(DirLightData));
	static const Uniform U_ALBEDO_SAMPLER(UNIFORM("albedoSampler"));
	static const Uniform U_CONST_ALBEDO(UNIFORM("constAlbedo"), (u32)sizeof(glm::vec4));
	static const Uniform U_METALLIC_SAMPLER(UNIFORM("metallicSampler"));
	static const Uniform U_CONST_METALLIC(UNIFORM("constMetallic"), sizeof(real));
	static const Uniform U_ROUGHNESS_SAMPLER(UNIFORM("roughnessSampler"));
	static const Uniform U_CONST_ROUGHNESS(UNIFORM("constRoughness"), sizeof(real));
	static const Uniform U_CONST_EMISSIVE(UNIFORM("constEmissive"), sizeof(glm::vec4));
	static const Uniform U_NORMAL_SAMPLER(UNIFORM("normalSampler"));
	static const Uniform U_ENABLE_ALBEDO_SAMPLER(UNIFORM("enableAlbedoSampler"), sizeof(i32));
	static const Uniform U_ENABLE_METALLIC_SAMPLER(UNIFORM("enableMetallicSampler"), sizeof(i32));
	static const Uniform U_ENABLE_ROUGHNESS_SAMPLER(UNIFORM("enableRoughnessSampler"), sizeof(i32));
	static const Uniform U_ENABLE_NORMAL_SAMPLER(UNIFORM("enableNormalSampler"), sizeof(i32));
	static const Uniform U_EMISSIVE_SAMPLER(UNIFORM("emissiveSampler"));
	static const Uniform U_CUBEMAP_SAMPLER(UNIFORM("cubemapSampler"));
	static const Uniform U_IRRADIANCE_SAMPLER(UNIFORM("irradianceSampler"));
	static const Uniform U_FB_0_SAMPLER(UNIFORM("normalRoughnessSampler"));
	static const Uniform U_FB_1_SAMPLER(UNIFORM("albedoMetallicSampler"));
	static const Uniform U_ENABLE_EMISSIVE_SAMPLER(UNIFORM("enableEmissiveSampler"), sizeof(i32));
	static const Uniform U_HDR_EQUIRECTANGULAR_SAMPLER(UNIFORM("hdrEquirectangular"));
	static const Uniform U_BRDF_LUT_SAMPLER(UNIFORM("brdfLUT"));
	static const Uniform U_PREFILTER_MAP(UNIFORM("prefilterMap"));
	static const Uniform U_EXPOSURE(UNIFORM("exposure"), sizeof(real));
	static const Uniform U_FONT_CHAR_DATA(UNIFORM("fontCharData"), sizeof(glm::vec4));
	static const Uniform U_TEX_SIZE(UNIFORM("texSize"), sizeof(glm::vec2));
	static const Uniform U_TEXTURE_SCALE(UNIFORM("textureScale"), sizeof(real));
	static const Uniform U_TEX_CHANNEL(UNIFORM("texChannel"), sizeof(i32));
	static const Uniform U_UNIFORM_BUFFER_CONSTANT(UNIFORM("uniformBufferConstant")); // TODO: Infer this
	static const Uniform U_UNIFORM_BUFFER_DYNAMIC(UNIFORM("uniformBufferDynamic")); // TODO: Infer this
	static const Uniform U_TIME(UNIFORM("time"), sizeof(glm::vec4));
	static const Uniform U_SDF_DATA(UNIFORM("sdfData"), sizeof(glm::vec4));
	static const Uniform U_HIGH_RES_TEX(UNIFORM("highResTex"));
	static const Uniform U_DEPTH_SAMPLER(UNIFORM("depthSampler"));
	static const Uniform U_NOISE_SAMPLER(UNIFORM("noiseSampler"));
	static const Uniform U_SSAO_RAW_SAMPLER(UNIFORM("ssaoRawSampler"));
	static const Uniform U_SSAO_FINAL_SAMPLER(UNIFORM("ssaoFinalSampler"));
	static const Uniform U_SSAO_NORMAL_SAMPLER(UNIFORM("ssaoNormalSampler")); // TODO: Use normalSampler uniform?
	static const Uniform U_SSAO_GEN_DATA(UNIFORM("ssaoGenData"), sizeof(SSAOGenData));
	static const Uniform U_SSAO_BLUR_DATA_DYNAMIC(UNIFORM("ssaoBlurDataDynamic"), sizeof(SSAOBlurDataDynamic));
	static const Uniform U_SSAO_BLUR_DATA_CONSTANT(UNIFORM("ssaoBlurDataConstant"), sizeof(SSAOBlurDataConstant));
	static const Uniform U_SSAO_SAMPLING_DATA(UNIFORM("ssaoData"), sizeof(SSAOSamplingData));
	static const Uniform U_LTC_MATRICES_SAMPLER(UNIFORM("ltcMatricesSampler"));
	static const Uniform U_LTC_AMPLITUDES_SAMPLER(UNIFORM("ltcAmplitudesSampler"));
	static const Uniform U_SHADOW_CASCADES_SAMPLER(UNIFORM("shadowCascadeSampler"));
	static const Uniform U_SHADOW_SAMPLING_DATA(UNIFORM("shadowSamplingData"), sizeof(ShadowSamplingData));
	static const Uniform U_NEAR_FAR_PLANES(UNIFORM("nearFarPlanes"), sizeof(glm::vec2));
	static const Uniform U_POST_PROCESS_MAT(UNIFORM("postProcessMatrix"), sizeof(glm::mat4));
	static const Uniform U_SCENE_SAMPLER(UNIFORM("sceneSampler"));
	static const Uniform U_HISTORY_SAMPLER(UNIFORM("historySampler"));
	static const Uniform U_LAST_FRAME_VIEWPROJ(UNIFORM("lastFrameViewProj"), sizeof(glm::mat4));
	static const Uniform U_PARTICLE_BUFFER(UNIFORM("particleBuffer"), sizeof(ParticleBufferData));
	static const Uniform U_PARTICLE_SIM_DATA(UNIFORM("particleSimData"), sizeof(ParticleSimData));
	static const Uniform U_OCEAN_DATA(UNIFORM("oceanData"), sizeof(OceanData));
	static const Uniform U_SKYBOX_DATA(UNIFORM("skyboxData"), sizeof(SkyboxData));
	static const Uniform U_UV_BLEND_AMOUNT(UNIFORM("uvBlendAmount"), sizeof(glm::vec2));
	static const Uniform U_SCREEN_SIZE(UNIFORM("screenSize"), sizeof(glm::vec4)); // window (w, h, 1/w, 1/h)
	static const Uniform U_TERRAIN_GEN_CONSTANT_DATA(UNIFORM("terrainGenConstantData"), sizeof(TerrainGenConstantData));
	static const Uniform U_TERRAIN_GEN_DYNAMIC_DATA(UNIFORM("terrainGenDynamicData"), sizeof(TerrainGenDynamicData));
	static const Uniform U_TERRAIN_POINT_BUFFER(UNIFORM("terrainPointBuffer"));
	static const Uniform U_TERRAIN_VERTEX_BUFFER(UNIFORM("terrainVertexBuffer"));
	static const Uniform U_RANDOM_TABLES(UNIFORM("randomTables"));
	static const Uniform U_CHARGE_AMOUNT(UNIFORM("chargeAmount"), sizeof(real));

#undef UNIFORM

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
		glm::vec4 constAlbedo = VEC4_ONE;
		glm::vec4 constEmissive = VEC4_ZERO;
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
		bool bEditorMaterial = false;
	};

	struct Texture
	{
		Texture() = default;
		Texture(const std::string& name);

		virtual ~Texture() = default;

		Texture(const Texture&) = delete;
		Texture(Texture&&) = delete;
		Texture& operator=(const Texture&) = delete;
		Texture& operator=(Texture&&) = delete;

		virtual void Reload() = 0;

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
	};

	template<typename UniformType>
	struct ShaderUniformContainer
	{
		struct TexPair
		{
			TexPair(Uniform const* uniform, UniformType object) :
				uniform(uniform),
				object(object)
			{}

			Uniform const* uniform;
			UniformType object;
		};

		using iter = typename std::vector<TexPair>::iterator;
		using const_iter = typename std::vector<TexPair>::const_iterator;

		void SetUniform(Uniform const* uniform, const UniformType object)
		{
			PROFILE_AUTO("ShaderUniformContainer SetUniform");
			for (u32 i = 0; i < (u32)values.size(); ++i)
			{
				if (values[i].uniform->id == uniform->id)
				{
					values[i].object = object;
					return;
				}
			}

			values.emplace_back(uniform, object);
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

		bool Contains(Uniform const* uniform) const
		{
			PROFILE_AUTO("ShaderUniformContainer Contains");
			for (const auto& pair : values)
			{
				if (pair.uniform->id == uniform->id)
				{
					return true;
				}
			}
			return false;
		}

		UniformType operator[](Uniform const* uniform)
		{
			for (const auto& pair : values)
			{
				if (pair.uniform->id == uniform->id)
				{
					return pair.object;
				}
			}
			return nullptr;
		}

		std::vector<TexPair> values;
	};

	struct UniformList
	{
		bool HasUniform(Uniform const* uniform) const;
		bool HasUniform(const StringID& uniformID) const;
		void AddUniform(Uniform const* uniform);
		u32 GetSizeInBytes() const;

		std::vector<Uniform const*> uniforms;
		u32 totalSizeInBytes = 0;
	};

	enum class GPUBufferType
	{
		STATIC,
		DYNAMIC,
		PARTICLE_DATA,
		TERRAIN_POINT_BUFFER,
		TERRAIN_VERTEX_BUFFER,

		_NONE
	};

	struct UniformBufferObjectData
	{
		u8* data = nullptr;
		u32 unitSize = 0; // Size of each buffer instance (per object)
	};

	struct GPUBuffer
	{
		GPUBuffer(GPUBufferType type, const std::string& debugName);
		virtual ~GPUBuffer();

		GPUBuffer(const GPUBuffer&) = delete;
		GPUBuffer(GPUBuffer&& other) = delete;
		GPUBuffer& operator=(const GPUBuffer&) = delete;
		GPUBuffer& operator=(GPUBuffer&&) = delete;

		void AllocHostMemory(u32 size, u32 alignment = u32_max);
		void FreeHostMemory();

		GPUBufferID ID = InvalidGPUBufferID;
		u32 fullDynamicBufferSize = 0;
		UniformBufferObjectData data;
		std::string debugName;

		GPUBufferType type = GPUBufferType::_NONE;
	};

	struct SpecializationInfoType
	{
		std::string name;
		u32 id;
		i32 defaultValut;
	};

	struct GPUBufferList
	{
		~GPUBufferList();

		void Add(GPUBufferType type, const std::string& debugName);
		GPUBuffer* Get(GPUBufferType type);
		const GPUBuffer* Get(GPUBufferType type) const;
		bool Has(GPUBufferType type) const;

		std::vector<GPUBuffer*> bufferList;
	};

	struct MaterialPropertyOverride
	{
		enum class ValueType
		{
			REAL,
			UINT,
			INT,
			BOOL,
			VEC2,
			VEC3,
			VEC4,
			MAT4,
			VOID_STAR,
			_NONE
		};

		MaterialPropertyOverride(const MaterialPropertyOverride& other)
		{
			memcpy(this, &other, sizeof(MaterialPropertyOverride));
		}
		MaterialPropertyOverride(MaterialPropertyOverride&& other)
		{
			memcpy(this, &other, sizeof(MaterialPropertyOverride));
		}
		void operator=(const MaterialPropertyOverride& other)
		{
			memcpy(this, &other, sizeof(MaterialPropertyOverride));
		}
		void operator=(MaterialPropertyOverride&& other)
		{
			memcpy(this, &other, sizeof(MaterialPropertyOverride));
		}

		MaterialPropertyOverride() : i32Value(0), valueType(ValueType::_NONE) {}
		MaterialPropertyOverride(real realValue) : realValue(realValue), valueType(ValueType::REAL) {}
		MaterialPropertyOverride(u32 u32Value) : u32Value(u32Value), valueType(ValueType::UINT) {}
		MaterialPropertyOverride(i32 i32Value) : i32Value(i32Value), valueType(ValueType::INT) {}
		MaterialPropertyOverride(bool boolValue) : boolValue(boolValue), valueType(ValueType::BOOL) {}
		MaterialPropertyOverride(const glm::vec2& vec2Value) : vec2Value(vec2Value), valueType(ValueType::VEC2) {}
		MaterialPropertyOverride(const glm::vec3& vec3Value) : vec3Value(vec3Value), valueType(ValueType::VEC3) {}
		MaterialPropertyOverride(const glm::vec4& vec4Value) : vec4Value(vec4Value), valueType(ValueType::VEC4) {}
		MaterialPropertyOverride(const glm::mat4& mat4Value) : mat4Value(mat4Value), valueType(ValueType::MAT4) {}
		MaterialPropertyOverride(void* pointerValue) : pointerValue(pointerValue), valueType(ValueType::VOID_STAR) {}

		void* GetDataPointer()
		{
			if (valueType == ValueType::VOID_STAR)
			{
				return pointerValue;
			}
			return &realValue;
		}

		union {
			real realValue;
			u32 u32Value;
			i32 i32Value;
			bool boolValue;
			glm::vec2 vec2Value;
			glm::vec3 vec3Value;
			glm::vec4 vec4Value;
			glm::mat4 mat4Value;
			void* pointerValue;
		};
		ValueType valueType;
	};

	struct UniformOverrides
	{
		using UniformPair = Pair<Uniform const*, MaterialPropertyOverride>;

		void AddUniform(Uniform const* uniform, const MaterialPropertyOverride& propertyOverride);
		bool HasUniform(Uniform const* uniform) const;
		bool HasUniform(Uniform const* uniform, MaterialPropertyOverride& outPropertyOverride) const;

		void Clear();

		std::map<StringID, UniformPair> overrides;
	};

	struct Material
	{
		struct PushConstantBlock;

		Material() = default;
		virtual ~Material();
		explicit Material(const Material& rhs);
		Material(Material&&) = delete;
		Material& operator=(const Material&) = delete;
		Material& operator=(Material&&) = delete;

		bool Equals(const Material& other);

		static void ParseJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut, i32 fileVersion);
		FLEX_NO_DISCARD JSONObject Serialize() const;

		static FLEX_NO_DISCARD std::vector<MaterialID> ParseMaterialArrayJSON(const JSONObject& object, i32 fileVersion);

		std::string name;
		ShaderID shaderID = InvalidShaderID;
		std::string normalTexturePath;

		// GBuffer samplers
		std::vector<Pair<std::string, void*>> sampledFrameBuffers;

		// TODO: OPTIMIZE: MEMORY: Only store dynamic buffers here, store constant buffers in shader/globally
		GPUBufferList gpuBufferList;

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
		bool renderToCubemap = false;

		bool generateReflectionProbeMaps = false;

		bool persistent = false;
		bool bEditorMaterial = false;

		bool bSerializable = true;

		bool bDynamic = false;

		real textureScale = 1.0f;
		real blendSharpness = 1.0f;

		glm::vec4 fontCharData;
		glm::vec2 texSize;

		// TODO: Replace all above fields using overrides
		UniformOverrides uniformOverrides;

		// TODO: Store TextureIDs here
		ShaderUniformContainer<Texture*> textures;

		struct PushConstantBlock final
		{
			PushConstantBlock() = default;
			PushConstantBlock(i32 initialSize);

			PushConstantBlock(const PushConstantBlock& rhs);
			PushConstantBlock(PushConstantBlock&& rhs);
			PushConstantBlock& operator=(const PushConstantBlock& rhs);
			PushConstantBlock& operator=(PushConstantBlock&& rhs);

			~PushConstantBlock();

			void InitWithSize(u32 dataSize);
			void SetData(real* newData, u32 dataSize);
			void SetData(const std::vector<Pair<void*, u32>>& dataList);
			void SetData(const glm::mat4& viewProj);
			void SetData(const glm::mat4& view, const glm::mat4& proj);
			void SetData(const glm::mat4& view, const glm::mat4& proj, i32 textureIndex);

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
		COMPUTE_TERRAIN,

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
		TopologyMode topologyMode = TopologyMode::TRIANGLE_LIST;

		bool visible = true;
		bool visibleInSceneExplorer = true;
		bool bEditorObject = false;
		bool bSetDynamicStates = false;
		bool bIndexed = false;
		bool bAllowDynamicBufferShrinking = true;
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

		virtual ~Shader() = default;

		Shader(const Shader&) = delete;
		Shader(Shader&&) = delete;
		Shader& operator=(const Shader&) = delete;
		Shader& operator=(Shader&&) = delete;

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

		u32 dynamicVertexIndexBufferIndex = 0;

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

	static const u32 Z_ORDER_UI = 50;

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
		u32 zOrder = 25; // Dictates ordering of sprites, [0, Z_ORDER_UI): renders before UI, [Z_ORDER_UI, 100+]: renders after UI

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
		glm::vec4 colour;                     // 16
		glm::vec4 charSizePixelsCharSizeNorm; // 32 - RG: char size in pixels, BA: char size in [0, 1] in screen space
		i32 channel;                          // 48 - Uses extra int slot
	};

	// 68 bytes
	struct TextVertex3D
	{
		glm::vec3 pos;                        // 0
											  // + 4
		glm::vec2 uv;                         // 16
		glm::vec4 colour;                     // 24
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

	struct DeviceDiagnosticCheckpoint
	{
		char name[48];
	};

	struct SpecializationConstantMetaData
	{
		SpecializationConstantID id;
		i32 value;
		// Editor-only:
		i32 min;
		i32 max;
	};

	i32 DebugOverlayNameToID(const char* DebugOverlayName);
} // namespace flex
