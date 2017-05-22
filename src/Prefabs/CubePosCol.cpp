#include "stdafx.h"

#include "Prefabs/CubePrefab.h"

#include "GameContext.h"
#include "Graphics/Renderer.h"
#include "Colours.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

using namespace glm;

CubePrefab::CubePrefab()
{
}

CubePrefab::~CubePrefab()
{
}

void CubePrefab::Init(const GameContext& gameContext, glm::vec3 position, glm::quat rotation, glm::vec3 scale)
{
	m_Position = position;
	m_Rotation = rotation;
	m_Scale = scale;

	Renderer* renderer = gameContext.renderer;

	//							 FRONT		  TOP			BACK		  BOTTOM		  RIGHT			 LEFT
	const float* colours[6] = { Colour::RED, Colour::BLUE, Colour::GRAY, Colour::ORANGE, Colour::WHITE, Colour::PINK };
	m_Vertices =
	{
		// Front
		{ -1.0f, -1.0f, -1.0f, 	colours[0] },
		{ -1.0f,  1.0f, -1.0f, 	colours[0] },
		{ 1.0f,  1.0f, -1.0f, 	colours[0] },

		{ -1.0f, -1.0f, -1.0f, 	colours[0] },
		{ 1.0f,  1.0f, -1.0f, 	colours[0] },
		{ 1.0f, -1.0f, -1.0f, 	colours[0] },

		// Top
		{ -1.0f, 1.0f, -1.0f, 	colours[1] },
		{  -1.0f, 1.0f,  1.0f, 	colours[1] },
		{  1.0f,  1.0f,  1.0f, 	colours[1] },

		{  -1.0f, 1.0f, -1.0f, 	colours[1] },
		{  1.0f,  1.0f,  1.0f, 	colours[1] },
		{  1.0f,  1.0f, -1.0f, 	colours[1] },

		// Back
		{ 1.0f, -1.0f, 1.0f, 	colours[2] },
		{ 1.0f,  1.0f, 1.0f, 	colours[2] },
		{ -1.0f,  1.0f, 1.0f, 	colours[2] },

		{ 1.0f, -1.0f, 1.0f, 	colours[2] },
		{ -1.0f, 1.0f, 1.0f, 	colours[2] },
		{ -1.0f, -1.0f, 1.0f, 	colours[2] },

		// Bottom
		{ -1.0f, -1.0f, -1.0f, 	colours[3] },
		{ 1.0f, -1.0f, -1.0f, 	colours[3] },
		{ 1.0f,  -1.0f,  1.0f, 	colours[3] },

		{ -1.0f, -1.0f, -1.0f, 	colours[3] },
		{ 1.0f, -1.0f,  1.0f, 	colours[3] },
		{ -1.0f, -1.0f,  1.0f, 	colours[3] },

		// Right
		{ 1.0f, -1.0f, -1.0f, 	colours[4] },
		{ 1.0f,  1.0f, -1.0f,	colours[4] },
		{ 1.0f,  1.0f,  1.0f, 	colours[4] },

		{ 1.0f, -1.0f, -1.0f, 	colours[4] },
		{ 1.0f,  1.0f,  1.0f, 	colours[4] },
		{ 1.0f, -1.0f,  1.0f, 	colours[4] },

		// Left
		{ -1.0f, -1.0f, -1.0f, colours[5] },
		{ -1.0f,  1.0f,  1.0f, colours[5] },
		{ -1.0f,  1.0f, -1.0f, colours[5] },
										
		{ -1.0f, -1.0f, -1.0f, 	colours[5] },
		{ -1.0f, -1.0f,  1.0f, 	colours[5] },
		{ -1.0f,  1.0f,  1.0f, 	colours[5] },
	};

	std::for_each(m_Vertices.begin(), m_Vertices.end(), [](VertexPosCol& vert) { vert.pos[0] *= 0.5f; vert.pos[1] *= 0.5f; vert.pos[2] *= 0.5f; });

	m_RenderID = renderer->Initialize(gameContext, &m_Vertices);

	renderer->DescribeShaderVariable(m_RenderID, gameContext.program, "in_Color", 3, Renderer::Type::FLOAT, false, VertexPosCol::stride,
		(void*)offsetof(VertexPosCol, VertexPosCol::col));

	m_UniformTimeID = renderer->GetShaderUniformLocation(gameContext.program, "in_Time");
}

void CubePrefab::Destroy(const GameContext& gameContext)
{
	gameContext.renderer->Destroy(m_RenderID);
}

void CubePrefab::Render(const GameContext& gameContext)
{
	Renderer* renderer = gameContext.renderer;

	renderer->SetUniform1f(m_UniformTimeID, gameContext.elapsedTime);

	glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_Position);
	glm::mat4 rotation = glm::mat4(m_Rotation);
	glm::mat4 scale = glm::scale(glm::mat4(1.0f), m_Scale);
	glm::mat4 model = translation * rotation * scale;
	renderer->UpdateTransformMatrix(gameContext, m_RenderID, model);

	renderer->Draw(gameContext, m_RenderID);
}

void CubePrefab::Rotate(glm::quat deltaRotation)
{
	m_Rotation *= deltaRotation;
}

void CubePrefab::Rotate(glm::vec3 deltaEulerRotationRad)
{
	quat rotationQuat(deltaEulerRotationRad);
	m_Rotation *= rotationQuat;
}

void CubePrefab::Scale(glm::vec3 deltaScale)
{
	m_Scale *= deltaScale;
}
