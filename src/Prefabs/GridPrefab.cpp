
#include "stdafx.h"

#include "Prefabs/GridPrefab.h"
#include "Colours.h"

#include <glm/gtc/matrix_transform.hpp>

GridPrefab::GridPrefab() :
	m_Position(0.0f),
	m_Rotation(),
	m_Scale(1.0f)
{
}

GridPrefab::~GridPrefab()
{
}

void GridPrefab::Init(const GameContext& gameContext, float rowWidth, int lineCount)
{
	m_LineCount = lineCount;

	Renderer* renderer = gameContext.renderer;

	const float* lineColor = Colour::GRAY;
	const float* centerLineColor = Colour::LIGHT_GRAY;

	const size_t vertexCount = lineCount * 2 * 2;
	m_Vertices.reserve(vertexCount);

	float halfWidth = (rowWidth * (lineCount - 1)) / 2.0f;

	// Horizontal lines
	for (int i = 0; i < lineCount; ++i)
	{
		const float* color = (i == lineCount / 2 ? centerLineColor : lineColor);
		m_Vertices.push_back({ i * rowWidth - halfWidth, 0.0f, -halfWidth, color });
		m_Vertices.push_back({ i * rowWidth - halfWidth, 0.0f, halfWidth, color });
	}

	// Vertical lines
	for (int i = 0; i < lineCount; ++i)
	{
		const float* color = (i == lineCount / 2 ? centerLineColor : lineColor);
		m_Vertices.push_back({ -halfWidth, 0.0f, i * rowWidth - halfWidth, color });
		m_Vertices.push_back({ halfWidth, 0.0f, i * rowWidth - halfWidth, color });
	}

	//std::for_each(m_Vertices.begin(), m_Vertices.end(), [](VertexPosCol& vert) { vert.pos[0] *= 0.5f; vert.pos[1] *= 0.5f; vert.pos[2] *= 0.5f; });

	m_RenderID = renderer->Initialize(gameContext, &m_Vertices);
	renderer->SetTopologyMode(m_RenderID, Renderer::TopologyMode::LINE_LIST);

	renderer->DescribeShaderVariable(m_RenderID, gameContext.program, "in_Color", 3, Renderer::Type::FLOAT, false, VertexPosCol::stride,
		(void*)offsetof(VertexPosCol, VertexPosCol::col));
}

void GridPrefab::Destroy(const GameContext& gameContext)
{
	gameContext.renderer->Destroy(m_RenderID);
}

void GridPrefab::Render(const GameContext& gameContext)
{
	Renderer* renderer = gameContext.renderer;

	glm::mat4 scale = glm::scale(glm::mat4(1.0f), m_Scale);
	renderer->UpdateTransformMatrix(gameContext, m_RenderID, scale);

	renderer->Draw(gameContext, m_RenderID);
}
