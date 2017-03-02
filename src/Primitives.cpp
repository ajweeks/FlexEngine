
#include "Primitives.h"
#include "GameContext.h"
#include "FreeCamera.h"

#include <glm\trigonometric.hpp>
#include <glm\geometric.hpp>
#include <glm\gtc\constants.hpp>
#include <glm\gtc\matrix_transform.hpp>
#include <glm\mat4x4.hpp>

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
	glVertexAttribPointer(posAttrib, 3, GL_FLOAT, GL_FALSE, stride, 0);

	GLint colorAttrib = glGetAttribLocation(program, "in_Color");
	glEnableVertexAttribArray(colorAttrib);
	glVertexAttribPointer(colorAttrib, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(VertexPosCol, VertexPosCol::col));

	m_UniformTimeID = glGetUniformLocation(program, "in_Time");
	m_MVPID = glGetUniformLocation(program, "in_MVP");

	glBindVertexArray(0);
}

CubePosCol::~CubePosCol()
{
	glDeleteVertexArrays(1, &m_VAO);
}

void CubePosCol::Draw(GLuint program, const GameContext& gameContext, float currentTime)
{
	glBindVertexArray(m_VAO);
	
	glUniform1f(m_UniformTimeID, currentTime);

	glUseProgram(program);

	glm::mat4 Translation = glm::translate(glm::mat4(1.0f), m_Position);
	glm::mat4 Rotation = glm::mat4(m_Rotation);
	glm::mat4 Scale = glm::scale(glm::mat4(1.0f), m_Scale);
	glm::mat4 Model = Translation * Rotation * Scale;
	glm::mat4 MVP = gameContext.camera->GetViewProjection() * Model;
	glUniformMatrix4fv(m_MVPID, 1, GL_FALSE, &MVP[0][0]);

	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

	glDrawArrays(GL_TRIANGLES, 0, NUM_VERTS);

	glBindVertexArray(0);
}

const VertexPosCol CubePosCol::s_Vertices[] =
{
	// Front
	{ -1.0f, -1.0f, -1.0f, 	Colour::RED },
	{ -1.0f,  1.0f, -1.0f, 	Colour::LIME_GREEN },
	{ 1.0f,  1.0f, -1.0f, 	Colour::ORANGE },

	{ -1.0f, -1.0f, -1.0f, 	Colour::RED },
	{ 1.0f,  1.0f, -1.0f, 	Colour::ORANGE },
	{ 1.0f, -1.0f, -1.0f, 	Colour::PINK },

	// Top
	{ -1.0f, 1.0f, -1.0f, 	Colour::GREEN },
	{ -1.0f, 1.0f,  1.0f, 	Colour::LIGHT_BLUE },
	{ 1.0f,  1.0f,  1.0f, 	Colour::YELLOW },
	
	{ -1.0f, 1.0f, -1.0f, 	Colour::GREEN },
	{ 1.0f,  1.0f,  1.0f, 	Colour::YELLOW },
	{ 1.0f,  1.0f, -1.0f, 	Colour::BLACK },

	// Back
	{ 1.0f, -1.0f, 1.0f, 	Colour::BLACK },
	{ 1.0f,  1.0f, 1.0f, 	Colour::DARK_GRAY },
	{ -1.0f,  1.0f, 1.0f, 	Colour::LIGHT_GRAY },

	{ 1.0f, -1.0f, 1.0f, 	Colour::BLACK },
	{ -1.0f, 1.0f, 1.0f, 	Colour::LIGHT_GRAY },
	{ -1.0f, -1.0f, 1.0f, 	Colour::WHITE },

	// Bottom
	{ -1.0f, -1.0f, -1.0f, 	Colour::BLUE },
	{ -1.0f, -1.0f,  1.0f, 	Colour::LIGHT_BLUE },
	{ 1.0f,  -1.0f,  1.0f, 	Colour::LIME_GREEN },

	{ -1.0f, -1.0f, -1.0f, 	Colour::BLUE },
	{ 1.0f, -1.0f,  1.0f, 	Colour::LIME_GREEN },
	{ 1.0f, -1.0f, -1.0f, 	Colour::YELLOW },

	// Right
	{ 1.0f, -1.0f, -1.0f, 	Colour::PINK },
	{ 1.0f,  1.0f, -1.0f,	Colour::CYAN },
	{ 1.0f,  1.0f,  1.0f, 	Colour::PURPLE },

	{ 1.0f, -1.0f, -1.0f, 	Colour::PINK },
	{ 1.0f,  1.0f,  1.0f, 	Colour::PURPLE },
	{ 1.0f, -1.0f,  1.0f, 	Colour::BLUE },

	// Left
	{ -1.0f, -1.0f, -1.0f, 	Colour::LIME_GREEN },
	{ -1.0f,  1.0f, -1.0f, 	Colour::LIGHT_GRAY },
	{ -1.0f,  1.0f,  1.0f, 	Colour::LIGHT_BLUE },

	{ -1.0f, -1.0f, -1.0f, 	Colour::LIME_GREEN },
	{ -1.0f,  1.0f,  1.0f, 	Colour::LIGHT_BLUE },
	{ -1.0f, -1.0f,  1.0f, 	Colour::ORANGE },
};

SpherePosCol::SpherePosCol()
{
}

void SpherePosCol::Init(GLuint program, glm::vec3 position, glm::quat rotation, glm::vec3 scale)
{
	m_Position = position;
	m_Rotation = rotation;
	m_Scale = scale;

	GLuint parallelCount = 10;
	GLuint meridianCount = 12;

	assert(parallelCount > 0 && meridianCount > 0);

	const float PI = glm::pi<float>();
	const float TWO_PI = glm::two_pi<float>();

	// Vertices
	VertexPosCol v1 (0.0f, 1.0f, 0.0f, Colour::BLACK);
	m_Vertices.push_back(v1);
	
	for (GLuint j = 0; j < parallelCount - 1; j++)
	{
		float polar = PI * float(j + 1) / (float)parallelCount;
		float sinP = sin(polar);
		float cosP = cos(polar);
		for (GLuint i = 0; i < meridianCount; i++)
		{
			float azimuth = TWO_PI * (float)i / (float)meridianCount;
			float sinA = sin(azimuth);
			float cosA = cos(azimuth);
			vec3 point(sinP * cosA, cosP, sinP * sinA);
			VertexPosCol vertex(point.x, point.y, point.z, Colour::WHITE);
			m_Vertices.push_back(vertex);
		}
	}
	VertexPosCol vF(0.0f, -1.0f, 0.0f, Colour::GRAY);
	m_Vertices.push_back(vF);

	m_NumVerts = m_Vertices.size();

	// Indices
	m_Indices.clear();

	// Top triangles
	for (size_t i = 0; i < meridianCount; i++)
	{
		GLuint a = i + 1;
		GLuint b = (i + 1) % meridianCount + 1;
		m_Indices.push_back(0);
		m_Indices.push_back(a);
		m_Indices.push_back(b);
	}
	
	// Center quads
	for (size_t j = 0; j < parallelCount - 2; j++)
	{
		GLuint aStart = j * meridianCount + 1;
		GLuint bStart = (j + 1) * meridianCount + 1;
		for (size_t i = 0; i < meridianCount; i++)
		{
			GLuint a = aStart + i;
			GLuint a1 = aStart + (i + 1) % meridianCount;
			GLuint b = bStart + i;
			GLuint b1 = bStart + (i + 1) % meridianCount;
			m_Indices.push_back(a);
			m_Indices.push_back(a1);
			m_Indices.push_back(b1);
			
			m_Indices.push_back(a);
			m_Indices.push_back(b1);
			m_Indices.push_back(b);
		}
	}
	
	// Bottom triangles
	for (size_t i = 0; i < meridianCount; i++)
	{
		GLuint a = i + meridianCount * (parallelCount - 2) + 1;
		GLuint b = (i + 1) % meridianCount + meridianCount * (parallelCount - 2) + 1;
		m_Indices.push_back(m_NumVerts - 1);
		m_Indices.push_back(a);
		m_Indices.push_back(b);
	}

	m_NumIndices = m_Indices.size();

	glGenVertexArrays(1, &m_VAO);
	glBindVertexArray(m_VAO);

	glGenBuffers(1, &m_VBO);
	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(m_Vertices[0]) * m_Vertices.size(), m_Vertices.data(), GL_STATIC_DRAW);

	glGenBuffers(1, &m_IBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(m_Indices[0]) * m_Indices.size(), m_Indices.data(), GL_STATIC_DRAW);

	const int stride = VertexPosCol::stride;

	GLint posAttrib = glGetAttribLocation(program, "in_Position");
	glEnableVertexAttribArray(posAttrib);
	glVertexAttribPointer(posAttrib, 3, GL_FLOAT, GL_FALSE, stride, 0);

	GLint colorAttrib = glGetAttribLocation(program, "in_Color");
	glEnableVertexAttribArray(colorAttrib);
	glVertexAttribPointer(colorAttrib, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(VertexPosCol, VertexPosCol::col));

	m_UniformTimeID = glGetUniformLocation(program, "in_Time");
	m_MVPID = glGetUniformLocation(program, "in_MVP");

	glBindVertexArray(0);
}

SpherePosCol::~SpherePosCol()
{
	glDeleteVertexArrays(1, &m_VAO);
}

void SpherePosCol::Draw(GLuint program, const GameContext& gameContext, float currentTime)
{
	glBindVertexArray(m_VAO);

	glUniform1f(m_UniformTimeID, currentTime);

	glUseProgram(program);

	glm::mat4 Translation = glm::translate(glm::mat4(1.0f), m_Position);
	glm::mat4 Rotation = glm::mat4(m_Rotation);
	glm::mat4 Scale = glm::scale(glm::mat4(1.0f), m_Scale);
	glm::mat4 Model = Translation * Rotation * Scale;
	glm::mat4 MVP = gameContext.camera->GetViewProjection() * Model;
	glUniformMatrix4fv(m_MVPID, 1, GL_FALSE, &MVP[0][0]);

	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);

	glDrawElements(GL_TRIANGLES, m_NumIndices, GL_UNSIGNED_INT, (GLuint*)0);

	glBindVertexArray(0);
}
