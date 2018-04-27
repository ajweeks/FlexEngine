#pragma once

#include <string>
#include <vector>
#include <array>
#include <map>

#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include "GameContext.hpp"

#include "VertexBufferData.hpp"
#include "Transform.hpp"
#include "Physics/PhysicsDebuggingSettings.hpp"

namespace flex
{
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
		BYTE,
		UNSIGNED_BYTE,
		SHORT,
		UNSIGNED_SHORT,
		INT,
		UNSIGNED_INT,
		FLOAT,
		DOUBLE,
		NONE
	};

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
		// TODO: Add brightness multiplier here
		glm::vec4 direction = { 0, 0, 1, 0 };

		glm::vec4 color = glm::vec4(1.0f);

		u32 enabled = 1;

		real brightness = 1.0f;
	};

	struct PointLight
	{
		glm::vec4 position = glm::vec4(0.0f);

		glm::vec4 color = glm::vec4(1.0f);

		u32 enabled = 1;

		real brightness = 1.0f;
		std::string name;
	};

	// TODO: Is setting all the members to false necessary?
	// TODO: Straight up copy most of these with a memcpy?
	struct MaterialCreateInfo
	{
		std::string shaderName = "";
		std::string name = "";

		std::string diffuseTexturePath = "";
		std::string normalTexturePath = "";
		std::string albedoTexturePath = "";
		std::string metallicTexturePath = "";
		std::string roughnessTexturePath = "";
		std::string aoTexturePath = "";
		std::string hdrEquirectangularTexturePath = "";

		bool generateDiffuseSampler = false;
		bool enableDiffuseSampler = false;
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

		glm::vec4 colorMultiplier = { 1, 1, 1, 1 };

		std::vector<std::pair<std::string, void*>> frameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs

		bool enableIrradianceSampler = false;
		bool generateIrradianceSampler = false;
		glm::uvec2 generatedIrradianceCubemapSize = { 0, 0 };
		MaterialID irradianceSamplerMatID = InvalidMaterialID; // The id of the material who has an irradiance sampler object (generateIrradianceSampler must be false)
		std::string environmentMapPath = "";

		bool enableBRDFLUT = false;
		bool renderToCubemap = true;

		std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT
		bool enableCubemapSampler = false;
		bool enableCubemapTrilinearFiltering = false;
		bool generateCubemapSampler = false;
		glm::uvec2 generatedCubemapSize = { 0, 0 };
		bool generateCubemapDepthBuffers = false;

		bool generatePrefilteredMap = false;
		bool enablePrefilteredMap = false;
		glm::uvec2 generatedPrefilteredCubemapSize = { 0, 0 };
		MaterialID prefilterMapSamplerMatID = InvalidMaterialID;

		bool generateReflectionProbeMaps = false;

		// PBR Constant values
		glm::vec3 constAlbedo = { 0, 0, 0 };
		real constMetallic = 0;
		real constRoughness = 0;
		real constAO = 0;
	};

	struct Material
	{
		std::string name = "";

		ShaderID shaderID = InvalidShaderID;

		bool generateDiffuseSampler = false;
		bool enableDiffuseSampler = false;
		std::string diffuseTexturePath = "";

		bool generateNormalSampler = false;
		bool enableNormalSampler = false;
		std::string normalTexturePath = "";

		// GBuffer samplers
		std::vector<std::pair<std::string, void*>> frameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs

		bool generateCubemapSampler = false;
		bool enableCubemapSampler = false;
		glm::uvec2 cubemapSamplerSize = { 0, 0 };
		std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT

		// PBR constants
		glm::vec4 constAlbedo = { 0, 0, 0, 0 };
		real constMetallic = 0;
		real constRoughness = 0;
		real constAO = 0;

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

		bool generateHDRCubemapSampler = false;

		bool enableIrradianceSampler = false;
		bool generateIrradianceSampler = false;
		glm::uvec2 irradianceSamplerSize = { 0,0 };
		std::string environmentMapPath = "";

		bool enablePrefilteredMap = false;
		bool generatePrefilteredMap = false;
		glm::uvec2 prefilteredMapSize = { 0,0 };

		bool enableBRDFLUT = false;
		bool renderToCubemap = true; // NOTE: This flag is currently ignored by GL renderer!

		bool generateReflectionProbeMaps = false;

		glm::vec4 colorMultiplier = { 1, 1, 1, 1 };

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

		std::string name = "";

		// If this field is null then this object will be given an identity transform which can not change
		Transform* transform = nullptr;

		bool visible = true;
		bool visibleInSceneExplorer = true;

		CullFace cullFace = CullFace::BACK;
		// TODO: Rename to enableBackfaceCulling
		bool enableCulling = true;

		DepthTestFunc depthTestReadFunc = DepthTestFunc::LEQUAL;
		bool depthWriteEnable = true;
	};

	struct Uniforms
	{
		std::map<std::string, bool> types;

		bool HasUniform(const std::string& name) const;
		void AddUniform(const std::string& name);
		void RemoveUniform(const std::string& name);
		u32 CalculateSize(i32 PointLightCount);
	};

	struct Shader
	{
		Shader(const std::string& name,
			const std::string& vertexShaderFilePath,
			const std::string& fragmentShaderFilePath);

		std::string name = "";

		std::string vertexShaderFilePath = "";
		std::string fragmentShaderFilePath = "";

		std::vector<char> vertexShaderCode = {};
		std::vector<char> fragmentShaderCode = {};

		Uniforms constantBufferUniforms = {};
		Uniforms dynamicBufferUniforms = {};

		bool deferred = false; // TODO: Replace this bool with just checking if numAttachments is larger than 1
		i32 subpass = 0;
		bool depthWriteEnable = true;
		bool translucent = false;

		// These variables should be set to true when the shader has these uniforms
		bool needDiffuseSampler = false;
		bool needNormalSampler = false;
		bool needCubemapSampler = false;
		bool needAlbedoSampler = false;
		bool needMetallicSampler = false;
		bool needRoughnessSampler = false;
		bool needAOSampler = false;
		bool needHDREquirectangularSampler = false;
		bool needIrradianceSampler = false;
		bool needPrefilteredMap = false;
		bool needBRDFLUT = false;
		bool needPushConstantBlock = false;

		VertexAttributes vertexAttributes = 0;
		i32 numAttachments = 1; // How many output textures the fragment shader has
	};

} // namespace flex
