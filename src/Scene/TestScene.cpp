#include "stdafx.h"

#include "Scene/TestScene.h"

#include <glm/vec3.hpp>

using namespace glm;

TestScene::TestScene(const GameContext& gameContext) :
	BaseScene("TestScene")
{
	//m_Sphere1.Init(gameContext, vec3(0.0f, 0.0f, 0.0f));
	m_Cube.Init(gameContext, vec3(0.0f, 0.0f, -6.0f), quat(vec3(0.0f, 1.0f, 0.0f)), vec3(3.0f, 1.0f, 1.0f));
	m_Cube2.Init(gameContext, vec3(0.0f, 0.0f, 4.0f), quat(vec3(0.0f, 0.0f, 2.0f)), vec3(1.0f, 5.0f, 1.0f));

	m_TimeID = gameContext.renderer->GetShaderUniformLocation(gameContext.program, "in_Time");

	//
	//m_CubesWide = 10;
	//m_CubesLong = 10;
	//m_Cubes.reserve(m_CubesWide * m_CubesLong);
	//
	//const float cubeScale = 0.5f;
	//const float cubeOffset = 1.5f;
	//
	//vec3 offset = vec3(10.0f, 0.0f, 10.0f);
	//
	//for (size_t i = 0; i < m_CubesLong; i++)
	//{
	//	for (size_t j = 0; j < m_CubesWide; j++)
	//	{
	//		m_Cubes.push_back(CubePosCol());
	//
	//		const int cubeIndex = i * m_CubesLong + j;
	//		const float yRot = (i / (float)m_CubesLong) + (j / (float)m_CubesWide);
	//		const float scaleScale = 0.2f + (cubeIndex / ((float)(m_CubesLong * m_CubesWide))) * 1.0f;
	//
	//		m_Cubes[cubeIndex].Init(gameContext, 
	//			offset + vec3(i * cubeOffset, 0.0f, j * cubeOffset), 
	//			quat(vec3(0.0f, yRot, 0.0f)), 
	//			vec3(cubeScale * scaleScale, cubeScale * scaleScale, cubeScale * scaleScale));
	//	}
	//}
}

TestScene::~TestScene()
{
}

void TestScene::Destroy(const GameContext& gameContext)
{
	//m_Sphere1.Destroy(gameContext);
	m_Cube.Destroy(gameContext);
	m_Cube2.Destroy(gameContext);
	//
	//for (size_t i = 0; i < m_CubesLong; i++)
	//{
	//	for (size_t j = 0; j < m_CubesWide; j++)
	//	{
	//		const int cubeIndex = i * m_CubesLong + j;
	//		m_Cubes[cubeIndex].Destroy(gameContext);
	//	}
	//}
}

void TestScene::UpdateAndRender(const GameContext& gameContext)
{
	gameContext.renderer->SetUniform1f(m_TimeID, gameContext.elapsedTime);

	//m_Sphere1.Render(gameContext);
	m_Cube.Render(gameContext);
	m_Cube2.Render(gameContext);
	//
	//for (size_t i = 0; i < m_CubesLong; i++)
	//{
	//	for (size_t j = 0; j < m_CubesWide; j++)
	//	{
	//		const int cubeIndex = i * m_CubesLong + j;
	//		m_Cubes[cubeIndex].Render(gameContext);
	//	}
	//}
}
