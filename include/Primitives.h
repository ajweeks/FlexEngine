#pragma once

#include <glm\vec2.hpp>
#include <glm\vec3.hpp>
#include <glm\vec4.hpp>

struct VertexPosCol
{
	VertexPosCol() {};
	VertexPosCol(float x, float y, float z, float w, float r, float g, float b, float a) :
		pos(x, y, z, w), col(r, g, b, a)
	{}
	VertexPosCol(glm::vec4 pos, glm::vec4 col) :
		pos(pos), col(col) 
	{}

	glm::vec4 pos;
	glm::vec4 col;
};

struct VertexPosColUV
{
	VertexPosColUV() {};
	VertexPosColUV(float x, float y, float z, float w, float r, float g, float b, float a, float u, float v) :
		pos(x, y, z, w), col(r, g, b, a), uv(u, v)
	{}
	VertexPosColUV(glm::vec4 pos, glm::vec4 col, glm::vec2 uv) :
		pos(pos), col(col), uv(uv)
	{}

	glm::vec4 pos;
	glm::vec4 col;
	glm::vec2 uv;
};
