#pragma once

#include <glm\detail\type_int.hpp>

class GameContext;

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

	virtual void SetVSyncEnabled(bool enableVSync) = 0;
	virtual void Clear(int flags) = 0;
	virtual void SwapBuffers(const GameContext& gameContext) = 0;

	virtual void BindVertexArray(glm::uint vertexArrayObject) = 0;
	virtual void UseProgram(glm::uint program) = 0;
	virtual void BindBuffer(BufferTarget bufferTarget, glm::uint buffer) = 0;
	virtual void SetUniform1f(glm::uint uniform, float value) = 0;
	virtual void SetUniformMatrix4fv(glm::uint uniform, glm::uint count, bool transpose, float* values) = 0;
	virtual void EnableVertexAttribArray(glm::uint index) = 0;
	virtual void VertexAttribPointer(glm::uint index, int size, Type type, bool normalized, size_t stride, const void* pointer) = 0;
	virtual void GenVertexArrays(glm::uint count, glm::uint* arrays) = 0;
	virtual void GenBuffers(glm::uint count, glm::uint* buffers) = 0;
	virtual void BufferData(BufferTarget bufferTarget, glm::uint size, const void* data, UsageFlag usage) = 0;

	virtual void DrawArrays(Mode mode, glm::uint first, glm::uint count) = 0;
	virtual void DrawElements(Mode mode, glm::uint count, Type type, const void* indices) = 0;

	virtual void DeleteVertexArrays(glm::uint count, glm::uint* arrays) = 0;

	virtual int GetAttribLocation(glm::uint program, const char* name) = 0;
	virtual int GetUniformLocation(glm::uint program, const char* name) = 0;

private:
	Renderer& operator=(const Renderer&) = delete;
	Renderer(const Renderer&) = delete;

};