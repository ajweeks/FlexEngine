#pragma once

#include "Vertex.h"

#include <glm\detail\type_int.hpp>

#include <vector>
#include <string>

struct GameContext;
struct VertexBufferData;

class Renderer
{
public:
	Renderer();
	virtual ~Renderer();

	struct RenderObjectCreateInfo
	{
		VertexBufferData* vertexBufferData = nullptr;
		std::vector<glm::uint>* indices = nullptr;

		// Leave empty to not use
		std::string diffuseMapPath;
		std::string specularMapPath;
		std::string normalMapPath;

		glm::uint shaderIndex;
	};

	enum class ClearFlag
	{
		COLOR = (1 << 0), 
		DEPTH = (1 << 1),
		STENCIL = (1 << 2)
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

	virtual void PostInitialize() = 0;

	virtual glm::uint Initialize(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) = 0;

	virtual void SetTopologyMode(glm::uint renderID, TopologyMode topology) = 0;
	virtual void SetClearColor(float r, float g, float b) = 0;

	virtual void Update(const GameContext& gameContext) = 0;
	virtual void Draw(const GameContext& gameContext, glm::uint renderID) = 0;
	virtual void ReloadShaders(GameContext& gameContext) = 0;

	virtual void OnWindowSize(int width, int height) = 0;

	virtual void SetVSyncEnabled(bool enableVSync) = 0;
	virtual void Clear(int flags, const GameContext& gameContext) = 0;
	virtual void SwapBuffers(const GameContext& gameContext) = 0;

	virtual void UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4& model) = 0;
	
	virtual int GetShaderUniformLocation(glm::uint program, const std::string uniformName) = 0;
	virtual void SetUniform1f(glm::uint location, float val) = 0;

	virtual glm::uint GetProgram(glm::uint renderID) = 0;
	virtual void DescribeShaderVariable(glm::uint renderID, glm::uint program, const std::string& variableName, int size, Renderer::Type renderType, bool normalized,
		int stride, void* pointer) = 0;

	virtual void Destroy(glm::uint renderID) = 0;

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