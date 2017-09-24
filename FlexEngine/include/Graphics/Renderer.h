#pragma once

#include <string>
#include <vector>
#include <array>

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
			glm::vec3 direction;
			
			bool enabled = true;

			glm::vec3 ambientCol = glm::vec3(0.0f);
			glm::vec3 diffuseCol = glm::vec3(1.0f);
			glm::vec3 specularCol = glm::vec3(1.0f);
		};

		struct PointLight
		{
			glm::vec3 position = glm::vec3(0.0f);

			bool enabled = true;

			// TODO: Just set brightness here and interpolate along correct values in shader
			float constant = 1.0f;
			float linear = 0.022f;
			float quadratic = 0.0019f;

			glm::vec3 ambientCol = glm::vec3(0.0f);
			glm::vec3 diffuseCol = glm::vec3(1.0f);
			glm::vec3 specularCol = glm::vec3(1.0f);
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

		struct Uniform
		{
			enum Type : glm::uint
			{
				NONE = 0,
				UNIFORM_BUFFER_CONSTANT = (1 << 0),
				UNIFORM_BUFFER_DYNAMIC = (1 << 1),
				PROJECTION_MAT4 = (1 << 2),
				VIEW_MAT4 = (1 << 3),
				VIEW_INV_MAT4 = (1 << 4),
				VIEW_PROJECTION_MAT4 = (1 << 5),
				MODEL_MAT4 = (1 << 6),
				MODEL_INV_TRANSPOSE_MAT4 = (1 << 7),
				MODEL_VIEW_PROJECTION_MAT4 = (1 << 8),
				CAM_POS_VEC4 = (1 << 9),
				VIEW_DIR_VEC4 = (1 << 10),
				DIR_LIGHT = (1 << 11),
				POINT_LIGHTS_VEC = (1 << 12),
				AMBIENT_COLOR_VEC4 = (1 << 13),
				SPECULAR_COLOR_VEC4 = (1 << 14),
				USE_DIFFUSE_TEXTURE_INT = (1 << 15), // bool for toggling texture usage
				DIFFUSE_TEXTURE_SAMPLER = (1 << 16), // texture itself
				USE_NORMAL_TEXTURE_INT = (1 << 17),
				NORMAL_TEXTURE_SAMPLER = (1 << 18),
				USE_SPECULAR_TEXTURE_INT = (1 << 19),
				SPECULAR_TEXTURE_SAMPLER = (1 << 20),
				USE_CUBEMAP_TEXTURE_INT = (1 << 21),
				CUBEMAP_TEXTURE_SAMPLER = (1 << 22),
				POSITION_TEXTURE_SAMPLER = (1 << 23),
				DIFFUSE_SPECULAR_TEXTURE_SAMPLER = (1 << 24),

				MAX_ENUM = (1 << 30)
			};

			static bool HasUniform(Type elements, Type uniform);
			static glm::uint CalculateSize(Type elements);
		};

		struct Shader
		{
			glm::uint program;

			std::string vertexShaderFilePath;
			std::string fragmentShaderFilePath;

			Uniform::Type constantBufferUniforms;
			Uniform::Type dynamicBufferUniforms;

			bool deferred = false;
		};

		struct MaterialCreateInfo
		{
			// Required fields:
			glm::uint shaderIndex;

			// Optional fields:
			std::string name;

			std::string diffuseTexturePath;
			std::string specularTexturePath;
			std::string normalTexturePath;

			bool usePositionSampler = false;
			glm::uint positionSamplerID;
			bool useNormalSampler = false;
			glm::uint normalSamplerID;
			bool useDiffuseSpecularSampler = false;
			glm::uint diffuseSpecularSamplerID;

			std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT
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

		virtual void SetFloat(ShaderID shaderID, const std::string& valName, float val) = 0;
		virtual void SetVec2f(ShaderID shaderID, const std::string& vecName, const glm::vec2& vec) = 0;
		virtual void SetVec3f(ShaderID shaderID, const std::string& vecName, const glm::vec3& vec) = 0;
		virtual void SetVec4f(ShaderID shaderID, const std::string& vecName, const glm::vec4& vec) = 0;
		virtual void SetMat4f(ShaderID shaderID, const std::string& matName, const glm::mat4& mat) = 0;

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

		struct SceneInfo
		{
			// Everything has to be aligned (16 byte? aka vec4) ( Or not? )
			glm::vec3 m_LightDir;
			glm::vec4 m_AmbientColor;
			glm::vec4 m_SpecularColor;
			// sky box, other lights, wireframe, etc...
		};
		SceneInfo& GetSceneInfo();

	protected:
		SceneInfo m_SceneInfo;

	private:
		Renderer& operator=(const Renderer&) = delete;
		Renderer(const Renderer&) = delete;

	};

	Renderer::Uniform::Type operator|(const Renderer::Uniform::Type& lhs, const Renderer::Uniform::Type& rhs);

} // namespace flex
