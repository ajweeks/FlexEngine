
#include "Primitives.h"
#include "GameContext.h"
#include "FreeCamera.h"
#include "Graphics/Renderer.h"

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

void CubePosCol::Init(const GameContext& gameContext, glm::vec3 position, glm::quat rotation, glm::vec3 scale)
{
	m_Position = position;
	m_Rotation = rotation;
	m_Scale = scale;

	Renderer* renderer = gameContext.renderer;

	renderer->GenVertexArrays(1, &m_VAO);
	renderer->BindVertexArray(m_VAO);

	renderer->GenBuffers(1, &m_VBO);
	renderer->BindBuffer(Renderer::BufferTarget::ARRAY_BUFFER, m_VBO);
	renderer->BufferData(Renderer::BufferTarget::ARRAY_BUFFER, sizeof(s_Vertices), s_Vertices, Renderer::UsageFlag::STATIC_DRAW);

	const int stride = VertexPosCol::stride;

	uint posAttrib = renderer->GetAttribLocation(gameContext.program, "in_Position");
	renderer->EnableVertexAttribArray(posAttrib);
	renderer->VertexAttribPointer(posAttrib, 3, Renderer::Type::FLOAT, false, stride, 0);

	uint colorAttrib = renderer->GetAttribLocation(gameContext.program, "in_Color");
	renderer->EnableVertexAttribArray(colorAttrib);
	renderer->VertexAttribPointer(colorAttrib, 3, Renderer::Type::FLOAT, false, stride, (void*)offsetof(VertexPosCol, VertexPosCol::col));

	m_UniformTimeID = renderer->GetUniformLocation(gameContext.program, "in_Time");
	m_MVPID = renderer->GetUniformLocation(gameContext.program, "in_MVP");

	renderer->BindVertexArray(0);
}

void CubePosCol::Destroy(const GameContext& gameContext)
{
	gameContext.renderer->DeleteVertexArrays(1, &m_VAO);
}

CubePosCol::~CubePosCol()
{
}

void CubePosCol::Render(const GameContext& gameContext)
{
	Renderer* renderer = gameContext.renderer;

	renderer->BindVertexArray(m_VAO);

	renderer->SetUniform1f(m_UniformTimeID, gameContext.elapsedTime);

	renderer->UseProgram(gameContext.program);

	glm::mat4 Translation = glm::translate(glm::mat4(1.0f), m_Position);
	glm::mat4 Rotation = glm::mat4(m_Rotation);
	glm::mat4 Scale = glm::scale(glm::mat4(1.0f), m_Scale);
	glm::mat4 Model = Translation * Rotation * Scale;
	glm::mat4 MVP = gameContext.camera->GetViewProjection() * Model;
	renderer->SetUniformMatrix4fv(m_MVPID, 1, false, &MVP[0][0]);

	renderer->BindBuffer(Renderer::BufferTarget::ARRAY_BUFFER, m_VBO);

	renderer->DrawArrays(Renderer::Mode::TRIANGLES , 0, NUM_VERTS);

	renderer->BindVertexArray(0);
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

void SpherePosCol::Init(const GameContext& gameContext, glm::vec3 position, glm::quat rotation, glm::vec3 scale)
{
	m_Position = position;
	m_Rotation = rotation;
	m_Scale = scale;

	uint parallelCount = 30;
	uint meridianCount = 30;

	assert(parallelCount > 0 && meridianCount > 0);

	const float PI = glm::pi<float>();
	const float TWO_PI = glm::two_pi<float>();

	// Vertices
	VertexPosCol v1 (0.0f, 1.0f, 0.0f, Colour::BLACK);
	m_Vertices.push_back(v1);
	
	for (uint j = 0; j < parallelCount - 1; j++)
	{
		float polar = PI * float(j + 1) / (float)parallelCount;
		float sinP = sin(polar);
		float cosP = cos(polar);
		for (uint i = 0; i < meridianCount; i++)
		{
			float azimuth = TWO_PI * (float)i / (float)meridianCount;
			float sinA = sin(azimuth);
			float cosA = cos(azimuth);
			vec3 point(sinP * cosA, cosP, sinP * sinA);
			const float* color = 
				(i % 2 == 0 ? j % 2 == 0 ? Colour::ORANGE : Colour::PURPLE : j % 2 == 0 ? Colour::WHITE : Colour::YELLOW);
			VertexPosCol vertex(point.x, point.y, point.z, color);
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
		uint a = i + 1;
		uint b = (i + 1) % meridianCount + 1;
		m_Indices.push_back(0);
		m_Indices.push_back(a);
		m_Indices.push_back(b);
	}
	
	// Center quads
	for (size_t j = 0; j < parallelCount - 2; j++)
	{
		uint aStart = j * meridianCount + 1;
		uint bStart = (j + 1) * meridianCount + 1;
		for (size_t i = 0; i < meridianCount; i++)
		{
			uint a = aStart + i;
			uint a1 = aStart + (i + 1) % meridianCount;
			uint b = bStart + i;
			uint b1 = bStart + (i + 1) % meridianCount;
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
		uint a = i + meridianCount * (parallelCount - 2) + 1;
		uint b = (i + 1) % meridianCount + meridianCount * (parallelCount - 2) + 1;
		m_Indices.push_back(m_NumVerts - 1);
		m_Indices.push_back(a);
		m_Indices.push_back(b);
	}

	m_NumIndices = m_Indices.size();

	Renderer* renderer = gameContext.renderer;

	renderer->GenVertexArrays(1, &m_VAO);
	renderer->BindVertexArray(m_VAO);

	renderer->GenBuffers(1, &m_VBO);
	renderer->BindBuffer(Renderer::BufferTarget::ARRAY_BUFFER, m_VBO);
	renderer->BufferData(Renderer::BufferTarget::ARRAY_BUFFER, sizeof(m_Vertices[0]) * m_Vertices.size(), m_Vertices.data(), Renderer::UsageFlag::STATIC_DRAW);

	renderer->GenBuffers(1, &m_IBO);
	renderer->BindBuffer(Renderer::BufferTarget::ELEMENT_ARRAY_BUFFER, m_IBO);
	renderer->BufferData(Renderer::BufferTarget::ELEMENT_ARRAY_BUFFER, sizeof(m_Indices[0]) * m_Indices.size(), m_Indices.data(), Renderer::UsageFlag::STATIC_DRAW);

	const int stride = VertexPosCol::stride;

	uint posAttrib = renderer->GetAttribLocation(gameContext.program, "in_Position");
	renderer->EnableVertexAttribArray(posAttrib);
	renderer->VertexAttribPointer(posAttrib, 3, Renderer::Type::FLOAT, false, stride, 0);

	uint colorAttrib = renderer->GetAttribLocation(gameContext.program, "in_Color");
	renderer->EnableVertexAttribArray(colorAttrib);
	renderer->VertexAttribPointer(colorAttrib, 3, Renderer::Type::FLOAT, false, stride, (void*)offsetof(VertexPosCol, VertexPosCol::col));

	m_UniformTimeID = renderer->GetUniformLocation(gameContext.program, "in_Time");
	m_MVPID = renderer->GetUniformLocation(gameContext.program, "in_MVP");

	renderer->BindVertexArray(0);
}

void SpherePosCol::Destroy(const GameContext & gameContext)
{
	gameContext.renderer->DeleteVertexArrays(1, &m_VAO);
}

SpherePosCol::~SpherePosCol()
{
}

void SpherePosCol::Render(const GameContext& gameContext)
{
	Renderer* renderer = gameContext.renderer;

	renderer->BindVertexArray(m_VAO);
	
	renderer->UseProgram(gameContext.program);

	renderer->SetUniform1f(m_UniformTimeID, gameContext.elapsedTime);

	glm::mat4 Translation = glm::translate(glm::mat4(1.0f), m_Position);
	glm::mat4 Rotation = glm::mat4(m_Rotation);
	glm::mat4 Scale = glm::scale(glm::mat4(1.0f), m_Scale);
	glm::mat4 Model = Translation * Rotation * Scale;
	glm::mat4 MVP = gameContext.camera->GetViewProjection() * Model;
	renderer->SetUniformMatrix4fv(m_MVPID, 1, false, &MVP[0][0]);

	renderer->BindBuffer(Renderer::BufferTarget::ARRAY_BUFFER, m_VBO);
	renderer->BindBuffer(Renderer::BufferTarget::ELEMENT_ARRAY_BUFFER, m_IBO);

	renderer->DrawElements(Renderer::Mode::TRIANGLES, m_NumIndices, Renderer::Type::UNSIGNED_INT, (uint*)0);

	renderer->BindVertexArray(0);
}
