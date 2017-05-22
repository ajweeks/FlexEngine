#pragma once

#include "Vertex.h"

#include <vector>

#include <glm\vec3.hpp>
#include <glm\gtc\quaternion.hpp>

struct GameContext;

struct SpherePrefab
{
	SpherePrefab();

	void Init(const GameContext& gameContext, 
		glm::vec3 position = glm::vec3(0.0f), 
		glm::quat rotation = glm::quat(glm::vec3(0.0f)), 
		glm::vec3 scale = glm::vec3(1.0f));
	void Destroy(const GameContext& gameContext);
	~SpherePrefab();

	void Render(const GameContext& gameContext);

	std::vector<VertexPosCol> m_Vertices;
	std::vector<glm::uint> m_Indices;

	glm::uint m_RenderID;

	glm::vec3 m_Position;
	glm::quat m_Rotation;
	glm::vec3 m_Scale;

	glm::uint m_UniformTimeID;
};
