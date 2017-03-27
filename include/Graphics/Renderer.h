#pragma once

#include "Vertex.h"

#include <glm\detail\type_int.hpp>

#include <vector>

struct GameContext;

class Renderer
{
public:
	Renderer(GameContext& gameContext);
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

	enum class Mode
	{
		POINTS,
		LINES,
		LINE_LOOP,
		LINE_STRIP,
		TRIANGLES,
		TRIANGLE_STRIP,
		TRIANGLE_FAN,
		QUADS
	};

	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices) = 0;
	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices, std::vector<glm::uint>* indices) = 0;
	
	virtual void Draw(glm::uint renderID) = 0;

	virtual void SetVSyncEnabled(bool enableVSync) = 0;
	virtual void Clear(int flags) = 0;
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