#pragma once

#include "Vertex.h"

#include <glm\vec3.hpp>
#include <glm\gtc\quaternion.hpp>

#include <vector>

struct GameContext;

struct GridPrefab
{
	GridPrefab();

	void Init(const GameContext& gameContext, float rowWidth, int lineCount);
	void Destroy(const GameContext& gameContext);
	~GridPrefab();

	void Render(const GameContext& gameContext);

	int m_LineCount;
	std::vector<VertexPosCol> m_Vertices;

	glm::uint m_RenderID;

	glm::vec3 m_Position;
	glm::quat m_Rotation;
	glm::vec3 m_Scale;

	glm::uint m_UniformTimeID;
};
