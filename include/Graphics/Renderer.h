#pragma once

#include "Vertex.h"
#include "VertexBufferData.h"

#include <glm\detail\type_int.hpp>

#include <vector>

struct GameContext;

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

	enum class CullMode
	{
		CULL_BACK,
		CULL_FRONT,
		CULL_NONE
	};

	virtual void PostInitialize() = 0;

	virtual glm::uint Initialize(const GameContext& gameContext, const VertexBufferData& vertexData) = 0;
	virtual glm::uint Initialize(const GameContext& gameContext, const VertexBufferData& vertexData, std::vector<glm::uint>* indices) = 0;

	virtual void SetTopologyMode(glm::uint renderID, TopologyMode topology) = 0;
	virtual void SetCullMode(glm::uint renderID, CullMode cullMode) = 0;
	virtual void SetClearColor(float r, float g, float b) = 0;

	virtual void Draw(const GameContext& gameContext, glm::uint renderID) = 0;

	virtual void OnWindowSize(int width, int height) = 0;

	virtual void SetVSyncEnabled(bool enableVSync) = 0;
	virtual void Clear(int flags, const GameContext& gameContext) = 0;
	virtual void SwapBuffers(const GameContext& gameContext) = 0;

	virtual void UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4x4& model) = 0;
	
	virtual int GetShaderUniformLocation(glm::uint program, const std::string uniformName) = 0;
	virtual void SetUniform1f(glm::uint location, float val) = 0;

	virtual void DescribeShaderVariable(glm::uint renderID, glm::uint program, const std::string& variableName, int size, Renderer::Type renderType, bool normalized,
		int stride, void* pointer) = 0;

	virtual void Destroy(glm::uint renderID) = 0;

private:
	Renderer& operator=(const Renderer&) = delete;
	Renderer(const Renderer&) = delete;

};