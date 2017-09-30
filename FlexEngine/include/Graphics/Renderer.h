#pragma once

#include <string>
#include <vector>
#include <array>
#include <map>

#include <glm/integer.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include "GameContext.h"
#include "Typedefs.h"
#include "VertexBufferData.h"
#include "Transform.h"

namespace flex
{
	class Renderer
	{
	public:
		Renderer();
		virtual ~Renderer();

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

		struct Material
		{
			std::string name;

			ShaderID shaderID;

			bool useDiffuseSampler = false;
			std::string diffuseTexturePath;

			bool useSpecularSampler = false;
			std::string specularTexturePath;

			bool useNormalSampler = false;
			std::string normalTexturePath;

			// GBuffer samplers
			bool usePositionFrameBufferSampler = false;
			bool useNormalFrameBufferSampler = false;
			bool useDiffuseSpecularFrameBufferSampler = false;

			bool useCubemapTexture = false;
			std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT

			// PBR constants
			glm::vec3 constAlbedo;
			float constMetallic;
			float constRoughness;
			float constAO;

			// PBR samplers
			bool useAlbedoSampler = false;
			std::string albedoTexturePath;

			bool useMetallicSampler = false;
			std::string metallicTexturePath;

			bool useRoughnessSampler = false;
			std::string roughnessTexturePath;

			bool useAOSampler = false;
			std::string aoTexturePath;
		};

		// Info that stays consistent across all renderers
		struct RenderObjectInfo
		{
			std::string name;
			std::string materialName;
			Transform* transform = nullptr;

			// Parent, children, etc.
		};

		struct RenderObjectCreateInfo
		{
			MaterialID materialID;

			VertexBufferData* vertexBufferData = nullptr;
			std::vector<glm::uint>* indices = nullptr;

			std::string name;
			Transform* transform;

			CullFace cullFace = CullFace::BACK;
		};

		struct Uniforms
		{
			/*
			Possible uniform names:

				model
				modelInvTranspose
				modelViewProj
				projection
				view
				viewInv
				viewProjection
				camPos
				dirLight
				pointLights
				useDiffuseSampler
				diffuseSampler
				useNormalSampler
				normalSampler
				useSpecularSampler
				specularSampler
				useCubemapSampler
				cubemapSampler
				positionFrameBufferSampler
				diffuseSpecularFrameBufferSampler
				normalFrameBufferSampler
				material
					useAlbedoSampler
					albedoSampler
					albedo				(constant value to use when not using sampler)
					useMetallicSampler
					metallicSampler
					metallic			(constant value to use when not using sampler)
					useRoughnessSampler
					roughnessSampler
					roughness			(constant value to use when not using sampler)
					useAOSampler
					aoSampler
					ao
			*/
			std::map<std::string, bool> types;

			inline bool HasUniform(const std::string& name) const;
			void AddUniform(const std::string& name, bool value);
			glm::uint CalculateSize(int pointLightCount);
		};

		struct Shader
		{
			Shader();
			Shader(const std::string& vertexShaderFilePath, const std::string& fragmentShaderFilePath);

			glm::uint program;

			std::string vertexShaderFilePath;
			std::string fragmentShaderFilePath;

			std::vector<char> vertexShaderCode;
			std::vector<char> fragmentShaderCode;

			Uniforms constantBufferUniforms;
			Uniforms dynamicBufferUniforms;

			bool deferred = false; // TODO: Replace this bool with just checking if numAttachments is larger than 1

			VertexAttributes vertexAttributes;
			int numAttachments = 1; // How many output textures the fragment shader has
		};

		struct MaterialCreateInfo
		{
			// Required fields:
			ShaderID shaderID;

			// Optional fields:
			std::string name;

			std::string diffuseTexturePath;
			std::string specularTexturePath;
			std::string normalTexturePath;
			std::string albedoTexturePath;
			std::string metallicTexturePath;
			std::string roughnessTexturePath;
			std::string aoTexturePath;

			bool usePositionFrameBufferSampler = false;
			glm::uint positionFrameBufferSamplerID;
			bool useNormalFrameBufferSampler = false;
			glm::uint normalFrameBufferSamplerID;
			bool useDiffuseSpecularFrameBufferSampler = false;
			glm::uint diffuseSpecularFrameBufferSamplerID;

			std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT

			// PBR Constant colors
			glm::vec3 constAlbedo;
			float constMetallic;
			float constRoughness;
			float constAO;

			// PBR textures
			bool useAlbedoSampler = false;
			glm::vec3 albedoSamplerID;
			bool useMetallicSampler = false;
			glm::uint metallicSamplerID;
			bool useRoughnessSampler = false;
			glm::uint roughnessSamplerID;
			bool useAOSampler = false;
			glm::uint aoSamplerID;
		};

		virtual void PostInitialize(const GameContext& gameContext) = 0;

		virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) = 0;
		virtual RenderID InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) = 0;
		virtual void PostInitializeRenderObject(RenderID renderID) = 0; // Only call when creating objects after calling PostInitialize()
		virtual DirectionalLightID InitializeDirectionalLight(const DirectionalLight& dirLight) = 0;
		virtual PointLightID InitializePointLight(const PointLight& pointLight) = 0;

		virtual DirectionalLight& GetDirectionalLight(DirectionalLightID dirLightID) = 0;
		virtual PointLight& GetPointLight(PointLightID pointLightID) = 0;
		virtual std::vector<PointLight>& GetAllPointLights() = 0;

		virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) = 0;
		virtual void SetClearColor(float r, float g, float b) = 0;

		virtual void Update(const GameContext& gameContext) = 0;
		virtual void Draw(const GameContext& gameContext) = 0;
		virtual void ReloadShaders(GameContext& gameContext) = 0;

		virtual void OnWindowSize(int width, int height) = 0;

		virtual void SetVSyncEnabled(bool enableVSync) = 0;

		virtual void UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model) = 0;

		virtual glm::uint GetRenderObjectCount() const = 0;
		virtual glm::uint GetRenderObjectCapacity() const = 0;

		virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size, Renderer::Type renderType, bool normalized,
			int stride, void* pointer) = 0;

		virtual void Destroy(RenderID renderID) = 0;

		virtual void GetRenderObjectInfos(std::vector<RenderObjectInfo>& vec) = 0;

		// ImGUI functions
		virtual void ImGui_Init(const GameContext& gameContext) = 0;
		virtual void ImGui_NewFrame(const GameContext& gameContext) = 0;
		virtual void ImGui_Render() = 0;
		virtual void ImGui_ReleaseRenderObjects() = 0;

	private:
		Renderer& operator=(const Renderer&) = delete;
		Renderer(const Renderer&) = delete;

	};
} // namespace flex
