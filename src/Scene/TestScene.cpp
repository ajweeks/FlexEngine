#include "stdafx.h"

#include "Scene/TestScene.h"

#include <glm/vec3.hpp>

TestScene::TestScene(const GameContext& gameContext) :
	BaseScene("TestScene")
{
	UNREFERENCED_PARAMETER(gameContext);
}

TestScene::~TestScene()
{
}

void TestScene::Initialize(const GameContext& gameContext)
{
	m_Grid = new MeshPrefab();
	m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
	m_Grid->GetTransform().position.y -= 0.05f;
	AddChild(m_Grid);

	//m_Teapot = new MeshPrefab();
	//m_Teapot->LoadFromFile(gameContext, "resources/models/teapot.fbx");
	//m_Teapot->SetTransform(glm::vec3(20.0f, 5.0f, 0.0f));
	//AddChild(m_Teapot);
	
	//m_Scene = new MeshPrefab();
	//m_Scene->LoadFromFile(gameContext, "resources/models/scene_02.fbx");
	//m_Scene->SetTransform(glm::vec3(0.0f, -3.35f, 0.0f));
	//AddChild(m_Scene);
	
	//m_Landscape = new MeshPrefab();
	//m_Landscape->LoadFromFile(gameContext, "resources/models/landscape_01.fbx");
	//m_Landscape->SetTransform(glm::vec3(0.0f, -41.0f, 500.0f));
	//AddChild(m_Landscape);

	//m_UVSphere.SetIterationCount(2);
	//m_UVSphere = new MeshPrefab();
	//m_UVSphere->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::UV_SPHERE);
	//m_UVSphere->SetTransform(Transform(vec3(-2.0f, 1.0f, 0.0f), quat(vec3(0.0f)), vec3(1.0f)));
	//AddChild(m_UVSphere);
	//
	//m_IcoSphere.Init(gameContext, SpherePrefab::Type::ICOSPHERE, vec3(2.0f, 1.0f, 0.0f), quat(vec3(0.0f)), vec3(0.5f, 0.5f, 0.5f));
	
	//m_Cube = new MeshPrefab();
	//m_Cube->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::CUBE);
	//m_Cube->SetTransform(Transform(glm::vec3(-4.0f, 0.5f, 0.0f), glm::quat(glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(3.0f, 1.0f, 1.0f)));
	//AddChild(m_Cube);
	//
	//m_Cube2 = new MeshPrefab();
	//m_Cube2->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::CUBE);
	//m_Cube2->SetTransform(Transform(glm::vec3(7.0f, 2.5f, 0.0f), glm::quat(glm::vec3(0.2f, 0.3f, 2.0f)), glm::vec3(1.0f, 5.0f, 1.0f)));
	//AddChild(m_Cube2);

	m_ChamferBox = new MeshPrefab();
	m_ChamferBox->LoadFromFile(gameContext, "resources/models/chamfer-box.fbx");
	m_ChamferBox->SetTransform(glm::vec3(-8.0f, 0.0f, 2.0f));
	AddChild(m_ChamferBox);
	
	//m_Rock1 = new MeshPrefab();
	//m_Rock1->LoadFromFile(gameContext, "resources/models/rock-01.fbx");
	//m_Rock1->SetTransform(glm::vec3(10.0f, 0.0f, 4.0f));
	//AddChild(m_Rock1);
	//
	//m_Rock2 = new MeshPrefab();
	//m_Rock2->LoadFromFile(gameContext, "resources/models/rock-02.fbx");
	//m_Rock2->SetTransform(glm::vec3(10.0f, 0.0f, 8.0f));
	//AddChild(m_Rock2);

	float spacing = 14.0f;

	m_TransformManipulator_1 = new MeshPrefab();
	m_TransformManipulator_1->SetUsedTextures(true, true, true);
	m_TransformManipulator_1->LoadFromFile(gameContext, "resources/models/transform-manipulator-position-with-planes.fbx");
	m_TransformManipulator_1->GetTransform().position.x = -spacing * 2.0f;
	m_TransformManipulator_1->GetTransform().position.z = -spacing * 1.0f;
	AddChild(m_TransformManipulator_1);

	m_TransformManipulator_2 = new MeshPrefab();
	m_TransformManipulator_2->SetUsedTextures(true, false, false);
	m_TransformManipulator_2->LoadFromFile(gameContext, "resources/models/transform-manipulator-position-with-planes-n.fbx");
	m_TransformManipulator_2->GetTransform().position.x = -spacing;
	m_TransformManipulator_2->GetTransform().position.z = -spacing * 0.5f;
	AddChild(m_TransformManipulator_2);

	m_TransformManipulator_3 = new MeshPrefab();
	m_TransformManipulator_3->SetUsedTextures(false, true, false);
	m_TransformManipulator_3->LoadFromFile(gameContext, "resources/models/transform-manipulator-position-with-planes.fbx");
	m_TransformManipulator_3->GetTransform().position.x = 0.0f;
	m_TransformManipulator_3->GetTransform().position.z = 0.0f;
	AddChild(m_TransformManipulator_3);

	m_TransformManipulator_4 = new MeshPrefab();
	m_TransformManipulator_4->SetUsedTextures(false, false, true);
	m_TransformManipulator_4->LoadFromFile(gameContext, "resources/models/transform-manipulator-position-with-planes.fbx");
	m_TransformManipulator_4->GetTransform().position.x = spacing;
	m_TransformManipulator_4->GetTransform().position.z = spacing * 0.5f;
	AddChild(m_TransformManipulator_4);

	m_TransformManipulator_5 = new MeshPrefab();
	m_TransformManipulator_4->SetUsedTextures(false, false, false);
	m_TransformManipulator_5->SetShaderIndex(1);
	m_TransformManipulator_5->LoadFromFile(gameContext, "resources/models/transform-manipulator-position-with-planes.fbx");
	m_TransformManipulator_5->GetTransform().position.x = spacing * 2.0f;
	m_TransformManipulator_5->GetTransform().position.z = spacing * 1.0f;
	AddChild(m_TransformManipulator_5);

	Renderer::SceneInfo& sceneInfo = gameContext.renderer->GetSceneInfo();
	sceneInfo.m_AmbientColor = glm::vec4(0.02f, 0.03f, 0.025f, 1.0f);
	sceneInfo.m_SpecularColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

	// Set in update:
	//sceneInfo.m_LightDir = glm::vec4(0.9f, -0.11f, 0.12f, 0.0f);

	//m_TimeID = gameContext.renderer->GetShaderUniformLocation(gameContext.program, "in_Time");
}

void TestScene::Destroy(const GameContext& gameContext)
{
	UNREFERENCED_PARAMETER(gameContext);
}

void TestScene::Update(const GameContext& gameContext)
{
	const float dt = gameContext.deltaTime;
	const float elapsed = gameContext.elapsedTime;

	Renderer::SceneInfo& sceneInfo = gameContext.renderer->GetSceneInfo();
	sceneInfo.m_LightDir = glm::vec4(sin(gameContext.elapsedTime), -0.5f, cos(gameContext.elapsedTime * 0.8f) * 0.5f, 0.0f);

	//gameContext.renderer->SetUniform1f(m_TimeID, gameContext.elapsedTime);

	//m_Cube2->GetTransform().Rotate({ 0.1f * dt, -0.3f * dt, 0.4f * dt });
	//m_Cube2->GetTransform().Scale({ 1.0f - (sin(elapsed) * 0.6f) * dt, 1.0f - (sin(elapsed * 1.28f) * 0.5f) * dt, 1.0f - (cos(0.5f + elapsed * 2.9f) * 0.3f) * dt});

	//m_Cube->GetTransform().Rotate({ 0.3f * dt, -0.9f * dt, 0.5f * dt });
	//m_Cube->GetTransform().Scale({ 1.0f - (sin(elapsed) * 0.6f) * dt, 1.0f - (sin(elapsed * 1.28f) * 0.5f) * dt, 1.0f - (cos(0.5f + elapsed * 2.9f) * 0.3f) * dt});
}
