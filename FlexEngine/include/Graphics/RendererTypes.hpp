#pragma once

#include <array>
#include <map>

IGNORE_WARNINGS_PUSH
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
IGNORE_WARNINGS_POP

#include "Functors.hpp"
#include "JSONTypes.hpp"
#include "Pair.hpp"

extern const char** DataTypeStrs;

namespace flex
{
	class VertexBufferData;
	class GameObject;

	static const i32 MAX_NUM_POINT_LIGHTS = 8;
	static const i32 NUM_SHADOW_CASCADES = 4;

	// 48 bytes
	struct DirLightData
	{
		glm::vec3 dir;       // 0
		i32 enabled;         // 12
		glm::vec3 color;     // 16
		real brightness;     // 28
		i32 castShadows;     // 32
		real shadowDarkness; // 36
		real pad[2];         // 40
	};

	// 32 bytes
	struct PointLightData
	{
		glm::vec3 pos;   // 0
		i32 enabled;     // 12
		glm::vec3 color; // 16
		real brightness; // 28
	};

	const u32 MAX_SSAO_KERNEL_SIZE = 64;
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

	// 8 bytes
	struct SSAOSamplingData
	{
		i32 ssaoEnabled; // 0
		real ssaoPowExp; // 4
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
		glm::mat4 cascadeViewProjMats[NUM_SHADOW_CASCADES]; // 0
		glm::vec4 cascadeDepthSplits;                       // 256
	};

	// Uniforms
	const u64 U_MODEL							= (1ull << 0);	const u32 US_MODEL						= sizeof(glm::mat4);
	const u64 U_VIEW							= (1ull << 1);	const u32 US_VIEW						= sizeof(glm::mat4);
	const u64 U_VIEW_INV						= (1ull << 2);	const u32 US_VIEW_INV					= sizeof(glm::mat4);
	const u64 U_VIEW_PROJECTION					= (1ull << 3);	const u32 US_VIEW_PROJECTION			= sizeof(glm::mat4);
	const u64 U_PROJECTION						= (1ull << 4);	const u32 US_PROJECTION					= sizeof(glm::mat4);
	const u64 U_PROJECTION_INV					= (1ull << 5);	const u32 US_PROJECTION_INV				= sizeof(glm::mat4);
	const u64 U_BLEND_SHARPNESS					= (1ull << 6);	const u32 US_BLEND_SHARPNESS			= sizeof(real);
	const u64 U_COLOR_MULTIPLIER				= (1ull << 7);	const u32 US_COLOR_MULTIPLIER			= sizeof(glm::vec4);
	const u64 U_CAM_POS							= (1ull << 8);	const u32 US_CAM_POS					= sizeof(glm::vec4);
	const u64 U_DIR_LIGHT						= (1ull << 9); const u32 US_DIR_LIGHT					= sizeof(DirLightData);
	const u64 U_POINT_LIGHTS					= (1ull << 10); const u32 US_POINT_LIGHTS				= sizeof(PointLightData) * MAX_NUM_POINT_LIGHTS;
	const u64 U_ALBEDO_SAMPLER					= (1ull << 11);
	const u64 U_CONST_ALBEDO					= (1ull << 12); const u32 US_CONST_ALBEDO				= sizeof(glm::vec4);
	const u64 U_METALLIC_SAMPLER				= (1ull << 13);
	const u64 U_CONST_METALLIC					= (1ull << 14); const u32 US_CONST_METALLIC				= sizeof(real);
	const u64 U_ROUGHNESS_SAMPLER				= (1ull << 15);
	const u64 U_CONST_ROUGHNESS					= (1ull << 16); const u32 US_CONST_ROUGHNESS			= sizeof(real);
	const u64 U_NORMAL_SAMPLER					= (1ull << 17);
	const u64 U_ENABLE_ALBEDO_SAMPLER			= (1ull << 18); const u32 US_ENABLE_ALBEDO_SAMPLER		= sizeof(i32);
	const u64 U_ENABLE_METALLIC_SAMPLER			= (1ull << 19); const u32 US_ENABLE_METALLIC_SAMPLER	= sizeof(i32);
	const u64 U_ENABLE_ROUGHNESS_SAMPLER		= (1ull << 20); const u32 US_ENABLE_ROUGHNESS_SAMPLER	= sizeof(i32);
	const u64 U_ENABLE_NORMAL_SAMPLER			= (1ull << 21); const u32 US_ENABLE_NORMAL_SAMPLER		= sizeof(i32);
	const u64 U_ENABLE_IRRADIANCE_SAMPLER		= (1ull << 22); const u32 US_ENABLE_IRRADIANCE_SAMPLER	= sizeof(i32);
	const u64 U_CUBEMAP_SAMPLER					= (1ull << 23);
	const u64 U_IRRADIANCE_SAMPLER				= (1ull << 24);
	const u64 U_FB_0_SAMPLER					= (1ull << 25);
	const u64 U_FB_1_SAMPLER					= (1ull << 26);
	const u64 U_SHOW_EDGES						= (1ull << 27); const u32 US_SHOW_EDGES					= sizeof(i32);
	const u64 U_LIGHT_VIEW_PROJS				= (1ull << 28); const u32 US_LIGHT_VIEW_PROJS			= sizeof(glm::mat4) * NUM_SHADOW_CASCADES;
	const u64 U_HDR_EQUIRECTANGULAR_SAMPLER		= (1ull << 29);
	const u64 U_BRDF_LUT_SAMPLER				= (1ull << 30);
	const u64 U_PREFILTER_MAP					= (1ull << 31);
	const u64 U_EXPOSURE						= (1ull << 32); const u32 US_EXPOSURE					= sizeof(real);
	const u64 U_FONT_CHAR_DATA					= (1ull << 33); const u32 US_FONT_CHAR_DATA				= sizeof(glm::vec4);
	const u64 U_TEX_SIZE						= (1ull << 34); const u32 US_TEX_SIZE					= sizeof(glm::vec2);
	const u64 U_UNIFORM_BUFFER_CONSTANT			= (1ull << 35);
	const u64 U_UNIFORM_BUFFER_DYNAMIC			= (1ull << 36);
	const u64 U_TEXTURE_SCALE					= (1ull << 37); const u32 US_TEXTURE_SCALE				= sizeof(real);
	const u64 U_TIME							= (1ull << 38); const u32 US_TIME						= sizeof(real);
	const u64 U_SDF_DATA						= (1ull << 39); const u32 US_SDF_DATA					= sizeof(glm::vec4);
	const u64 U_TEX_CHANNEL						= (1ull << 40); const u32 US_TEX_CHANNEL				= sizeof(i32);
	const u64 U_HIGH_RES_TEX					= (1ull << 41);
	const u64 U_DEPTH_SAMPLER					= (1ull << 42);
	const u64 U_NOISE_SAMPLER					= (1ull << 43);
	const u64 U_SSAO_RAW_SAMPLER				= (1ull << 44);
	const u64 U_SSAO_FINAL_SAMPLER				= (1ull << 45);
	const u64 U_SSAO_NORMAL_SAMPLER				= (1ull << 46);
	const u64 U_SSAO_GEN_DATA					= (1ull << 47); const u32 US_SSAO_GEN_DATA				= sizeof(SSAOGenData);
	const u64 U_SSAO_BLUR_DATA_DYNAMIC			= (1ull << 48); const u32 US_SSAO_BLUR_DATA_DYNAMIC		= sizeof(SSAOBlurDataDynamic);
	const u64 U_SSAO_BLUR_DATA_CONSTANT			= (1ull << 49); const u32 US_SSAO_BLUR_DATA_CONSTANT	= sizeof(SSAOBlurDataConstant);
	const u64 U_SSAO_SAMPLING_DATA				= (1ull << 50); const u32 US_SSAO_SAMPLING_DATA			= sizeof(SSAOSamplingData);
	const u64 U_FXAA_DATA						= (1ull << 51); const u32 US_FXAA_DATA					= sizeof(FXAAData);
	const u64 U_SHADOW_SAMPLER					= (1ull << 52);
	const u64 U_SHADOW_SAMPLING_DATA			= (1ull << 53); const u32 US_SHADOW_SAMPLING_DATA		= sizeof(ShadowSamplingData);
	const u64 U_NEAR_FAR_PLANES					= (1ull << 54); const u32 US_NEAR_FAR_PLANES			= sizeof(glm::vec2);
	const u64 U_POST_PROCESS_MAT				= (1ull << 55); const u32 US_POST_PROCESS_MAT			= sizeof(glm::mat4);
	const u64 U_SCENE_SAMPLER					= (1ull << 56);
	const u64 U_HISTORY_SAMPLER					= (1ull << 57);
	// NOTE!: New uniforms must be added to Uniforms::CalculateSizeInBytes

	enum class ClearFlag
	{
		COLOR =   (1 << 0),
		DEPTH =   (1 << 1),
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

	// TODO: Is setting all the members to false necessary?
	// TODO: Straight up copy most of these with a memcpy?
	struct MaterialCreateInfo
	{
		std::string shaderName = "";
		std::string name = "";

		std::string normalTexturePath = "";
		std::string albedoTexturePath = "";
		std::string metallicTexturePath = "";
		std::string roughnessTexturePath = "";
		std::string hdrEquirectangularTexturePath = "";

		glm::vec4 colorMultiplier = { 1.0f, 1.0f, 1.0f, 1.0f };
		std::vector<Pair<std::string, void*>> sampledFrameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs
		glm::vec2 generatedIrradianceCubemapSize = { 0.0f, 0.0f };
		MaterialID irradianceSamplerMatID = InvalidMaterialID; // The id of the material who has an irradiance sampler object (generateIrradianceSampler must be false)
		std::string environmentMapPath = "";
		std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT
		glm::vec2 generatedCubemapSize = { 0.0f, 0.0f };
		glm::vec2 generatedPrefilteredCubemapSize = { 0.0f, 0.0f };
		MaterialID prefilterMapSamplerMatID = InvalidMaterialID;

		// PBR Constant values
		glm::vec3 constAlbedo = { 1.0f, 1.0f, 1.0f };
		real constMetallic = 0.0f;
		real constRoughness = 0.0f;

		real textureScale = 1.0f;

		bool generateNormalSampler = false;
		bool enableNormalSampler = false;
		bool generateAlbedoSampler = false;
		bool enableAlbedoSampler = false;
		bool generateMetallicSampler = false;
		bool enableMetallicSampler = false;
		bool generateRoughnessSampler = false;
		bool enableRoughnessSampler = false;
		bool generateHDREquirectangularSampler = false;
		bool enableHDREquirectangularSampler = false;
		bool generateHDRCubemapSampler = false;

		bool enableIrradianceSampler = false;
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

		bool persistent = false;
		bool visibleInEditor = true;
	};

	struct Material
	{
		~Material()
		{
			if (pushConstantBlock)
			{
				delete pushConstantBlock;
				pushConstantBlock = nullptr;
			}
		}

		bool Equals(const Material& other);

		static void ParseJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut);
		JSONObject Serialize() const;

		std::string name = "";
		ShaderID shaderID = InvalidShaderID;
		std::string normalTexturePath = "";

		// GBuffer samplers
		std::vector<Pair<std::string, void*>> sampledFrameBuffers;

		glm::vec2 cubemapSamplerSize = { 0, 0 };
		std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT

		// PBR constants
		glm::vec4 constAlbedo = { 1, 1, 1, 1 };
		real constMetallic = 0;
		real constRoughness = 0;
		std::string albedoTexturePath = "";
		std::string metallicTexturePath = "";
		std::string roughnessTexturePath = "";
		std::string hdrEquirectangularTexturePath = "";
		glm::vec2 irradianceSamplerSize = { 0, 0 };
		std::string environmentMapPath = "";
		glm::vec2 prefilteredMapSize = { 0, 0 };
		glm::vec4 colorMultiplier = { 1, 1, 1, 1 };

		bool generateNormalSampler = false;
		bool enableNormalSampler = false;

		bool generateCubemapSampler = false;
		bool enableCubemapSampler = false;

		// PBR samplers
		bool generateAlbedoSampler = false;
		bool enableAlbedoSampler = false;

		bool generateMetallicSampler = false;
		bool enableMetallicSampler = false;

		bool generateRoughnessSampler = false;
		bool enableRoughnessSampler = false;

		bool generateHDREquirectangularSampler = false;
		bool enableHDREquirectangularSampler = false;

		bool enableCubemapTrilinearFiltering = false;
		bool generateHDRCubemapSampler = false;

		bool enableIrradianceSampler = false;
		bool generateIrradianceSampler = false;

		bool enablePrefilteredMap = false;
		bool generatePrefilteredMap = false;

		bool enableBRDFLUT = false;
		bool renderToCubemap = false; // NOTE: This flag is currently ignored by GL renderer!

		bool generateReflectionProbeMaps = false;

		bool persistent = false;
		bool visibleInEditor = false;

		real textureScale = 1.0f;
		real blendSharpness = 1.0f;

		glm::vec4 fontCharData;
		glm::vec2 texSize;

		struct PushConstantBlock
		{
			PushConstantBlock(i32 initialSize) : size(initialSize) { assert(initialSize != 0); }
			PushConstantBlock() {}

			~PushConstantBlock()
			{
				if (data)
				{
					free_hooked(data);
					data = nullptr;
					size = 0;
				}
			}

			void SetData(const glm::mat4& viewProj)
			{
				const i32 dataSize = sizeof(glm::mat4) * 1;
				if (data == nullptr)
				{
					assert(size == dataSize || size == 0);

					size = dataSize;
					data = malloc_hooked(dataSize);
				}
				else
				{
					assert(size == dataSize && "Attempted to set push constant data with differing size. Block must be reallocated.");
				}
				real* dst = (real*)data;
				memcpy(dst, &viewProj, sizeof(glm::mat4)); dst += sizeof(glm::mat4) / sizeof(real);
			}

			void SetData(const glm::mat4& view, const glm::mat4& proj)
			{
				const i32 dataSize = sizeof(glm::mat4) * 2;
				if (data == nullptr)
				{
					assert(size == dataSize || size == 0);

					size = dataSize;
					data = malloc_hooked(dataSize);
				}
				else
				{
					assert(size == dataSize && "Attempted to set push constant data with differing size. Block must be reallocated.");
				}
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
					data = malloc_hooked(dataSize);
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

		private:
			PushConstantBlock(PushConstantBlock& other) = delete;
			PushConstantBlock(PushConstantBlock&& other) = delete;
			PushConstantBlock& operator=(PushConstantBlock& other) = delete;
			PushConstantBlock& operator=(PushConstantBlock&& other) = delete;
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
		UI,
		GAMMA_CORRECT,

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
		bool bDepthWriteEnable = true;
		bool bDepthTestEnable = true;
		bool bEditorObject = false;
		bool bSetDynamicStates = false;
	};

	struct Uniforms
	{
		u64 uniforms;

		bool HasUniform(u64 uniform) const;
		void AddUniform(u64 uniform);
		u32 CalculateSizeInBytes() const;
	};

	struct Shader
	{
		Shader(const std::string& name,
			   const std::string& inVertexShaderFilePath,
			   const std::string& inFragmentShaderFilePath = "",
			   const std::string& inGeometryShaderFilePath = "");

		std::string name = "";

		std::string vertexShaderFilePath = "";
		std::string fragmentShaderFilePath = "";
		std::string geometryShaderFilePath = "";

		std::vector<char> vertexShaderCode = {};
		std::vector<char> fragmentShaderCode = {};
		std::vector<char> geometryShaderCode = {};

		Uniforms constantBufferUniforms = {};
		Uniforms dynamicBufferUniforms = {};

		VertexAttributes vertexAttributes = 0;
		i32 numAttachments = 1; // How many output textures the fragment shader has

		i32 subpass = 0;
		bool bDeferred = false; // TODO: Replace this bool with just checking if numAttachments is larger than 1
		bool bDepthWriteEnable = true;
		bool bTranslucent = false;

		// These variables should be set to true when the shader has these uniforms
		bool bNeedNormalSampler = false;
		bool bNeedCubemapSampler = false;
		bool bNeedAlbedoSampler = false;
		bool bNeedMetallicSampler = false;
		bool bNeedRoughnessSampler = false;
		bool bNeedHDREquirectangularSampler = false;
		bool bNeedIrradianceSampler = false;
		bool bNeedPrefilteredMap = false;
		bool bNeedBRDFLUT = false;
		bool bNeedShadowMap = false;
		bool bNeedDepthSampler = false;
		bool bNeedNoiseSampler = false; // TODO: Replace with check for U_NOISE_SAMPLER
		bool bNeedPushConstantBlock = false;
		bool bGenerateVertexBufferForAll = false;
		bool bTextureArr = false;
		u32 pushConstantBlockSize = 0;

		bool bDynamic = false;
		u32 dynamicVertexBufferSize = 0;
		RenderPassType renderPassType = RenderPassType::_NONE;

		bool pushConstantsNeededInFragStage = false;
	};

	struct SpriteQuadDrawInfo
	{
		//RenderID spriteObjectRenderID = InvalidRenderID;
		TextureID textureID = InvalidTextureID;
		u32 textureLayer = 0;
		u32 FBO = 0; // 0 for rendering to final RT
		u32 RBO = 0; // 0 for rendering to final RT
		MaterialID materialID = InvalidMaterialID;
		glm::vec3 pos = VEC3_ZERO;
		glm::quat rotation = QUAT_UNIT;
		glm::vec3 scale = VEC3_ONE;
		AnchorPoint anchor = AnchorPoint::TOP_LEFT;
		glm::vec4 color = VEC4_ONE;

		bool bScreenSpace = true;
		bool bReadDepth = true;
		bool bWriteDepth = true;
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
		glm::vec4 color;                      // 16
		glm::vec4 charSizePixelsCharSizeNorm; // 32 - RG: char size in pixels, BA: char size in [0, 1] in screen space
		i32 channel;                          // 48 - Uses extra int slot
	};

	// 68 bytes
	struct TextVertex3D
	{
		glm::vec3 pos;                        // 0
		                                      // + 4
		glm::vec2 uv;                         // 16
		glm::vec4 color;                      // 24
		glm::vec3 tangent;                    // 32
		                                      // + 4
		glm::vec4 charSizePixelsCharSizeNorm; // 48 - RG: char size in pixels, BA: char size in [0, 1] in screen space
		i32 channel;                          // 64 - uses extra int slot
	};

} // namespace flex
