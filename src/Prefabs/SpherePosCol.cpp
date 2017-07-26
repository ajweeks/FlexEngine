#include "stdafx.h"

#include "Prefabs/SpherePrefab.h"

#include "GameContext.h"
#include "Graphics/Renderer.h"
#include "Colors.h"
#include "Typedefs.h"
#include "Logger.h"
#include "Helpers.h"

#include <glm\gtc\matrix_transform.hpp>

using namespace glm;

SpherePrefab::SpherePrefab() :
	m_IterationCount(1)
{
}

void SpherePrefab::Init(const GameContext& gameContext, Type type, const Transform& transform)
{
	m_Type = type;
	m_Transform = transform;
	
	uint parallelCount = 30;
	uint meridianCount = 30;

	assert(parallelCount > 0 && meridianCount > 0);

	const float PI = glm::pi<float>();
	const float TWO_PI = glm::two_pi<float>();

	switch (type)
	{
	case SpherePrefab::Type::ICOSPHERE:
	{
		// Vertices
		const float t = (1.0f + sqrt(5.0f)) / 2.0f;

		m_Vertices.push_back(VertexPosCol({ -1.0f, t, 0 }, Color::RED));
		m_Vertices.push_back(VertexPosCol({1.0f, t, 0 }, Color::YELLOW));
		m_Vertices.push_back(VertexPosCol({-1.0f, -t, 0 }, Color::GRAY));
		m_Vertices.push_back(VertexPosCol({1.0f, -t, 0 }, Color::ORANGE));
							 
		m_Vertices.push_back(VertexPosCol({0, -1.0f, t }, Color::WHITE));
		m_Vertices.push_back(VertexPosCol({0, 1.0f, t }, Color::BLACK));
		m_Vertices.push_back(VertexPosCol({0, -1.0f, -t }, Color::GREEN));
		m_Vertices.push_back(VertexPosCol({0, 1.0f, -t }, Color::PINK));
							 
		m_Vertices.push_back(VertexPosCol({ t, 0, -1.0f }, Color::PURPLE));
		m_Vertices.push_back(VertexPosCol({t, 0, 1.0f }, Color::LIGHT_BLUE));
		m_Vertices.push_back(VertexPosCol({-t, 0, -1.0f }, Color::LIME_GREEN));
		m_Vertices.push_back(VertexPosCol({-t, 0, 1.0f }, Color::LIGHT_GRAY));

		// Indices
		std::vector<uint> ind;
		// 5 faces around point 0
		m_Indices.push_back(0);
		m_Indices.push_back(11);
		m_Indices.push_back(5);
		m_Indices.push_back(0);
		m_Indices.push_back(5);
		m_Indices.push_back(1);
		m_Indices.push_back(0);
		m_Indices.push_back(1);
		m_Indices.push_back(7);
		m_Indices.push_back(0);
		m_Indices.push_back(7);
		m_Indices.push_back(10);
		m_Indices.push_back(0);
		m_Indices.push_back(10);
		m_Indices.push_back(11);

		// 5 adjacent faces
		m_Indices.push_back(1);
		m_Indices.push_back(5);
		m_Indices.push_back(9);
		m_Indices.push_back(5);
		m_Indices.push_back(11);
		m_Indices.push_back(4);
		m_Indices.push_back(11);
		m_Indices.push_back(10);
		m_Indices.push_back(2);
		m_Indices.push_back(10);
		m_Indices.push_back(7);
		m_Indices.push_back(6);
		m_Indices.push_back(7);
		m_Indices.push_back(1);
		m_Indices.push_back(8);

		// 5 faces around point 3
		m_Indices.push_back(3);
		m_Indices.push_back(9);
		m_Indices.push_back(4);
		m_Indices.push_back(3);
		m_Indices.push_back(4);
		m_Indices.push_back(2);
		m_Indices.push_back(3);
		m_Indices.push_back(2);
		m_Indices.push_back(6);
		m_Indices.push_back(3);
		m_Indices.push_back(6);
		m_Indices.push_back(8);
		m_Indices.push_back(3);
		m_Indices.push_back(8);
		m_Indices.push_back(9);

		// 5 adjacent faces
		m_Indices.push_back(4);
		m_Indices.push_back(9);
		m_Indices.push_back(5);
		m_Indices.push_back(2);
		m_Indices.push_back(4);
		m_Indices.push_back(11);
		m_Indices.push_back(6);
		m_Indices.push_back(2);
		m_Indices.push_back(10);
		m_Indices.push_back(8);
		m_Indices.push_back(6);
		m_Indices.push_back(7);
		m_Indices.push_back(9);
		m_Indices.push_back(8);
		m_Indices.push_back(1);

		for (size_t i = 0; i < m_IterationCount; ++i)
		{
			const size_t numFaces = m_Indices.size() / 3;
			for (size_t j = 0; j < numFaces; ++j)
			{
				// Get three midpoints
				vec3 midPointA = normalize(Lerp(m_Vertices[m_Indices[j * 3]].pos, m_Vertices[m_Indices[j * 3 + 1]].pos, 0.5f));
				vec3 midPointB = normalize(Lerp(m_Vertices[m_Indices[j * 3 + 1]].pos, m_Vertices[m_Indices[j * 3 + 2]].pos, 0.5f));
				vec3 midPointC = normalize(Lerp(m_Vertices[m_Indices[j * 3 + 2]].pos, m_Vertices[m_Indices[j * 3]].pos, 0.5f));

				m_Vertices.push_back(VertexPosCol(midPointA, Color::BLACK));
				m_Vertices.push_back(VertexPosCol(midPointB, Color::BLACK));
				m_Vertices.push_back(VertexPosCol(midPointC, Color::BLACK));

				int a = m_Vertices.size() - 3;
				int b = m_Vertices.size() - 2;
				int c = m_Vertices.size() - 1;

				
			}
		}

	} break;
	case SpherePrefab::Type::UVSPHERE:
	{
		// Vertices
		VertexPosCol v1({ 0.0f, 1.0f, 0.0f }, Color::GRAY); // Top vertex
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
				const glm::vec4 color =
					(i % 2 == 0 ? j % 2 == 0 ? Color::ORANGE : Color::PURPLE : j % 2 == 0 ? Color::WHITE : Color::YELLOW);
				VertexPosCol vertex(point, color);
				m_Vertices.push_back(vertex);
			}
		}
		VertexPosCol vF({ 0.0f, -1.0f, 0.0f }, Color::GRAY); // Bottom vertex
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
			m_Indices.push_back(b);
			m_Indices.push_back(a);
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
	} break;
	default:
		Logger::LogError("Unhandled sphere type: " + std::to_string((int)type));
		break;
	}

	Renderer* renderer = gameContext.renderer;

	m_RenderID = renderer->Initialize(gameContext, &m_Vertices, &m_Indices);

	renderer->DescribeShaderVariable(m_RenderID, gameContext.program, "in_Color", 4, Renderer::Type::FLOAT, false, 
		VertexPosCol::stride, (void*)offsetof(VertexPosCol, VertexPosCol::col));

	m_UniformTimeID = renderer->GetShaderUniformLocation(gameContext.program, "in_Time");

	//renderer->SetTopologyMode(m_RenderID, Renderer::TopologyMode::POINT_LIST);
}

void SpherePrefab::Destroy(const GameContext& gameContext)
{
	gameContext.renderer->Destroy(m_RenderID);
}

SpherePrefab::~SpherePrefab()
{
}

void SpherePrefab::Render(const GameContext& gameContext)
{
	Renderer* renderer = gameContext.renderer;

	renderer->SetUniform1f(m_UniformTimeID, gameContext.elapsedTime);

	glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_Transform.position);
	glm::mat4 rotation = glm::mat4(m_Transform.rotation);
	glm::mat4 scale = glm::scale(glm::mat4(1.0f), m_Transform.scale);
	glm::mat4 model = translation * rotation * scale;
	renderer->UpdateTransformMatrix(gameContext, m_RenderID, model);

	renderer->Draw(gameContext, m_RenderID);
}

void SpherePrefab::SetIterationCount(size_t iterationCount)
{
	if (!m_Vertices.empty())
	{
		Logger::LogWarning("Sphere prefab iteration count changed after initialization, this has no effect!");
	}
	m_IterationCount = iterationCount;
}
