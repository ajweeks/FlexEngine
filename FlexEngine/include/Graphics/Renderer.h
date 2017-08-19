#pragma once

#include <string>
#include <vector>

#include <glm\integer.hpp>
#include <glm\vec4.hpp>
#include <glm\mat4x4.hpp>

#include "GameContext.h"
#include "Typedefs.h"
#include "VertexBufferData.h"

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

		struct RenderObjectCreateInfo
		{
			VertexBufferData* vertexBufferData = nullptr;
			std::vector<glm::uint>* indices = nullptr;

			// Leave empty to not use
			std::string diffuseMapPath;
			std::string specularMapPath;
			std::string normalMapPath;

			glm::uint shaderIndex;

			CullFace cullFace = CullFace::BACK;

			// Leave empty to not use
			std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT
		};

		virtual void PostInitialize() = 0;

		virtual RenderID InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) = 0;

		virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) = 0;
		virtual void SetClearColor(float r, float g, float b) = 0;

		virtual void Update(const GameContext& gameContext) = 0;
		virtual void Draw(const GameContext& gameContext) = 0;
		virtual void ReloadShaders(GameContext& gameContext) = 0;

		virtual void OnWindowSize(int width, int height) = 0;

		virtual void SetVSyncEnabled(bool enableVSync) = 0;
		virtual void Clear(int flags, const GameContext& gameContext) = 0;
		virtual void SwapBuffers(const GameContext& gameContext) = 0;

		virtual void UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model) = 0;

		virtual int GetShaderUniformLocation(RenderID program, const std::string uniformName) = 0;
		virtual void SetUniform1f(int location, float val) = 0;

		virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size, Renderer::Type renderType, bool normalized,
			int stride, void* pointer) = 0;

		virtual void Destroy(RenderID renderID) = 0;

		struct SceneInfo
		{
			// Everything has to be aligned (16 byte? aka vec4)
			glm::vec4 m_LightDir;
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
} // namespace flex
