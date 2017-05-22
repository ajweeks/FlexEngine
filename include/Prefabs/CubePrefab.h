#pragma once

#include "Vertex.h"

#include <glm\vec3.hpp>
#include <glm\gtc\quaternion.hpp>

#include <vector>

struct GameContext;

struct CubePrefab
{
	CubePrefab();

	void Init(const GameContext& gameContext, 
		glm::vec3 position = glm::vec3(0.0f), 
		glm::quat rotation = glm::quat(glm::vec3(0.0f)), 
		glm::vec3 scale = glm::vec3(1.0f));
	void Destroy(const GameContext& gameContext);
	~CubePrefab();

	void Render(const GameContext& gameContext);

	void Rotate(glm::quat deltaRotation);
	void Rotate(glm::vec3 deltaEulerRotationRad);
	void Scale(glm::vec3 deltaScale);

	std::vector<VertexPosCol> m_Vertices;

	glm::uint m_RenderID;

	glm::vec3 m_Position;
	glm::quat m_Rotation;
	glm::vec3 m_Scale;

	glm::uint m_UniformTimeID;
};
