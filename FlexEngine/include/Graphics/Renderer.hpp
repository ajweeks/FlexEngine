#pragma once

#include <string>
#include <vector>
#include <array>
#include <map>

#include <glm/integer.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include "GameContext.hpp"
#include "Typedefs.hpp"
#include "VertexBufferData.hpp"
#include "Transform.hpp"

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
			glm::vec4 direction;

			glm::vec4 color = glm::vec4(1.0f);

			glm::uint enabled = 1;
			float padding[3];
		};

		struct PointLight
		{
			glm::vec4 position = glm::vec4(0.0f);

			// Vec4 for padding's sake
			glm::vec4 color = glm::vec4(1.0f);

			glm::uint enabled = 1;
			float padding[3];
		};

		// TODO: Is setting all the members to false necessary?
		// TODO: Straight up copy most of these with a memcpy?
		struct MaterialCreateInfo
		{
			std::string shaderName;
			std::string name;

			std::string diffuseTexturePath;
			std::string specularTexturePath;
			std::string normalTexturePath;
			std::string albedoTexturePath;
			std::string metallicTexturePath;
			std::string roughnessTexturePath;
			std::string aoTexturePath;
			std::string hdrEquirectangularTexturePath;

			bool generateDiffuseSampler;
			bool enableDiffuseSampler;
			bool generateSpecularSampler;
			bool enableSpecularSampler;
			bool generateNormalSampler;
			bool enableNormalSampler;
			bool generateAlbedoSampler;
			bool enableAlbedoSampler;
			bool generateMetallicSampler;
			bool enableMetallicSampler;
			bool generateRoughnessSampler;
			bool enableRoughnessSampler;
			bool generateAOSampler;
			bool enableAOSampler;
			bool generateHDREquirectangularSampler;
			bool enableHDREquirectangularSampler;
			bool generateHDRCubemapSampler;

			std::vector<std::pair<std::string, void*>> frameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs

			bool enableIrradianceSampler;
			bool generateIrradianceSampler;
			glm::uvec2 generatedIrradianceCubemapSize;
			MaterialID irradianceSamplerMatID; // The id of the material who has an irradiance sampler object (generateIrradianceSampler must be false)

			bool enableBRDFLUT;

			std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT
			bool enableCubemapSampler;
			bool enableCubemapTrilinearFiltering;
			bool generateCubemapSampler;
			glm::uvec2 generatedCubemapSize;
			bool generateCubemapDepthBuffers;

			bool generatePrefilteredMap;
			bool enablePrefilteredMap;
			glm::uvec2 generatedPrefilteredCubemapSize;
			MaterialID prefilterMapSamplerMatID;

			bool generateReflectionProbeMaps;

			// PBR Constant colors
			glm::vec3 constAlbedo;
			float constMetallic;
			float constRoughness;
			float constAO;
		};

		struct Material
		{
			std::string name;

			ShaderID shaderID;

			bool generateDiffuseSampler;
			bool enableDiffuseSampler;
			std::string diffuseTexturePath;

			bool generateSpecularSampler;
			bool enableSpecularSampler;
			std::string specularTexturePath;

			bool generateNormalSampler;
			bool enableNormalSampler;
			std::string normalTexturePath;

			// GBuffer samplers
			std::vector<std::pair<std::string, void*>> frameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs

			bool generateCubemapSampler;   // Cubemap is enabled 
			bool enableCubemapSampler;   // Cubemap is enabled 
			glm::uvec2 cubemapSamplerSize;
			std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT

			// PBR constants
			glm::vec4 constAlbedo;
			float constMetallic;
			float constRoughness;
			float constAO;

			// PBR samplers
			bool generateAlbedoSampler;
			bool enableAlbedoSampler;
			std::string albedoTexturePath;

			bool generateMetallicSampler;
			bool enableMetallicSampler;
			std::string metallicTexturePath;

			bool generateRoughnessSampler;
			bool enableRoughnessSampler;
			std::string roughnessTexturePath;

			bool generateAOSampler;
			bool enableAOSampler;
			std::string aoTexturePath;

			bool generateHDREquirectangularSampler;
			bool enableHDREquirectangularSampler;
			std::string hdrEquirectangularTexturePath;

			bool generateHDRCubemapSampler;

			bool enableIrradianceSampler;
			bool generateIrradianceSampler;
			glm::uvec2 irradianceSamplerSize;

			bool enablePrefilteredMap;
			bool generatePrefilteredMap;
			glm::uvec2 prefilteredMapSize;

			bool enableBRDFLUT;

			bool generateReflectionProbeMaps;

			// TODO: Make this more dynamic!
			struct PushConstantBlock
			{
				glm::mat4 mvp;
			};
			PushConstantBlock pushConstantBlock;
		};

		struct RenderObjectCreateInfo
		{
			MaterialID materialID;

			VertexBufferData* vertexBufferData = nullptr;
			std::vector<glm::uint>* indices = nullptr;

			std::string name;
			Transform* transform;

			CullFace cullFace = CullFace::BACK;
			bool enableCulling = true;

			DepthTestFunc depthTestReadFunc = DepthTestFunc::LEQUAL;
			bool depthWriteEnable = true;
		};

		struct Uniforms
		{
			std::map<std::string, bool> types;

			inline bool HasUniform(const std::string& name) const;
			void AddUniform(const std::string& name);
			void RemoveUniform(const std::string& name);
			glm::uint CalculateSize(int pointLightCount);
		};

		struct Shader
		{
			std::string name;

			std::string vertexShaderFilePath;
			std::string fragmentShaderFilePath;

			std::vector<char> vertexShaderCode;
			std::vector<char> fragmentShaderCode;

			Uniforms constantBufferUniforms;
			Uniforms dynamicBufferUniforms;

			bool deferred; // TODO: Replace this bool with just checking if numAttachments is larger than 1
			int subpass = 0;
			bool depthWriteEnable = true;

			// These variables should be set to true when the shader has these uniforms
			bool needDiffuseSampler;
			bool needSpecularSampler;
			bool needNormalSampler;
			bool needCubemapSampler;
			bool needAlbedoSampler;
			bool needMetallicSampler;
			bool needRoughnessSampler;
			bool needAOSampler;
			bool needHDREquirectangularSampler;
			bool needIrradianceSampler;
			bool needPrefilteredMap;
			bool needBRDFLUT;
			bool needPushConstantBlock;

			VertexAttributes vertexAttributes;
			int numAttachments = 1; // How many output textures the fragment shader has
		};

		virtual void PostInitialize(const GameContext& gameContext) = 0;

		virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) = 0;
		virtual RenderID InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) = 0;
		virtual void PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID) = 0; // Only call when creating objects after calling PostInitialize()
		virtual DirectionalLightID InitializeDirectionalLight(const DirectionalLight& dirLight) = 0;
		virtual PointLightID InitializePointLight(const PointLight& pointLight) = 0;

		virtual DirectionalLight& GetDirectionalLight(DirectionalLightID dirLightID) = 0;
		virtual PointLight& GetPointLight(PointLightID pointLightID) = 0;

		virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) = 0;
		virtual void SetClearColor(float r, float g, float b) = 0;

		virtual void Update(const GameContext& gameContext) = 0;
		virtual void Draw(const GameContext& gameContext) = 0;
		virtual void DrawImGuiItems(const GameContext& gameContext) = 0;

		virtual void ReloadShaders(GameContext& gameContext) = 0;

		virtual void OnWindowSize(int width, int height) = 0;

		virtual void SetRenderObjectVisible(RenderID renderID, bool visible) = 0;

		virtual void SetVSyncEnabled(bool enableVSync) = 0;

		virtual glm::uint GetRenderObjectCount() const = 0;
		virtual glm::uint GetRenderObjectCapacity() const = 0;

		virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size, Renderer::Type renderType, bool normalized,
			int stride, void* pointer) = 0;

		virtual void Destroy(RenderID renderID) = 0;

		// ImGUI functions
		virtual void ImGui_Init(const GameContext& gameContext) = 0;
		virtual void ImGui_NewFrame(const GameContext& gameContext) = 0;
		virtual void ImGui_Render() = 0;
		virtual void ImGui_ReleaseRenderObjects() = 0;

	protected:
		std::vector<PointLight> m_PointLights;
		DirectionalLight m_DirectionalLight;


	private:
		Renderer& operator=(const Renderer&) = delete;
		Renderer(const Renderer&) = delete;

	};
} // namespace flex
