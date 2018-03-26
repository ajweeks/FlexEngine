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

/*
 * TODO:
 * - Add near & far members for child classes to use
 * 
*/

namespace flex
{
	class Renderer
	{
	public:
		Renderer();
		virtual ~Renderer();

		// TODO: Remove unused enums
		enum class ClearFlag
		{
			COLOR = (1 << 0),
			DEPTH = (1 << 1),
			STENCIL = (1 << 2)
		};

		enum class CullFace
		{
			BACK,
			FRONT,
			FRONT_AND_BACK
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
			ELEMENT_ARRAY_BUFFER
		};

		enum class UsageFlag
		{
			STATIC_DRAW,
			DYNAMIC_DRAW
		};

		enum class Type
		{
			BYTE,
			UNSIGNED_BYTE,
			SHORT,
			UNSIGNED_SHORT,
			INT,
			UNSIGNED_INT,
			FLOAT,
			DOUBLE
		};

		enum class TopologyMode
		{
			POINT_LIST,
			LINE_LIST,
			LINE_LOOP,
			LINE_STRIP,
			TRIANGLE_LIST,
			TRIANGLE_STRIP,
			TRIANGLE_FAN
		};

		struct DirectionalLight
		{
			glm::vec4 direction = { 0, 0, 0, 0 };

			glm::vec4 color = glm::vec4(1.0f);

			u32 enabled = 1;
			real padding[3];
		};

		struct PointLight
		{
			glm::vec4 position = glm::vec4(0.0f);

			// Vec4 for padding's sake
			glm::vec4 color = glm::vec4(1.0f);

			u32 enabled = 1;
			real padding[3];
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

			glm::vec4 colorMultiplier = {1, 1, 1, 1};

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

			// PBR Constant colors
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
			glm::vec4 constAlbedo = { 0, 0, 0, 0};
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
			Transform* transform = nullptr;

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

		virtual void PostInitialize(const GameContext& gameContext) = 0;

		virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) = 0;
		virtual RenderID InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) = 0;
		virtual void PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID) = 0; // Only call when creating objects after calling PostInitialize()
		virtual DirectionalLightID InitializeDirectionalLight(const DirectionalLight& dirLight) = 0;
		virtual PointLightID InitializePointLight(const PointLight& pointLight) = 0;

		virtual DirectionalLight& GetDirectionalLight(DirectionalLightID dirLightID) = 0;
		virtual PointLight& GetPointLight(PointLightID pointLight) = 0;

		virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) = 0;
		virtual void SetClearColor(real r, real g, real b) = 0;

		virtual void Update(const GameContext& gameContext) = 0;
		virtual void Draw(const GameContext& gameContext) = 0;
		virtual void DrawImGuiItems(const GameContext& gameContext) = 0;

		virtual void UpdateRenderObjectVertexData(RenderID renderID) = 0;

		virtual void ReloadShaders(GameContext& gameContext) = 0;

		virtual void OnWindowSize(i32 width, i32 height) = 0;

		virtual void SetRenderObjectVisible(RenderID renderID, bool visible) = 0;

		virtual void SetVSyncEnabled(bool enableVSync) = 0;

		virtual u32 GetRenderObjectCount() const = 0;
		virtual u32 GetRenderObjectCapacity() const = 0;

		virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size, Renderer::Type renderType, bool normalized,
			i32 stride, void* pointer) = 0;

		virtual void SetSkyboxMaterial(MaterialID skyboxMaterialID) = 0;
		virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) = 0;
		virtual void SetReflectionProbeMaterial(MaterialID reflectionProbeMaterialID);

		virtual Material& GetMaterial(MaterialID matID) = 0;
		virtual Shader& GetShader(ShaderID shaderID)  = 0;

		virtual void Destroy(RenderID renderID) = 0;

		virtual void ImGuiNewFrame() = 0;

		PhysicsDebuggingSettings& GetPhysicsDebuggingSettings();

		static const u32 MAX_TEXTURE_DIM = 65536;

	protected:
		std::vector<PointLight> m_PointLights;
		DirectionalLight m_DirectionalLight;

		struct DrawCallInfo
		{
			RenderID cubemapObjectRenderID = InvalidRenderID;
			bool renderToCubemap = false;
			bool deferred = false;
		};
		
		MaterialID m_ReflectionProbeMaterialID = InvalidMaterialID; // Set by the user via SetReflecionProbeMaterial

		bool m_VSyncEnabled = true;
		PhysicsDebuggingSettings m_PhysicsDebuggingSettings;

	private:
		Renderer& operator=(const Renderer&) = delete;
		Renderer(const Renderer&) = delete;

	};
} // namespace flex
