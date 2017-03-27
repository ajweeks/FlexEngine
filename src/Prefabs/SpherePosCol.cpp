
#include "Prefabs/SpherePosCol.h"
#include "Graphics/Renderer.h"
#include "FreeCamera.h"

#include <glm\gtc\matrix_transform.hpp>

using namespace glm;

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
	VertexPosCol v1(0.0f, 1.0f, 0.0f, Colour::BLACK);
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

	const uint numVerts = m_Vertices.size();

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
		m_Indices.push_back(numVerts - 1);
		m_Indices.push_back(a);
		m_Indices.push_back(b);
	}

	const uint numIndices = m_Indices.size();

	Renderer* renderer = gameContext.renderer;

	m_RenderID = renderer->Initialize(gameContext, &m_Vertices, &m_Indices);

	renderer->DescribeShaderVariable(m_RenderID, gameContext.program, "in_Color", 3, Renderer::Type::FLOAT, false, VertexPosCol::stride,
		(void*)offsetof(VertexPosCol, VertexPosCol::col));

	m_UniformTimeID = renderer->GetShaderUniformLocation(gameContext.program, "in_Time");
}

void SpherePosCol::Destroy(const GameContext& gameContext)
{
	gameContext.renderer->Destroy(m_RenderID);
}

SpherePosCol::~SpherePosCol()
{
}

void SpherePosCol::Render(const GameContext& gameContext)
{
	Renderer* renderer = gameContext.renderer;

	renderer->SetUniform1f(m_UniformTimeID, gameContext.elapsedTime);

	glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_Position);
	glm::mat4 rotation = glm::mat4(m_Rotation);
	glm::mat4 scale = glm::scale(glm::mat4(1.0f), m_Scale);
	glm::mat4 model = translation * rotation * scale;
	renderer->UpdateTransformMatrix(gameContext, m_RenderID, model);

	renderer->Draw(m_RenderID);
}
