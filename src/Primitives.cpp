
#include "Primitives.h"

#include <glm\trigonometric.hpp>
#include <glm\geometric.hpp>
#include <glm\gtc\constants.hpp>
#include <glm\gtc\matrix_transform.hpp>
#include <glm\mat4x4.hpp>

#include <vector>

using namespace glm;

// CubePosCol
CubePosCol::CubePosCol()
{
}

void CubePosCol::Init(GLuint program, glm::vec3 position, glm::quat rotation, glm::vec3 scale)
{
	m_Position = position;
	m_Rotation = rotation;
	m_Scale = scale;

	glGenVertexArrays(1, &m_VAO);
	glBindVertexArray(m_VAO);

	glGenBuffers(1, &m_VBO);
	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(s_Vertices), s_Vertices, GL_STATIC_DRAW);

	const int stride = VertexPosCol::stride;

	GLint posAttrib = glGetAttribLocation(program, "in_Position");
	glEnableVertexAttribArray(posAttrib);
	glVertexAttribPointer(posAttrib, 4, GL_FLOAT, GL_FALSE, stride, 0);

	GLint colorAttrib = glGetAttribLocation(program, "in_Color");
	glEnableVertexAttribArray(colorAttrib);
	glVertexAttribPointer(colorAttrib, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(VertexPosCol, VertexPosCol::col));

	m_UniformTimeID = glGetUniformLocation(program, "in_Time");
	m_MVPID = glGetUniformLocation(program, "in_MVP");

	glBindVertexArray(0);
}

CubePosCol::~CubePosCol()
{
	glDeleteVertexArrays(1, &m_VAO);
}

void CubePosCol::Draw(GLuint program, const glm::mat4& viewProjection, float currentTime)
{
	glBindVertexArray(m_VAO);
	
	glUniform1f(m_UniformTimeID, currentTime);

	glUseProgram(program);

	glm::mat4 Translation = glm::translate(glm::mat4(1.0f), m_Position);
	glm::mat4 Rotation = glm::mat4(m_Rotation);
	glm::mat4 Scale = glm::scale(glm::mat4(1.0f), m_Scale);
	glm::mat4 Model = Translation * Rotation * Scale;
	glm::mat4 MVP = viewProjection * Model;
	glUniformMatrix4fv(m_MVPID, 1, GL_FALSE, &MVP[0][0]);

	glDrawArrays(GL_TRIANGLES, 0, NUM_VERTS);

	glBindVertexArray(0);
}

const VertexPosCol CubePosCol::s_Vertices[] =
{
	// Front
	{ -1.0f, -1.0f, -1.0f, 1.0f,	Colour::RED },
	{ -1.0f,  1.0f, -1.0f, 1.0f,	Colour::LIME_GREEN },
	{ 1.0f,  1.0f, -1.0f, 1.0f,		Colour::ORANGE },

	{ -1.0f, -1.0f, -1.0f, 1.0f,	Colour::RED },
	{ 1.0f,  1.0f, -1.0f, 1.0f,		Colour::ORANGE },
	{ 1.0f, -1.0f, -1.0f, 1.0f,		Colour::PINK },

	// Top
	{ -1.0f, 1.0f, -1.0f, 1.0f,		Colour::GREEN },
	{ -1.0f, 1.0f,  1.0f, 1.0f,		Colour::LIGHT_BLUE },
	{ 1.0f,  1.0f,  1.0f, 1.0f,		Colour::YELLOW },
	
	{ -1.0f, 1.0f, -1.0f, 1.0f,		Colour::GREEN },
	{ 1.0f,  1.0f,  1.0f, 1.0f,		Colour::YELLOW },
	{ 1.0f,  1.0f, -1.0f, 1.0f,		Colour::BLACK },

	// Back
	{ 1.0f, -1.0f, 1.0f, 1.0f,		Colour::BLACK },
	{ 1.0f,  1.0f, 1.0f, 1.0f,		Colour::DARK_GRAY },
	{ -1.0f,  1.0f, 1.0f, 1.0f,		Colour::LIGHT_GRAY },

	{ 1.0f, -1.0f, 1.0f, 1.0f,		Colour::BLACK },
	{ -1.0f, 1.0f, 1.0f, 1.0f,		Colour::LIGHT_GRAY },
	{ -1.0f, -1.0f, 1.0f, 1.0f,		Colour::WHITE },

	// Bottom
	{ -1.0f, -1.0f, -1.0f, 1.0f,	Colour::BLUE },
	{ -1.0f, -1.0f,  1.0f, 1.0f,	Colour::LIGHT_BLUE },
	{ 1.0f,  -1.0f,  1.0f, 1.0f,	Colour::LIME_GREEN },

	{ -1.0f, -1.0f, -1.0f, 1.0f,	Colour::BLUE },
	{ 1.0f, -1.0f,  1.0f, 1.0f,		Colour::LIME_GREEN },
	{ 1.0f, -1.0f, -1.0f, 1.0f,		Colour::YELLOW },

	// Right
	{ 1.0f, -1.0f, -1.0f, 1.0f,		Colour::PINK },
	{ 1.0f,  1.0f, -1.0f, 1.0f,		Colour::CYAN },
	{ 1.0f,  1.0f,  1.0f, 1.0f,		Colour::PURPLE },

	{ 1.0f, -1.0f, -1.0f, 1.0f,		Colour::PINK },
	{ 1.0f,  1.0f,  1.0f, 1.0f,		Colour::PURPLE },
	{ 1.0f, -1.0f,  1.0f, 1.0f,		Colour::BLUE },

	// Left
	{ -1.0f, -1.0f, -1.0f, 1.0f,	Colour::LIME_GREEN },
	{ -1.0f,  1.0f, -1.0f, 1.0f,	Colour::LIGHT_GRAY },
	{ -1.0f,  1.0f,  1.0f, 1.0f,	Colour::LIGHT_BLUE },

	{ -1.0f, -1.0f, -1.0f, 1.0f,	Colour::LIME_GREEN },
	{ -1.0f,  1.0f,  1.0f, 1.0f,	Colour::LIGHT_BLUE },
	{ -1.0f, -1.0f,  1.0f, 1.0f,	Colour::ORANGE },
};

// Sphere
Sphere::Sphere()
{
}

void Sphere::Init(GLuint program,  Type type, float radius, int parallelSegments, int meridianSegments,
	glm::vec3 position, glm::quat rotation, glm::vec3 scale)
{
	m_Position = position;
	m_Rotation = rotation;
	m_Scale = scale;

	std::vector<VertexPosCol> vertices;

	switch (type)
	{
	case Sphere::Type::STANDARD:
	{
		m_VertexCount = parallelSegments * meridianSegments;
		vertices.reserve(m_VertexCount);

		const float PI = pi<float>();
		const float TWO_PI = two_pi<float>();
		for (size_t p = 0; p < parallelSegments; ++p)
		{
			for (size_t m = 0; m < meridianSegments; ++m)
			{
				float inclination = PI * (float)(p) / (float)(parallelSegments);
				float azimuthAngle = TWO_PI * (float)(m) / (float)(meridianSegments);
				vec3 cartesian1 = SphericalToCartesian(radius, inclination, azimuthAngle);

				float y = -cos(PI * (float)(p) / (float)(parallelSegments));
				//float radius = glm::sqrt(1 - (y * y));
				float x = sin(TWO_PI * (float)(m) / (float)(meridianSegments));
				float z = cos(TWO_PI * (float)(m) / (float)(meridianSegments));
				vec3 cartesian2 = vec3(x, y, z) * radius;

				VertexPosCol vertex = VertexPosCol(cartesian1.x, cartesian1.y, cartesian1.z, 1.0, Colour::WHITE);
				vertices.push_back(vertex);
			}
		}
	} break;
	case Sphere::Type::SPHEREIFIED_CUBE:
	{

	} break;
	case Sphere::Type::ICOSAHEDRON:
	{

	} break;
	default:
	{
	} break;
	}

	glGenVertexArrays(1, &m_VAO);
	glBindVertexArray(m_VAO);

	glGenBuffers(1, &m_VBO);
	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

	const int stride = VertexPosCol::stride;

	GLint posAttrib = glGetAttribLocation(program, "in_Position");
	glEnableVertexAttribArray(posAttrib);
	glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, stride, 0);

	GLint colorAttrib = glGetAttribLocation(program, "in_Color");
	glEnableVertexAttribArray(colorAttrib);
	glVertexAttribPointer(colorAttrib, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(VertexPosCol, VertexPosCol::col));

	m_UniformTimeID = glGetUniformLocation(program, "in_Time");
	m_MVPID = glGetUniformLocation(program, "in_MVP");

	glBindVertexArray(0);
}

Sphere::~Sphere()
{
	glDeleteVertexArrays(1, &m_VAO);
}

void Sphere::Draw(GLuint program, const glm::mat4& viewProjection, float currentTime)
{
	glBindVertexArray(m_VAO);

	glUniform1f(m_UniformTimeID, currentTime);

	glUseProgram(program);

	glm::mat4 Translation = glm::translate(glm::mat4(1.0f), m_Position);
	glm::mat4 Rotation = glm::mat4(m_Rotation);
	glm::mat4 Scale = glm::scale(glm::mat4(1.0f), m_Scale);
	glm::mat4 Model = Translation * Rotation * Scale;
	glm::mat4 MVP = viewProjection * Model;
	glUniformMatrix4fv(m_MVPID, 1, GL_FALSE, &MVP[0][0]);

	glDrawArrays(GL_TRIANGLES, 0, m_VertexCount);

	glBindVertexArray(0);
}

glm::vec3 Sphere::SphericalToCartesian(float radius, float inclination, float azimuthAngle)
{
	vec3 result = {};

	result.y = radius * cos(inclination);
	result.x = radius * sin(inclination) * cos(azimuthAngle);
	result.z = radius * sin(inclination) * sin(azimuthAngle);

	return result;
}

glm::vec3 Sphere::CartesionToSpherical(float x, float y, float z)
{
	return CartesionToSpherical(vec3(x, y, z));
}

glm::vec3 Sphere::CartesionToSpherical(glm::vec3 cartesian)
{
	const float radius = length(cartesian);
	const float inclination = acos(cartesian.z / radius);
	const float azimuthAngle = atan2(cartesian.y, cartesian.x);

	vec3 result = {};

	result.x = radius;
	result.y = inclination;
	result.z = azimuthAngle;

	return result;
}

