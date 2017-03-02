#pragma once

#include "Colours.h"

#include <glm\vec2.hpp>
#include <glm\vec3.hpp>
#include <glm\vec4.hpp>
#include <glm\gtc\quaternion.hpp>

#include <glad\glad.h>
#include <GLFW\glfw3.h>

struct VertexPosCol
{
	VertexPosCol() {};
	VertexPosCol(float x, float y, float z, float w, float r, float g, float b, float a) : pos{ x, y, z, w }, col{ r, g, b, a } {}
	VertexPosCol(float x, float y, float z, float w, const float col[4]) : pos{ x, y, z, w }, col{ col[0], col[1], col[2], col[3] } {}
	VertexPosCol(float x, float y, float z, float w, glm::vec4) : pos{ x, y, z, w }, col{ col[0], col[1], col[2], col[3] } {}
	VertexPosCol(const float pos[4], const float col[4]) : pos{ pos[0], pos[1], pos[2], pos[3] }, col{ col[0], col[1], col[2], col[3] } {}
	VertexPosCol(glm::vec4 pos, glm::vec4 col) : pos{ pos[0], pos[1], pos[2], pos[3] }, col{ col[0], col[1], col[2], col[3] } {}

	static const int stride = 4 * sizeof(float) + 4 * sizeof(float);
	float pos[4];
	float col[4];
};

//struct VertexPosColUV
//{
//	VertexPosColUV() {};
//	VertexPosColUV(float x, float y, float z, float w, float r, float g, float b, float a, float u, float v) : pos{ x, y, z, w }, col{ r, g, b, a }, uv{ u, v } {}
//	VertexPosColUV(float x, float y, float z, float w, const float col[4], float u, float v) : pos{ x, y, z, w }, col{ col[0], col[1], col[2], col[3] }, uv{ u, v } {}
//	VertexPosColUV(float x, float y, float z, float w, glm::vec4, float u, float v) : pos{ x, y, z, w }, col{ col[0], col[1], col[2], col[3] }, uv{ u, v} {}
//	VertexPosColUV(const float pos[4], const float col[4], const float uv[2]) : pos{ pos[0], pos[1], pos[2], pos[3] }, col{ col[0], col[1], col[2], col[3] }, uv{ uv[0], uv[1] } {}
//	VertexPosColUV(glm::vec4 pos, glm::vec4 col, glm::vec2 uv) : pos{ pos[0], pos[1], pos[2], pos[3] }, col{ col[0], col[1], col[2], col[3] }, uv { uv[0], uv[1] } {}
//
//	static const int stride = 4 * sizeof(float) + 4 * sizeof(float) + 2 * sizeof(float);
//	float pos[4];
//	float col[4];
//	float uv[2];
//};

struct CubePosCol
{
	CubePosCol();

	void Init(GLuint program, glm::vec3 position, glm::quat rotation = glm::quat(glm::vec3(0.0f)), glm::vec3 scale = glm::vec3(1.0f));
	~CubePosCol();

	void Draw(GLuint program, const glm::mat4& viewProjection, float currentTime);

	static const int NUM_VERTS = 4 * 12;
	static const VertexPosCol s_Vertices[NUM_VERTS];

	glm::vec3 m_Position;
	glm::quat m_Rotation;
	glm::vec3 m_Scale;

	GLuint m_MVPID;
	GLuint m_UniformTimeID;
	GLuint m_VAO;
	GLuint m_VBO;
};

struct Sphere
{
	enum class Type
	{
		STANDARD, // "UV" sphere, with edges parallel to the equator, and meridian lines running from pole to pole
		SPHEREIFIED_CUBE, // Inflated cube
		ICOSAHEDRON // All faces are equilateral triangles and have equal surface areas
	};

	Sphere();
	void Init(GLuint program, Type type, float radius, int parallelSegments, int meridianSegments,
		glm::vec3 position, glm::quat rotation = glm::quat(glm::vec3(0.0f)), glm::vec3 scale = glm::vec3(1.0f));
	~Sphere();

	glm::vec3 SphericalToCartesian(float radius, float inclination, float azimuthAngle);
	glm::vec3 CartesionToSpherical(float x, float y, float z);
	glm::vec3 CartesionToSpherical(glm::vec3 cartesian);

	void Draw(GLuint program, const glm::mat4& viewProjection, float currentTime);

	glm::vec3 m_Position;
	glm::quat m_Rotation;
	glm::vec3 m_Scale;

	GLuint m_VertexCount;

	GLuint m_MVPID;
	GLuint m_UniformTimeID;
	GLuint m_VAO;
	GLuint m_VBO;
};