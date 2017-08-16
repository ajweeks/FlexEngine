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
	m_Grid = new MeshPrefab();
	m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
	AddChild(m_Grid);

	m_Grid2 = new MeshPrefab();
	m_Grid2->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
	m_Grid2->GetTransform().position.y += 10.0f;
	AddChild(m_Grid2);

	m_Teapot = new MeshPrefab();
	m_Teapot->LoadFromFile(gameContext, "resources/models/teapot.fbx");
	m_Teapot->SetTransform(glm::vec3(20.0f, 5.0f, 0.0f));
	AddChild(m_Teapot);
	
	//m_Scene = new MeshPrefab();
	//m_Scene->LoadFromFile(gameContext, "resources/models/scene_02.fbx");
	//m_Scene->SetTransform(glm::vec3(0.0f, -3.35f, 0.0f));
	//AddChild(m_Scene);
	//
	//m_Landscape = new MeshPrefab();
	//m_Landscape->LoadFromFile(gameContext, "resources/models/landscape_01.fbx");
	//m_Landscape->SetTransform(glm::vec3(0.0f, -41.0f, 500.0f));
	//AddChild(m_Landscape);

	//m_UVSphere.SetIterationCount(2);
	//m_UVSphere = new MeshPrefab();
	//m_UVSphere->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::UV_SPHERE);
	//m_UVSphere->SetTransform(Transform(vec3(-2.0f, 1.0f, 0.0f), quat(vec3(0.0f)), vec3(1.0f)));
	//AddChild(m_UVSphere);
	
	//m_IcoSphere.Init(gameContext, SpherePrefab::Type::ICOSPHERE, vec3(2.0f, 1.0f, 0.0f), quat(vec3(0.0f)), vec3(0.5f, 0.5f, 0.5f));
	
	//m_Cube = new MeshPrefab();
	//m_Cube->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::CUBE);
	//m_Cube->SetTransform(Transform(vec3(-4.0f, 0.5f, 0.0f), quat(vec3(0.0f, 1.0f, 0.0f)), vec3(3.0f, 1.0f, 1.0f)));
	//AddChild(m_Cube);
	//
	//m_Cube2 = new MeshPrefab();
	//m_Cube2->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::CUBE);
	//m_Cube2->SetTransform(Transform(vec3(7.0f, 2.5f, 0.0f), quat(vec3(0.2f, 0.3f, 2.0f)), vec3(1.0f, 5.0f, 1.0f)));
	//AddChild(m_Cube2);

	//m_CubesWide = 2;
	//m_CubesLong = 3;
	//m_Cubes.reserve(m_CubesWide * m_CubesLong);
	//
	//const float cubeScale = 0.5f;
	//const float cubeOffset = 1.5f;
	//
	//vec3 offset = vec3(m_CubesWide / 2.0f * -cubeScale, cubeScale / 2.0f, cubeOffset + m_CubesLong / 2.0f * cubeScale);
	//
	//for (size_t i = 0; i < m_CubesLong; i++)
	//{
	//	for (size_t j = 0; j < m_CubesWide; j++)
	//	{
	//		MeshPrefab* cube = new MeshPrefab();
	//
	//		const int cubeIndex = i * m_CubesWide + j;
	//		const float yRot = (i / (float)m_CubesLong) + (j / (float)m_CubesWide);
	//		const float scaleScale = 0.2f + (cubeIndex / ((float)(m_CubesLong * m_CubesWide))) * 1.0f;
	//
	//		cube->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::CUBE);
	//		cube->SetTransform(Transform(
	//				offset + vec3(i * cubeOffset, 0.0f, j * cubeOffset),
	//				quat(vec3(0.0f, yRot, 0.0f)),
	//				vec3(cubeScale * scaleScale, cubeScale * scaleScale, cubeScale * scaleScale)));
	//
	//		m_Cubes.push_back(cube);
	//		AddChild(cube);
	//	}
	//}

	m_ChamferBox = new MeshPrefab();
	m_ChamferBox->LoadFromFile(gameContext, "resources/models/chamfer-box.fbx");
	m_ChamferBox->SetTransform(glm::vec3(-8.0f, 0.0f, 2.0f));
	AddChild(m_ChamferBox);
	
	//m_PunchingBag = new MeshPrefab();
	//m_PunchingBag->LoadFromFile(gameContext, "resources/models/punching-bag.fbx");
	//m_PunchingBag->SetTransform(glm::vec3(-3.5f, -6.0f, 0.0f));
	//AddChild(m_PunchingBag);
	
	m_Rock1 = new MeshPrefab();
	m_Rock1->LoadFromFile(gameContext, "resources/models/rock-01.fbx");
	m_Rock1->SetTransform(glm::vec3(10.0f, 0.0f, 4.0f));
	AddChild(m_Rock1);
	
	m_Rock2 = new MeshPrefab();
	m_Rock2->LoadFromFile(gameContext, "resources/models/rock-02.fbx");
	m_Rock2->SetTransform(glm::vec3(10.0f, 0.0f, 8.0f));
	AddChild(m_Rock2);

	m_TransformManipulatorPosition = new MeshPrefab();
	m_TransformManipulatorPosition->LoadFromFile(gameContext, "resources/models/transform-manipulator-position-with-planes.fbx");
	AddChild(m_TransformManipulatorPosition);

	Renderer::SceneInfo& sceneInfo = gameContext.renderer->GetSceneInfo();
	sceneInfo.m_AmbientColor = glm::vec4(0.02f, 0.03f, 0.025f, 1.0f);
	sceneInfo.m_SpecularColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	sceneInfo.m_LightDir = glm::vec4(0.9f, 0.11f, 0.12f, 0.0f);

	//m_TimeID = gameContext.renderer->GetShaderUniformLocation(gameContext.program, "in_Time");
}

void TestScene::Destroy(const GameContext& gameContext)
{
}

void TestScene::Update(const GameContext& gameContext)
{
	const float dt = gameContext.deltaTime;
	const float elapsed = gameContext.elapsedTime;

	Renderer::SceneInfo& sceneInfo = gameContext.renderer->GetSceneInfo();
	sceneInfo.m_LightDir = glm::vec4(sin(gameContext.elapsedTime), 0.5f, cos(gameContext.elapsedTime * 0.8f) * 0.5f, 0.0f);

	//gameContext.renderer->SetUniform1f(m_TimeID, gameContext.elapsedTime);

	//m_Cube2->GetTransform().Rotate({ 0.1f * dt, -0.3f * dt, 0.4f * dt });
	//m_Cube2->GetTransform().Scale({ 1.0f - (sin(elapsed) * 0.6f) * dt, 1.0f - (sin(elapsed * 1.28f) * 0.5f) * dt, 1.0f - (cos(0.5f + elapsed * 2.9f) * 0.3f) * dt});

	//m_Cube->GetTransform().Rotate({ 0.3f * dt, -0.9f * dt, 0.5f * dt });
	//m_Cube->GetTransform().Scale({ 1.0f - (sin(elapsed) * 0.6f) * dt, 1.0f - (sin(elapsed * 1.28f) * 0.5f) * dt, 1.0f - (cos(0.5f + elapsed * 2.9f) * 0.3f) * dt});
}
