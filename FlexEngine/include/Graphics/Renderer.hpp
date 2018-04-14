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
#include "RendererTypes.hpp"

namespace flex
{
	class Renderer
	{
	public:
		Renderer();
		virtual ~Renderer();

		virtual void Initialize(const GameContext& gameContext) = 0;
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

		virtual void OnWindowSizeChanged(i32 width, i32 height) = 0;

		virtual void SetRenderObjectVisible(RenderID renderID, bool visible) = 0;

		virtual void SetVSyncEnabled(bool enableVSync) = 0;
		virtual bool GetVSyncEnabled() = 0;

		virtual u32 GetRenderObjectCount() const = 0;
		virtual u32 GetRenderObjectCapacity() const = 0;

		virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size, DataType dataType, bool normalized,
			i32 stride, void* pointer) = 0;

		virtual void SetSkyboxMaterial(MaterialID skyboxMaterialID) = 0;
		virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) = 0;
		virtual void SetReflectionProbeMaterial(MaterialID reflectionProbeMaterialID);

		virtual Material& GetMaterial(MaterialID matID) = 0;
		virtual Shader& GetShader(ShaderID shaderID)  = 0;

		virtual void DestroyRenderObject(RenderID renderID) = 0;

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
