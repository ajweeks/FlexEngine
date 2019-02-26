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

	struct DirLightData
	{
		glm::vec3 dir;
		glm::vec4 color;
		real brightness;
		bool bEnabled;

		real shadowDarkness;
		bool bCastShadow;
		real shadowMapNearPlane;
		real shadowMapFarPlane;
		real shadowMapZoom;

		// DEBUG: (just for preview in ImGui)
		u32 shadowTextureID = 0;
		glm::vec3 pos;
	};

	struct PointLightData
	{
		glm::vec3 pos;
		glm::vec4 color;
		real brightness;
		bool bEnabled;
	};

	// Uniforms
	const u64 U_MODEL							= (1ull << 0); const u32 US_MODEL						= sizeof(glm::mat4);
	const u64 U_MODEL_INV_TRANSPOSE				= (1ull << 1); const u32 US_MODEL_INV_TRANSPOSE			= sizeof(glm::mat4);
	const u64 U_VIEW							= (1ull << 2); const u32 US_VIEW						= sizeof(glm::mat4);
	const u64 U_VIEW_INV						= (1ull << 3); const u32 US_VIEW_INV					= sizeof(glm::mat4);
	const u64 U_VIEW_PROJECTION					= (1ull << 4); const u32 US_VIEW_PROJECTION				= sizeof(glm::mat4);
	const u64 U_MODEL_VIEW_PROJ					= (1ull << 5); const u32 US_MODEL_VIEW_PROJ				= sizeof(glm::mat4);
	// 6
	const u64 U_PROJECTION						= (1ull << 7); const u32 US_PROJECTION					= sizeof(glm::mat4);
	const u64 U_BLEND_SHARPNESS					= (1ull << 8); const u32 US_BLEND_SHARPNESS				= sizeof(real);
	const u64 U_COLOR_MULTIPLIER				= (1ull << 9); const u32 US_COLOR_MULTIPLIER			= sizeof(glm::vec4);
	const u64 U_CAM_POS							= (1ull << 10); const u32 US_CAM_POS					= sizeof(glm::vec4);
	const u64 U_DIR_LIGHT						= (1ull << 11); const u32 US_DIR_LIGHT					= sizeof(DirLightData);
	const u64 U_POINT_LIGHTS					= (1ull << 12); const u32 US_POINT_LIGHTS				= sizeof(PointLightData);
	const u64 U_ALBEDO_SAMPLER					= (1ull << 13); //const u32 US_ALBEDO_SAMPLER			= sizeof(glm::mat4);
	const u64 U_CONST_ALBEDO					= (1ull << 14); const u32 US_CONST_ALBEDO				= sizeof(glm::vec4);
	const u64 U_METALLIC_SAMPLER				= (1ull << 15); //const u32 US_METALLIC_SAMPLER			= sizeof(glm::mat4);
	const u64 U_CONST_METALLIC					= (1ull << 16); const u32 US_CONST_METALLIC				= sizeof(real);
	const u64 U_ROUGHNESS_SAMPLER				= (1ull << 17); //const u32 US_ROUGHNESS_SAMPLER		= sizeof(glm::mat4);
	const u64 U_CONST_ROUGHNESS					= (1ull << 18); const u32 US_CONST_ROUGHNESS			= sizeof(real);
	const u64 U_AO_SAMPLER						= (1ull << 19); //const u32 US_AO_SAMPLER				= sizeof(glm::mat4);
	const u64 U_CONST_AO						= (1ull << 20); const u32 US_CONST_AO					= sizeof(real);
	const u64 U_NORMAL_SAMPLER					= (1ull << 21); //const u32 US_NORMAL_SAMPLER			= sizeof(glm::mat4);
	const u64 U_ENABLE_CUBEMAP_SAMPLER			= (1ull << 22); const u32 US_ENABLE_CUBEMAP_SAMPLER		= sizeof(i32);
	const u64 U_ENABLE_ALBEDO_SAMPLER			= (1ull << 23); const u32 US_ENABLE_ALBEDO_SAMPLER		= sizeof(i32);
	const u64 U_ENABLE_METALLIC_SAMPLER			= (1ull << 24); const u32 US_ENABLE_METALLIC_SAMPLER	= sizeof(i32);
	const u64 U_ENABLE_ROUGHNESS_SAMPLER		= (1ull << 25); const u32 US_ENABLE_ROUGHNESS_SAMPLER	= sizeof(i32);
	const u64 U_ENABLE_AO_SAMPLER				= (1ull << 26); const u32 US_ENABLE_AO_SAMPLER			= sizeof(i32);
	const u64 U_ENABLE_NORMAL_SAMPLER			= (1ull << 27); const u32 US_ENABLE_NORMAL_SAMPLER		= sizeof(i32);
	const u64 U_ENABLE_IRRADIANCE_SAMPLER		= (1ull << 28); const u32 US_ENABLE_IRRADIANCE_SAMPLER	= sizeof(i32);
	const u64 U_CUBEMAP_SAMPLER					= (1ull << 29); //const u32 US_CUBEMAP_SAMPLER			= sizeof(glm::mat4);
	const u64 U_IRRADIANCE_SAMPLER				= (1ull << 30); //const u32 US_IRRADIANCE_SAMPLER		= sizeof(glm::mat4);
	const u64 U_FB_0_SAMPLER					= (1ull << 31); //const u32 US_FB_0_SAMPLER				= sizeof(glm::mat4);
	const u64 U_FB_1_SAMPLER					= (1ull << 32); //const u32 US_FB_1_SAMPLER				= sizeof(glm::mat4);
	const u64 U_FB_2_SAMPLER					= (1ull << 33); //const u32 US_FB_2_SAMPLER				= sizeof(glm::mat4);
	const u64 U_TEXEL_STEP						= (1ull << 34); const u32 US_TEXEL_STEP					= sizeof(real);
	const u64 U_SHOW_EDGES						= (1ull << 35); const u32 US_SHOW_EDGES					= sizeof(i32);
	const u64 U_LIGHT_VIEW_PROJ					= (1ull << 36); const u32 US_LIGHT_VIEW_PROJ			= sizeof(glm::mat4);
	const u64 U_HDR_EQUIRECTANGULAR_SAMPLER		= (1ull << 37); //const u32 US_HDR_EQUIRECTANGULAR_SAMPLER= sizeof(glm::mat4);
	const u64 U_BRDF_LUT_SAMPLER				= (1ull << 38); //const u32 US_BRDF_LUT_SAMPLER			= sizeof(glm::mat4);
	const u64 U_PREFILTER_MAP					= (1ull << 39); //const u32 US_PREFILTER_MAP			= sizeof(glm::mat4);
	const u64 U_EXPOSURE						= (1ull << 40); const u32 US_EXPOSURE					= sizeof(real);
	const u64 U_TRANSFORM_MAT					= (1ull << 41); const u32 US_TRANSFORM_MAT				= sizeof(glm::mat4);
	const u64 U_TEX_SIZE						= (1ull << 42); const u32 US_TEX_SIZE					= sizeof(real);
	const u64 U_UNIFORM_BUFFER_CONSTANT			= (1ull << 43); //const u32 US_UNIFORM_BUFFER_CONSTANT	= sizeof(glm::mat4);
	const u64 U_UNIFORM_BUFFER_DYNAMIC			= (1ull << 44); //const u32 US_UNIFORM_BUFFER_DYNAMIC	= sizeof(glm::mat4);
	const u64 U_TEXTURE_SCALE					= (1ull << 45); const u32 US_TEXTURE_SCALE				= sizeof(real);
	const u64 U_TIME							= (1ull << 46); const u32 US_TIME						= sizeof(real);

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

		_NONE
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
		std::string aoTexturePath = "";
		std::string hdrEquirectangularTexturePath = "";

		glm::vec4 colorMultiplier = { 1.0f, 1.0f, 1.0f, 1.0f };
		std::vector<Pair<std::string, void*>> frameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs
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
		real constAO = 0.0f;

		real textureScale = 1.0f;

		bool generateNormalSampler = false;
		bool enableNormalSampler = false;
		bool generateAlbedoSampler = false;
		bool enableAlbedoSampler = false;
		bool generateMetallicSampler = false;
		bool enableMetallicSampler = false;
		bool generateRoughnessSampler = false;
		bool enableRoughnessSampler = false;
		bool generateAOSampler = false;
		bool enableAOSampler = false;
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

		bool engineMaterial = false;

	};

	struct Material
	{
		bool Equals(const Material& other);

		static void ParseJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut);
		JSONObject Serialize() const;

		std::string name = "";
		ShaderID shaderID = InvalidShaderID;
		std::string normalTexturePath = "";

		// GBuffer samplers
		std::vector<Pair<std::string, void*>> frameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs

		glm::vec2 cubemapSamplerSize = { 0, 0 };
		std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT

		// PBR constants
		glm::vec4 constAlbedo = { 1, 1, 1, 1 };
		real constMetallic = 0;
		real constRoughness = 0;
		real constAO = 1;
		std::string albedoTexturePath = "";
		std::string metallicTexturePath = "";
		std::string roughnessTexturePath = "";
		std::string aoTexturePath = "";
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

		bool generateAOSampler = false;
		bool enableAOSampler = false;

		bool generateHDREquirectangularSampler = false;
		bool enableHDREquirectangularSampler = false;

		bool enableCubemapTrilinearFiltering = false;
		bool generateHDRCubemapSampler = false;

		bool enableIrradianceSampler = false;
		bool generateIrradianceSampler = false;

		bool enablePrefilteredMap = false;
		bool generatePrefilteredMap = false;

		bool enableBRDFLUT = false;
		bool renderToCubemap = true; // NOTE: This flag is currently ignored by GL renderer!

		bool generateReflectionProbeMaps = false;

		// If true, this material shouldn't be removed when switching scenes
		bool engineMaterial = false;

		real textureScale = 1.0f;
		real blendSharpness = 1.0f;

		// TODO: Make this more dynamic!
		struct PushConstantBlock
		{
			glm::mat4 mvp;
		};
		PushConstantBlock pushConstantBlock = {};
	};

	struct RenderObjectCreateInfo
	{
		MaterialID materialID = InvalidMaterialID;

		VertexBufferData* vertexBufferData = nullptr;
		std::vector<u32>* indices = nullptr;

		GameObject* gameObject = nullptr;

		DepthTestFunc depthTestReadFunc = DepthTestFunc::LEQUAL;
		CullFace cullFace = CullFace::BACK;

		bool visible = true;
		bool visibleInSceneExplorer = true;
		bool enableCulling = true;
		bool depthWriteEnable = true;
		bool editorObject = false;
	};

	struct Uniforms
	{
		u64 uniforms;
		std::map<const char*, bool, strCmp> types;

		bool HasUniform(u64 uniform) const;
		void AddUniform(u64 uniform);
		u32 CalculateSizeInBytes(u32 numPointLights) const;
	};

	struct Shader
	{
		Shader(const std::string& name,
			   const std::string& vertexShaderFilePath,
			   const std::string& fragmentShaderFilePath,
			   const std::string& geometryShaderFilePath = "");

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
		bool deferred = false; // TODO: Replace this bool with just checking if numAttachments is larger than 1
		bool depthWriteEnable = true;
		bool translucent = false;

		// These variables should be set to true when the shader has these uniforms
		bool bNeedNormalSampler = false;
		bool bNeedCubemapSampler = false;
		bool bNeedAlbedoSampler = false;
		bool bNeedMetallicSampler = false;
		bool bNeedRoughnessSampler = false;
		bool bNeedAOSampler = false;
		bool bNeedHDREquirectangularSampler = false;
		bool bNeedIrradianceSampler = false;
		bool bNeedPrefilteredMap = false;
		bool bNeedBRDFLUT = false;
		bool bNeedShadowMap = false;
		bool bNeedPushConstantBlock = false;
	};

	struct SpriteQuadDrawInfo
	{
		RenderID spriteObjectRenderID = InvalidRenderID;
		u32 textureHandleID = 0; // Not a TextureID, but the GL id (TODO: Make API-agnostic)
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
	};

} // namespace flex
