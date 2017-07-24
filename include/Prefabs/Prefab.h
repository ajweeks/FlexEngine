#pragma once

#include "Vertex.h"
#include "Transform.h"

#include <glm\vec3.hpp>
#include <glm\gtc\quaternion.hpp>

#include <vector>

struct GameContext;

class Prefab
{
public:
	Prefab();
	virtual ~Prefab();

	virtual void Destroy(const GameContext& gameContext) {};
	virtual void Render(const GameContext& gameContext) = 0;

	Transform GetTransform();

protected:
	std::vector<VertexPosCol> m_Vertices;
	Transform m_Transform;

private:


};