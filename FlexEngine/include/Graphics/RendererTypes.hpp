#pragma once

#include <array>
#include <atomic> // For atomic_uint32_t
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
		explicit Uniform(const char* uniformName, StringID id, u64 size, u32 index);

		Uniform(const Uniform&) = delete;
		Uniform(Uniform&&) = delete;
		Uniform& operator=(const Uniform&) = delete;
		Uniform& operator=(Uniform&&) = delete;

		StringID id;
		u32 size;
		u32 index;
		const char* DBG_name;
	};

	void RegisterUniform(StringID uniformNameSID, Uniform* uniform);
	Uniform const* UniformFromStringID(StringID uniformNameSID);

#define UNIFORMS_START static constexpr u32 s_UniformsStart = __LINE__;
#define DECL_UNIFORM(var, name, size) \
	static constexpr StringID var##_ID = SID(name); \
	static const Uniform var(name, var##_ID, size, __LINE__ - s_UniformsStart - 1)

	UNIFORMS_START;
	// NOTE: The order of variables here must match the order used in shaders
	DECL_UNIFORM(U_MODEL, "model", sizeof(glm::mat4)); // TODO: Rename to OBJECT_TO_WORLD
	DECL_UNIFORM(U_CAM_POS, "camPos", sizeof(glm::vec4));
	DECL_UNIFORM(U_VIEW, "view", sizeof(glm::mat4));
	DECL_UNIFORM(U_VIEW_INV, "invView", sizeof(glm::mat4));
	DECL_UNIFORM(U_VIEW_PROJECTION, "viewProjection", sizeof(glm::mat4));
	DECL_UNIFORM(U_PROJECTION, "projection", sizeof(glm::mat4));
	DECL_UNIFORM(U_PROJECTION_INV, "invProj", sizeof(glm::mat4));
	DECL_UNIFORM(U_COLOUR_MULTIPLIER, "colourMultiplier", sizeof(glm::vec4));
	DECL_UNIFORM(U_DIR_LIGHT, "dirLight", (u32)sizeof(DirLightData));
	DECL_UNIFORM(U_POINT_LIGHTS, "pointLights", sizeof(PointLightData)* MAX_POINT_LIGHT_COUNT);
	DECL_UNIFORM(U_SPOT_LIGHTS, "spotLights", sizeof(SpotLightData)* MAX_SPOT_LIGHT_COUNT);
	DECL_UNIFORM(U_AREA_LIGHTS, "areaLights", sizeof(AreaLightData)* MAX_AREA_LIGHT_COUNT);
	DECL_UNIFORM(U_CONST_ALBEDO, "constAlbedo", (u32)sizeof(glm::vec4));
	DECL_UNIFORM(U_CONST_METALLIC, "constMetallic", sizeof(real));
	DECL_UNIFORM(U_CONST_ROUGHNESS, "constRoughness", sizeof(real));
	DECL_UNIFORM(U_CONST_EMISSIVE, "constEmissive", sizeof(glm::vec4));
	DECL_UNIFORM(U_ENABLE_ALBEDO_SAMPLER, "enableAlbedoSampler", sizeof(i32));
	DECL_UNIFORM(U_ENABLE_METALLIC_SAMPLER, "enableMetallicSampler", sizeof(i32));
	DECL_UNIFORM(U_ENABLE_ROUGHNESS_SAMPLER, "enableRoughnessSampler", sizeof(i32));
	DECL_UNIFORM(U_ENABLE_NORMAL_SAMPLER, "enableNormalSampler", sizeof(i32));
	DECL_UNIFORM(U_ENABLE_EMISSIVE_SAMPLER, "enableEmissiveSampler", sizeof(i32));
	DECL_UNIFORM(U_EXPOSURE, "exposure", sizeof(real));
	DECL_UNIFORM(U_FONT_CHAR_DATA, "fontCharData", sizeof(glm::vec4));
	DECL_UNIFORM(U_SDF_DATA, "sdfData", sizeof(glm::vec4));
	DECL_UNIFORM(U_TEX_SIZE, "texSize", sizeof(glm::vec2));
	DECL_UNIFORM(U_TEX_CHANNEL, "texChannel", sizeof(i32));
	DECL_UNIFORM(U_OCEAN_DATA, "oceanData", sizeof(OceanData));
	DECL_UNIFORM(U_SKYBOX_DATA, "skyboxData", sizeof(SkyboxData));
	DECL_UNIFORM(U_TIME, "time", sizeof(glm::vec4));
	DECL_UNIFORM(U_SHADOW_SAMPLING_DATA, "shadowSamplingData", sizeof(ShadowSamplingData));
	DECL_UNIFORM(U_SSAO_GEN_DATA, "ssaoGenData", sizeof(SSAOGenData));
	DECL_UNIFORM(U_SSAO_BLUR_DATA_DYNAMIC, "ssaoBlurDataDynamic", sizeof(SSAOBlurDataDynamic));
	DECL_UNIFORM(U_SSAO_BLUR_DATA_CONSTANT, "ssaoBlurDataConstant", sizeof(SSAOBlurDataConstant));
	DECL_UNIFORM(U_SSAO_SAMPLING_DATA, "ssaoData", sizeof(SSAOSamplingData));
	DECL_UNIFORM(U_NEAR_FAR_PLANES, "nearFarPlanes", sizeof(glm::vec2));
	DECL_UNIFORM(U_POST_PROCESS_MAT, "postProcessMatrix", sizeof(glm::mat4));
	DECL_UNIFORM(U_LAST_FRAME_VIEWPROJ, "lastFrameViewProj", sizeof(glm::mat4));
	DECL_UNIFORM(U_PARTICLE_BUFFER, "particleBuffer", sizeof(ParticleBufferData));
	DECL_UNIFORM(U_PARTICLE_SIM_DATA, "particleSimData", sizeof(ParticleSimData));
	DECL_UNIFORM(U_UV_BLEND_AMOUNT, "uvBlendAmount", sizeof(glm::vec2));
	DECL_UNIFORM(U_SCREEN_SIZE, "screenSize", sizeof(glm::vec4)); // window (w, h, 1/w, 1/h)
	DECL_UNIFORM(U_BLEND_SHARPNESS, "blendSharpness", sizeof(real));
	DECL_UNIFORM(U_TEXTURE_SCALE, "textureScale", sizeof(real));
	DECL_UNIFORM(U_TERRAIN_GEN_CONSTANT_DATA, "terrainGenConstantData", sizeof(TerrainGenConstantData));
	DECL_UNIFORM(U_TERRAIN_GEN_DYNAMIC_DATA, "terrainGenDynamicData", sizeof(TerrainGenDynamicData));
	DECL_UNIFORM(U_CHARGE_AMOUNT, "chargeAmount", sizeof(real));
	// Textures & buffers (0-sized)
	DECL_UNIFORM(U_ALBEDO_SAMPLER, "albedoSampler", 0);
	DECL_UNIFORM(U_METALLIC_SAMPLER, "metallicSampler", 0);
	DECL_UNIFORM(U_ROUGHNESS_SAMPLER, "roughnessSampler", 0);
	DECL_UNIFORM(U_NORMAL_SAMPLER, "normalSampler", 0);
	DECL_UNIFORM(U_EMISSIVE_SAMPLER, "emissiveSampler", 0);
	DECL_UNIFORM(U_CUBEMAP_SAMPLER, "cubemapSampler", 0);
	DECL_UNIFORM(U_IRRADIANCE_SAMPLER, "irradianceSampler", 0);
	DECL_UNIFORM(U_FB_0_SAMPLER, "normalRoughnessSampler", 0);
	DECL_UNIFORM(U_FB_1_SAMPLER, "albedoMetallicSampler", 0);
	DECL_UNIFORM(U_HDR_EQUIRECTANGULAR_SAMPLER, "hdrEquirectangular", 0);
	DECL_UNIFORM(U_BRDF_LUT_SAMPLER, "brdfLUT", 0);
	DECL_UNIFORM(U_DEPTH_SAMPLER, "depthSampler", 0);
	DECL_UNIFORM(U_NOISE_SAMPLER, "noiseSampler", 0);
	DECL_UNIFORM(U_SSAO_RAW_SAMPLER, "ssaoRawSampler", 0);
	DECL_UNIFORM(U_SSAO_FINAL_SAMPLER, "ssaoFinalSampler", 0);
	DECL_UNIFORM(U_SSAO_NORMAL_SAMPLER, "ssaoNormalSampler", 0); // TODO: Use normalSampler uniform?
	DECL_UNIFORM(U_LTC_MATRICES_SAMPLER, "ltcMatricesSampler", 0);
	DECL_UNIFORM(U_LTC_AMPLITUDES_SAMPLER, "ltcAmplitudesSampler", 0);
	DECL_UNIFORM(U_SHADOW_CASCADES_SAMPLER, "shadowCascadeSampler", 0);
	DECL_UNIFORM(U_SCENE_SAMPLER, "sceneSampler", 0);
	DECL_UNIFORM(U_HISTORY_SAMPLER, "historySampler", 0);
	DECL_UNIFORM(U_PREFILTER_MAP, "prefilterMap", 0);
	DECL_UNIFORM(U_HIGH_RES_TEX, "highResTex", 0);
	DECL_UNIFORM(U_TERRAIN_POINT_BUFFER, "terrainPointBuffer", 0);
	DECL_UNIFORM(U_TERRAIN_VERTEX_BUFFER, "terrainVertexBuffer", 0);
	DECL_UNIFORM(U_RANDOM_TABLES, "randomTables", 0);
	DECL_UNIFORM(U_UNIFORM_BUFFER_CONSTANT, "uniformBufferConstant", 0); // TODO: Infer this
	DECL_UNIFORM(U_UNIFORM_BUFFER_DYNAMIC, "uniformBufferDynamic", 0); // TODO: Infer this

#undef DECL_UNIFORM
#undef UNIFORMS_START

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

	enum TextureFormat : u32
	{
		UNDEFINED = 0,
		R4G4_UNORM_PACK8 = 1,
		R4G4B4A4_UNORM_PACK16 = 2,
		B4G4R4A4_UNORM_PACK16 = 3,
		R5G6B5_UNORM_PACK16 = 4,
		B5G6R5_UNORM_PACK16 = 5,
		R5G5B5A1_UNORM_PACK16 = 6,
		B5G5R5A1_UNORM_PACK16 = 7,
		A1R5G5B5_UNORM_PACK16 = 8,
		R8_UNORM = 9,
		R8_SNORM = 10,
		R8_USCALED = 11,
		R8_SSCALED = 12,
		R8_UINT = 13,
		R8_SINT = 14,
		R8_SRGB = 15,
		R8G8_UNORM = 16,
		R8G8_SNORM = 17,
		R8G8_USCALED = 18,
		R8G8_SSCALED = 19,
		R8G8_UINT = 20,
		R8G8_SINT = 21,
		R8G8_SRGB = 22,
		R8G8B8_UNORM = 23,
		R8G8B8_SNORM = 24,
		R8G8B8_USCALED = 25,
		R8G8B8_SSCALED = 26,
		R8G8B8_UINT = 27,
		R8G8B8_SINT = 28,
		R8G8B8_SRGB = 29,
		B8G8R8_UNORM = 30,
		B8G8R8_SNORM = 31,
		B8G8R8_USCALED = 32,
		B8G8R8_SSCALED = 33,
		B8G8R8_UINT = 34,
		B8G8R8_SINT = 35,
		B8G8R8_SRGB = 36,
		R8G8B8A8_UNORM = 37,
		R8G8B8A8_SNORM = 38,
		R8G8B8A8_USCALED = 39,
		R8G8B8A8_SSCALED = 40,
		R8G8B8A8_UINT = 41,
		R8G8B8A8_SINT = 42,
		R8G8B8A8_SRGB = 43,
		B8G8R8A8_UNORM = 44,
		B8G8R8A8_SNORM = 45,
		B8G8R8A8_USCALED = 46,
		B8G8R8A8_SSCALED = 47,
		B8G8R8A8_UINT = 48,
		B8G8R8A8_SINT = 49,
		B8G8R8A8_SRGB = 50,
		A8B8G8R8_UNORM_PACK32 = 51,
		A8B8G8R8_SNORM_PACK32 = 52,
		A8B8G8R8_USCALED_PACK32 = 53,
		A8B8G8R8_SSCALED_PACK32 = 54,
		A8B8G8R8_UINT_PACK32 = 55,
		A8B8G8R8_SINT_PACK32 = 56,
		A8B8G8R8_SRGB_PACK32 = 57,
		A2R10G10B10_UNORM_PACK32 = 58,
		A2R10G10B10_SNORM_PACK32 = 59,
		A2R10G10B10_USCALED_PACK32 = 60,
		A2R10G10B10_SSCALED_PACK32 = 61,
		A2R10G10B10_UINT_PACK32 = 62,
		A2R10G10B10_SINT_PACK32 = 63,
		A2B10G10R10_UNORM_PACK32 = 64,
		A2B10G10R10_SNORM_PACK32 = 65,
		A2B10G10R10_USCALED_PACK32 = 66,
		A2B10G10R10_SSCALED_PACK32 = 67,
		A2B10G10R10_UINT_PACK32 = 68,
		A2B10G10R10_SINT_PACK32 = 69,
		R16_UNORM = 70,
		R16_SNORM = 71,
		R16_USCALED = 72,
		R16_SSCALED = 73,
		R16_UINT = 74,
		R16_SINT = 75,
		R16_SFLOAT = 76,
		R16G16_UNORM = 77,
		R16G16_SNORM = 78,
		R16G16_USCALED = 79,
		R16G16_SSCALED = 80,
		R16G16_UINT = 81,
		R16G16_SINT = 82,
		R16G16_SFLOAT = 83,
		R16G16B16_UNORM = 84,
		R16G16B16_SNORM = 85,
		R16G16B16_USCALED = 86,
		R16G16B16_SSCALED = 87,
		R16G16B16_UINT = 88,
		R16G16B16_SINT = 89,
		R16G16B16_SFLOAT = 90,
		R16G16B16A16_UNORM = 91,
		R16G16B16A16_SNORM = 92,
		R16G16B16A16_USCALED = 93,
		R16G16B16A16_SSCALED = 94,
		R16G16B16A16_UINT = 95,
		R16G16B16A16_SINT = 96,
		R16G16B16A16_SFLOAT = 97,
		R32_UINT = 98,
		R32_SINT = 99,
		R32_SFLOAT = 100,
		R32G32_UINT = 101,
		R32G32_SINT = 102,
		R32G32_SFLOAT = 103,
		R32G32B32_UINT = 104,
		R32G32B32_SINT = 105,
		R32G32B32_SFLOAT = 106,
		R32G32B32A32_UINT = 107,
		R32G32B32A32_SINT = 108,
		R32G32B32A32_SFLOAT = 109,
		R64_UINT = 110,
		R64_SINT = 111,
		R64_SFLOAT = 112,
		R64G64_UINT = 113,
		R64G64_SINT = 114,
		R64G64_SFLOAT = 115,
		R64G64B64_UINT = 116,
		R64G64B64_SINT = 117,
		R64G64B64_SFLOAT = 118,
		R64G64B64A64_UINT = 119,
		R64G64B64A64_SINT = 120,
		R64G64B64A64_SFLOAT = 121,
		B10G11R11_UFLOAT_PACK32 = 122,
		E5B9G9R9_UFLOAT_PACK32 = 123,
		D16_UNORM = 124,
		X8_D24_UNORM_PACK32 = 125,
		D32_SFLOAT = 126,
		S8_UINT = 127,
		D16_UNORM_S8_UINT = 128,
		D24_UNORM_S8_UINT = 129,
		D32_SFLOAT_S8_UINT = 130,
		BC1_RGB_UNORM_BLOCK = 131,
		BC1_RGB_SRGB_BLOCK = 132,
		BC1_RGBA_UNORM_BLOCK = 133,
		BC1_RGBA_SRGB_BLOCK = 134,
		BC2_UNORM_BLOCK = 135,
		BC2_SRGB_BLOCK = 136,
		BC3_UNORM_BLOCK = 137,
		BC3_SRGB_BLOCK = 138,
		BC4_UNORM_BLOCK = 139,
		BC4_SNORM_BLOCK = 140,
		BC5_UNORM_BLOCK = 141,
		BC5_SNORM_BLOCK = 142,
		BC6H_UFLOAT_BLOCK = 143,
		BC6H_SFLOAT_BLOCK = 144,
		BC7_UNORM_BLOCK = 145,
		BC7_SRGB_BLOCK = 146,
		ETC2_R8G8B8_UNORM_BLOCK = 147,
		ETC2_R8G8B8_SRGB_BLOCK = 148,
		ETC2_R8G8B8A1_UNORM_BLOCK = 149,
		ETC2_R8G8B8A1_SRGB_BLOCK = 150,
		ETC2_R8G8B8A8_UNORM_BLOCK = 151,
		ETC2_R8G8B8A8_SRGB_BLOCK = 152,
		EAC_R11_UNORM_BLOCK = 153,
		EAC_R11_SNORM_BLOCK = 154,
		EAC_R11G11_UNORM_BLOCK = 155,
		EAC_R11G11_SNORM_BLOCK = 156,
		ASTC_4x4_UNORM_BLOCK = 157,
		ASTC_4x4_SRGB_BLOCK = 158,
		ASTC_5x4_UNORM_BLOCK = 159,
		ASTC_5x4_SRGB_BLOCK = 160,
		ASTC_5x5_UNORM_BLOCK = 161,
		ASTC_5x5_SRGB_BLOCK = 162,
		ASTC_6x5_UNORM_BLOCK = 163,
		ASTC_6x5_SRGB_BLOCK = 164,
		ASTC_6x6_UNORM_BLOCK = 165,
		ASTC_6x6_SRGB_BLOCK = 166,
		ASTC_8x5_UNORM_BLOCK = 167,
		ASTC_8x5_SRGB_BLOCK = 168,
		ASTC_8x6_UNORM_BLOCK = 169,
		ASTC_8x6_SRGB_BLOCK = 170,
		ASTC_8x8_UNORM_BLOCK = 171,
		ASTC_8x8_SRGB_BLOCK = 172,
		ASTC_10x5_UNORM_BLOCK = 173,
		ASTC_10x5_SRGB_BLOCK = 174,
		ASTC_10x6_UNORM_BLOCK = 175,
		ASTC_10x6_SRGB_BLOCK = 176,
		ASTC_10x8_UNORM_BLOCK = 177,
		ASTC_10x8_SRGB_BLOCK = 178,
		ASTC_10x10_UNORM_BLOCK = 179,
		ASTC_10x10_SRGB_BLOCK = 180,
		ASTC_12x10_UNORM_BLOCK = 181,
		ASTC_12x10_SRGB_BLOCK = 182,
		ASTC_12x12_UNORM_BLOCK = 183,
		ASTC_12x12_SRGB_BLOCK = 184,
		G8B8G8R8_422_UNORM = 1000156000,
		B8G8R8G8_422_UNORM = 1000156001,
		G8_B8_R8_3PLANE_420_UNORM = 1000156002,
		G8_B8R8_2PLANE_420_UNORM = 1000156003,
		G8_B8_R8_3PLANE_422_UNORM = 1000156004,
		G8_B8R8_2PLANE_422_UNORM = 1000156005,
		G8_B8_R8_3PLANE_444_UNORM = 1000156006,
		R10X6_UNORM_PACK16 = 1000156007,
		R10X6G10X6_UNORM_2PACK16 = 1000156008,
		R10X6G10X6B10X6A10X6_UNORM_4PACK16 = 1000156009,
		G10X6B10X6G10X6R10X6_422_UNORM_4PACK16 = 1000156010,
		B10X6G10X6R10X6G10X6_422_UNORM_4PACK16 = 1000156011,
		G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16 = 1000156012,
		G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16 = 1000156013,
		G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16 = 1000156014,
		G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16 = 1000156015,
		G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16 = 1000156016,
		R12X4_UNORM_PACK16 = 1000156017,
		R12X4G12X4_UNORM_2PACK16 = 1000156018,
		R12X4G12X4B12X4A12X4_UNORM_4PACK16 = 1000156019,
		G12X4B12X4G12X4R12X4_422_UNORM_4PACK16 = 1000156020,
		B12X4G12X4R12X4G12X4_422_UNORM_4PACK16 = 1000156021,
		G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16 = 1000156022,
		G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16 = 1000156023,
		G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16 = 1000156024,
		G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16 = 1000156025,
		G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16 = 1000156026,
		G16B16G16R16_422_UNORM = 1000156027,
		B16G16R16G16_422_UNORM = 1000156028,
		G16_B16_R16_3PLANE_420_UNORM = 1000156029,
		G16_B16R16_2PLANE_420_UNORM = 1000156030,
		G16_B16_R16_3PLANE_422_UNORM = 1000156031,
		G16_B16R16_2PLANE_422_UNORM = 1000156032,
		G16_B16_R16_3PLANE_444_UNORM = 1000156033,
		PVRTC1_2BPP_UNORM_BLOCK_IMG = 1000054000,
		PVRTC1_4BPP_UNORM_BLOCK_IMG = 1000054001,
		PVRTC2_2BPP_UNORM_BLOCK_IMG = 1000054002,
		PVRTC2_4BPP_UNORM_BLOCK_IMG = 1000054003,
		PVRTC1_2BPP_SRGB_BLOCK_IMG = 1000054004,
		PVRTC1_4BPP_SRGB_BLOCK_IMG = 1000054005,
		PVRTC2_2BPP_SRGB_BLOCK_IMG = 1000054006,
		PVRTC2_4BPP_SRGB_BLOCK_IMG = 1000054007,
		ASTC_4x4_SFLOAT_BLOCK_EXT = 1000066000,
		ASTC_5x4_SFLOAT_BLOCK_EXT = 1000066001,
		ASTC_5x5_SFLOAT_BLOCK_EXT = 1000066002,
		ASTC_6x5_SFLOAT_BLOCK_EXT = 1000066003,
		ASTC_6x6_SFLOAT_BLOCK_EXT = 1000066004,
		ASTC_8x5_SFLOAT_BLOCK_EXT = 1000066005,
		ASTC_8x6_SFLOAT_BLOCK_EXT = 1000066006,
		ASTC_8x8_SFLOAT_BLOCK_EXT = 1000066007,
		ASTC_10x5_SFLOAT_BLOCK_EXT = 1000066008,
		ASTC_10x6_SFLOAT_BLOCK_EXT = 1000066009,
		ASTC_10x8_SFLOAT_BLOCK_EXT = 1000066010,
		ASTC_10x10_SFLOAT_BLOCK_EXT = 1000066011,
		ASTC_12x10_SFLOAT_BLOCK_EXT = 1000066012,
		ASTC_12x12_SFLOAT_BLOCK_EXT = 1000066013,
		G8_B8R8_2PLANE_444_UNORM_EXT = 1000330000,
		G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT = 1000330001,
		G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT = 1000330002,
		G16_B16R16_2PLANE_444_UNORM_EXT = 1000330003,
		A4R4G4B4_UNORM_PACK16_EXT = 1000340000,
		A4B4G4R4_UNORM_PACK16_EXT = 1000340001,
		G8B8G8R8_422_UNORM_KHR = G8B8G8R8_422_UNORM,
		B8G8R8G8_422_UNORM_KHR = B8G8R8G8_422_UNORM,
		G8_B8_R8_3PLANE_420_UNORM_KHR = G8_B8_R8_3PLANE_420_UNORM,
		G8_B8R8_2PLANE_420_UNORM_KHR = G8_B8R8_2PLANE_420_UNORM,
		G8_B8_R8_3PLANE_422_UNORM_KHR = G8_B8_R8_3PLANE_422_UNORM,
		G8_B8R8_2PLANE_422_UNORM_KHR = G8_B8R8_2PLANE_422_UNORM,
		G8_B8_R8_3PLANE_444_UNORM_KHR = G8_B8_R8_3PLANE_444_UNORM,
		R10X6_UNORM_PACK16_KHR = R10X6_UNORM_PACK16,
		R10X6G10X6_UNORM_2PACK16_KHR = R10X6G10X6_UNORM_2PACK16,
		R10X6G10X6B10X6A10X6_UNORM_4PACK16_KHR = R10X6G10X6B10X6A10X6_UNORM_4PACK16,
		G10X6B10X6G10X6R10X6_422_UNORM_4PACK16_KHR = G10X6B10X6G10X6R10X6_422_UNORM_4PACK16,
		B10X6G10X6R10X6G10X6_422_UNORM_4PACK16_KHR = B10X6G10X6R10X6G10X6_422_UNORM_4PACK16,
		G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16_KHR = G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16,
		G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16_KHR = G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16,
		G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16_KHR = G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16,
		G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16_KHR = G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16,
		G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16_KHR = G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16,
		R12X4_UNORM_PACK16_KHR = R12X4_UNORM_PACK16,
		R12X4G12X4_UNORM_2PACK16_KHR = R12X4G12X4_UNORM_2PACK16,
		R12X4G12X4B12X4A12X4_UNORM_4PACK16_KHR = R12X4G12X4B12X4A12X4_UNORM_4PACK16,
		G12X4B12X4G12X4R12X4_422_UNORM_4PACK16_KHR = G12X4B12X4G12X4R12X4_422_UNORM_4PACK16,
		B12X4G12X4R12X4G12X4_422_UNORM_4PACK16_KHR = B12X4G12X4R12X4G12X4_422_UNORM_4PACK16,
		G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16_KHR = G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16,
		G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16_KHR = G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16,
		G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16_KHR = G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16,
		G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16_KHR = G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16,
		G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16_KHR = G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16,
		G16B16G16R16_422_UNORM_KHR = G16B16G16R16_422_UNORM,
		B16G16R16G16_422_UNORM_KHR = B16G16R16G16_422_UNORM,
		G16_B16_R16_3PLANE_420_UNORM_KHR = G16_B16_R16_3PLANE_420_UNORM,
		G16_B16R16_2PLANE_420_UNORM_KHR = G16_B16R16_2PLANE_420_UNORM,
		G16_B16_R16_3PLANE_422_UNORM_KHR = G16_B16_R16_3PLANE_422_UNORM,
		G16_B16R16_2PLANE_422_UNORM_KHR = G16_B16R16_2PLANE_422_UNORM,
		G16_B16_R16_3PLANE_444_UNORM_KHR = G16_B16_R16_3PLANE_444_UNORM,
		MAX_ENUM = 0x7FFFFFFF
	};

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

	using HTextureSampler = void*;

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

		bool LoadData(i32 requestedChannelCount);
		void FreeData();

		virtual u32 CreateFromMemory(void* buffer, u32 bufferSize, u32 inWidth, u32 inHeight, u32 inChannelCount,
			TextureFormat inFormat, i32 inMipLevels, HTextureSampler inSampler, i32 layerCount = 1) = 0;

		/*
		* Creates this texture's rendering resources
		* Requires data to have been loaded already
		* Returns the size of the image
		*/
		virtual u64 Create(bool bGenerateFullMipChain = false) = 0;

		/*
		 * Creates image, image view, and sampler based on the texture at relativeFilePath
		 * Returns true if load completed successfully
		 */
		virtual bool LoadFromFile(const std::string& inRelativeFilePath, HTextureSampler inSampler, TextureFormat inFormat = TextureFormat::UNDEFINED) = 0;

		bool IsLoading() const;
		bool IsCreated() const;

		u32 width = 0;
		u32 height = 0;
		u32 channelCount = 0;
		u8* pixels = nullptr; // Only valid in between load and creation
		std::string name;
		std::string relativeFilePath;
		std::string fileName;
		std::array<std::string, 6> relativeCubemapFilePaths;
		u32 mipLevels = 1;
		bool bFlipVertically = false;
		bool bGenerateMipMaps = false;
		bool bHDR = false;
		bool bIsArray = false;
		std::atomic_uint32_t bIsLoading = 0;
		std::atomic_uint32_t bIsCreated = 0;
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
		bool HasUniform(StringID uniformID) const;
		bool HasUniform(StringID uniformID, MaterialPropertyOverride& outPropertyOverride) const;

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

		bool generateCubemapSampler = false;
		bool enableCubemapSampler = false;

		// PBR samplers
		bool enableAlbedoSampler = false;
		bool enableMetallicSampler = false;
		bool enableRoughnessSampler = false;
		bool enableNormalSampler = false;
		bool enableEmissiveSampler = false;

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
