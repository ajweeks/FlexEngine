#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#pragma warning(push, 0)
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#pragma warning(pop)

#include "Functors.hpp"
#include "JSONTypes.hpp"

extern const char** DataTypeStrs;

namespace flex
{
	class VertexBufferData;
	class GameObject;

#define BIT(x) (1 << x)

	enum class Uniform : i64
	{
		MODEL						= BIT(0),
		MODEL_INV_TRANSPOSE			= BIT(1),
		VIEW						= BIT(2),
		VIEW_PROJECTION				= BIT(3),
		PROJECTION					= BIT(4),
		COLOR_MULTIPLIER			= BIT(5),
		CAM_POS						= BIT(6),
		DIR_LIGHT					= BIT(7),
		POINT_LIGHTS				= BIT(8),
		ALBEDO_SAMPLER				= BIT(9),
		CONST_ALBEDO				= BIT(10),
		METALLIC_SAMPLER			= BIT(11),
		CONST_METALLIC				= BIT(12),
		ROUGHNESS_SAMPLER			= BIT(13),
		CONST_ROUGHNESS				= BIT(14),
		AO_SAMPLER					= BIT(15),
		CONST_AO					= BIT(16),
		NORMAL_SAMPLER				= BIT(17),
		ENABLE_CUBEMAP_SAMPLER		= BIT(18),
		CUBEMAP_SAMPLER				= BIT(19),
		IRRADIANCE_SAMPLER			= BIT(20),
		FB_0_SAMPLER				= BIT(21),
		FB_1_SAMPLER				= BIT(22),
		FB_2_SAMPLER				= BIT(23),
		TEXEL_STEP					= BIT(24),
		SHOW_EDGES					= BIT(25),
		LIGHT_VIEW_PROJ				= BIT(26),
		HDR_EQUIRECTANGULAR_SAMPLER	= BIT(27),
		EXPOSURE					= BIT(28),
		TRANSFORM_MAT				= BIT(29),
		TEX_SIZE					= BIT(30),
	};

	enum class ClearFlag
	{
		COLOR =   (1 << 0),
		DEPTH =   (1 << 1),
		STENCIL = (1 << 2),
		NONE
	};

	enum class CullFace
	{
		BACK,
		FRONT,
		FRONT_AND_BACK,
		NONE
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
		NONE
	};

	enum class BufferTarget
	{
		ARRAY_BUFFER,
		ELEMENT_ARRAY_BUFFER,
		NONE
	};

	enum class UsageFlag
	{
		STATIC_DRAW,
		DYNAMIC_DRAW,
		NONE
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
		// NOTE: If adding any types, entries must also be added to the following array!
		NONE
	};

	const char* DataTypeToString(DataType dataType);

	enum class TopologyMode
	{
		POINT_LIST,
		LINE_LIST,
		LINE_LOOP,
		LINE_STRIP,
		TRIANGLE_LIST,
		TRIANGLE_STRIP,
		TRIANGLE_FAN,
		NONE
	};

	struct DirectionalLight
	{
		glm::quat rotation = QUAT_UNIT; // Applied to unit vec (1,0,0) before being sent to shader
		glm::vec4 color = VEC4_ONE;
		u32 enabled = 1;
		real brightness = 1.0f;

		real shadowDarkness = 0.0f;
		bool bCastShadow = true;
		real shadowMapNearPlane = -80.0f;
		real shadowMapFarPlane = 100.0f;
		real shadowMapZoom = 30.0f;

		// Not used for rendering but allows users to position in the world
		glm::vec3 position = VEC3_ZERO;

		// DEBUG: (just for preview in ImGui)
		u32 shadowTextureID = 0;
	};

	struct PointLight
	{
		glm::vec4 position = VEC4_ZERO;
		glm::vec4 color = VEC4_ONE;
		u32 enabled = 1;
		real brightness = 500.0f;
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

		glm::vec4 colorMultiplier = { 1.0f, 1.0f, 1.0f, 1.0f };

		std::vector<std::pair<std::string, void*>> frameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs

		bool enableIrradianceSampler = false;
		bool generateIrradianceSampler = false;
		glm::vec2 generatedIrradianceCubemapSize = { 0.0f, 0.0f };
		MaterialID irradianceSamplerMatID = InvalidMaterialID; // The id of the material who has an irradiance sampler object (generateIrradianceSampler must be false)
		std::string environmentMapPath = "";

		bool enableBRDFLUT = false;
		bool renderToCubemap = true;

		std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT
		bool enableCubemapSampler = false;
		bool enableCubemapTrilinearFiltering = false;
		bool generateCubemapSampler = false;
		glm::vec2 generatedCubemapSize = { 0.0f, 0.0f };
		bool generateCubemapDepthBuffers = false;

		bool generatePrefilteredMap = false;
		bool enablePrefilteredMap = false;
		glm::vec2 generatedPrefilteredCubemapSize = { 0.0f, 0.0f };
		MaterialID prefilterMapSamplerMatID = InvalidMaterialID;

		bool generateReflectionProbeMaps = false;

		// PBR Constant values
		glm::vec3 constAlbedo = { 1.0f, 1.0f, 1.0f };
		real constMetallic = 0.0f;
		real constRoughness = 0.0f;
		real constAO = 0.0f;

		real textureScale = 1.0f;

		bool engineMaterial = false;
	};

	struct Material
	{
		bool Equals(const Material& other);

		static void ParseJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut);
		JSONObject Serialize() const;

		std::string name = "";

		ShaderID shaderID = InvalidShaderID;

		bool generateNormalSampler = false;
		bool enableNormalSampler = false;
		std::string normalTexturePath = "";

		// GBuffer samplers
		std::vector<std::pair<std::string, void*>> frameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs

		bool generateCubemapSampler = false;
		bool enableCubemapSampler = false;
		glm::vec2 cubemapSamplerSize = { 0, 0 };
		std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT

		// PBR constants
		glm::vec4 constAlbedo = { 1, 1, 1, 1 };
		real constMetallic = 0;
		real constRoughness = 0;
		real constAO = 1;

		// PBR samplers
		bool generateAlbedoSampler = false;
		bool enableAlbedoSampler = false;
		std::string albedoTexturePath = "";

		bool generateMetallicSampler = false;
		bool enableMetallicSampler = false;
		std::string metallicTexturePath = "";

		bool generateRoughnessSampler = false;
		bool enableRoughnessSampler = false;
		std::string roughnessTexturePath = "";

		bool generateAOSampler = false;
		bool enableAOSampler = false;
		std::string aoTexturePath = "";

		bool generateHDREquirectangularSampler = false;
		bool enableHDREquirectangularSampler = false;
		std::string hdrEquirectangularTexturePath = "";

		bool enableCubemapTrilinearFiltering = false;
		bool generateHDRCubemapSampler = false;

		bool enableIrradianceSampler = false;
		bool generateIrradianceSampler = false;
		glm::vec2 irradianceSamplerSize = { 0, 0 };
		std::string environmentMapPath = "";

		bool enablePrefilteredMap = false;
		bool generatePrefilteredMap = false;
		glm::vec2 prefilteredMapSize = { 0, 0 };

		bool enableBRDFLUT = false;
		bool renderToCubemap = true; // NOTE: This flag is currently ignored by GL renderer!

		bool generateReflectionProbeMaps = false;

		glm::vec4 colorMultiplier = { 1, 1, 1, 1 };

		// If true, this material shouldn't be removed when switching scenes
		bool engineMaterial = false;

		real textureScale = 1.0f;

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

		bool visible = true;
		bool visibleInSceneExplorer = true;

		CullFace cullFace = CullFace::BACK;
		// TODO: Rename to enableBackfaceCulling
		bool enableCulling = true;

		DepthTestFunc depthTestReadFunc = DepthTestFunc::LEQUAL;
		bool depthWriteEnable = true;

		bool editorObject = false;
	};

	struct Uniforms
	{
		Uniform uniforms;
		std::map<const char*, bool, strCmp> types;

		bool HasUniform(Uniform uniform) const;
		void AddUniform(Uniform uniform);
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

		bool deferred = false; // TODO: Replace this bool with just checking if numAttachments is larger than 1
		i32 subpass = 0;
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

		VertexAttributes vertexAttributes = 0;
		i32 numAttachments = 1; // How many output textures the fragment shader has
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
