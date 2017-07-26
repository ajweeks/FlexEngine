#include "stdafx.h"

#include "Scene/TestScene.h"

#include <glm/vec3.hpp>

using namespace glm;

TestScene::TestScene(const GameContext& gameContext) :
	BaseScene("TestScene")
{
}

TestScene::~TestScene()
{
}

void TestScene::Initialize(const GameContext& gameContext)
{
	//m_UVSphere.SetIterationCount(2);
	m_UVSphere.LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::UV_SPHERE);
	m_UVSphere.SetTransform(Transform(vec3(-2.0f, 1.0f, 0.0f), quat(), vec3()));
	//m_IcoSphere.Init(gameContext, SpherePrefab::Type::ICOSPHERE, vec3(2.0f, 1.0f, 0.0f), quat(vec3(0.0f)), vec3(0.5f, 0.5f, 0.5f));
	
	m_Cube.LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::CUBE);
	m_Cube.SetTransform(Transform(vec3(-4.0f, 0.5f, 0.0f), quat(vec3(0.0f, 1.0f, 0.0f)), vec3(3.0f, 1.0f, 1.0f)));

	m_Cube2.LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::CUBE);
	m_Cube2.SetTransform(Transform(vec3(7.0f, 2.5f, 0.0f), quat(vec3(0.2f, 0.3f, 2.0f)), vec3(1.0f, 5.0f, 1.0f)));

	//m_TimeID = gameContext.renderer->GetShaderUniformLocation(gameContext.program, "in_Time");

	m_CubesWide = 7;
	m_CubesLong = 7;
	m_Cubes.reserve(m_CubesWide * m_CubesLong);

	const float cubeScale = 0.5f;
	const float cubeOffset = 1.5f;

	vec3 offset = vec3(m_CubesWide / 2.0f * -cubeScale, cubeScale / 2.0f, cubeOffset + m_CubesLong / 2.0f * cubeScale);

	for (size_t i = 0; i < m_CubesLong; i++)
	{
		for (size_t j = 0; j < m_CubesWide; j++)
		{
			m_Cubes.push_back({});

			const int cubeIndex = i * m_CubesLong + j;
			const float yRot = (i / (float)m_CubesLong) + (j / (float)m_CubesWide);
			const float scaleScale = 0.2f + (cubeIndex / ((float)(m_CubesLong * m_CubesWide))) * 1.0f;

			m_Cubes[cubeIndex].LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::CUBE);
			m_Cubes[cubeIndex].SetTransform(Transform(
					offset + vec3(i * cubeOffset, 0.0f, j * cubeOffset),
					quat(vec3(0.0f, yRot, 0.0f)),
					vec3(cubeScale * scaleScale, cubeScale * scaleScale, cubeScale * scaleScale)));
		}
	}

	m_Grid.LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
}

void TestScene::Destroy(const GameContext& gameContext)
{
	m_UVSphere.Destroy(gameContext);
	//m_IcoSphere.Destroy(gameContext);
	m_Cube.Destroy(gameContext);
	m_Cube2.Destroy(gameContext);
	
	for (size_t i = 0; i < m_CubesLong; i++)
	{
		for (size_t j = 0; j < m_CubesWide; j++)
		{
			const int cubeIndex = i * m_CubesLong + j;
			m_Cubes[cubeIndex].Destroy(gameContext);
		}
	}

	m_Grid.Destroy(gameContext);
}

void TestScene::UpdateAndRender(const GameContext& gameContext)
{
	const float dt = gameContext.deltaTime;
	const float elapsed = gameContext.elapsedTime;

	gameContext.renderer->SetUniform1f(m_TimeID, gameContext.elapsedTime);

	m_UVSphere.Render(gameContext);
	//m_IcoSphere.Render(gameContext);
	m_Cube.Render(gameContext);
	m_Cube2.GetTransform().Rotate({ 0.1f * dt, -0.3f * dt, 0.4f * dt });
	m_Cube2.GetTransform().Scale({ 1.0f - (sin(elapsed) * 0.01f) * dt, 1.0f - (sin(elapsed * 1.28f) * 0.01f) * dt, 1.0f - (cos(elapsed * 2.9f) * 0.01f) * dt});
	m_Cube2.Render(gameContext);
	
	for (size_t i = 0; i < m_CubesLong; i++)
	{
		for (size_t j = 0; j < m_CubesWide; j++)
		{
			const int cubeIndex = i * m_CubesLong + j;
			m_Cubes[cubeIndex].Render(gameContext);
		}
	}

	m_Grid.Render(gameContext);
}
