#include "stdafx.h"

#include "Scene/Scenes/TestScene.h"

#include <glm/vec3.hpp>

#include <imgui.h>

namespace flex
{
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
		Renderer::MaterialCreateInfo colorMatInfo = {};
		colorMatInfo.shaderIndex = 1;
		colorMatInfo.name = "Color";
		const MaterialID colorMatID = gameContext.renderer->InitializeMaterial(gameContext, &colorMatInfo);


		Renderer::MaterialCreateInfo skyboxMatInfo = {};
		skyboxMatInfo.name = "Skybox";
		skyboxMatInfo.shaderIndex = 3;

		const std::string directory = RESOURCE_LOCATION + "textures/skyboxes/box_01/";
		const std::string fileName = "skybox";
		const std::string extension = ".jpg";

		skyboxMatInfo.cubeMapFilePaths = {
			directory + fileName + "_r" + extension,
			directory + fileName + "_l" + extension,
			directory + fileName + "_u" + extension,
			directory + fileName + "_d" + extension,
			directory + fileName + "_b" + extension,
			directory + fileName + "_f" + extension,
		};
		const MaterialID skyboxMatID = gameContext.renderer->InitializeMaterial(gameContext, &skyboxMatInfo);


		Renderer::MaterialCreateInfo brickMatInfo = {};
		brickMatInfo.shaderIndex = 0;
		brickMatInfo.name = "Brick";
		brickMatInfo.diffuseTexturePath = RESOURCE_LOCATION + "textures/brick_d.png";
		brickMatInfo.specularTexturePath = RESOURCE_LOCATION + "textures/brick_s.png";
		brickMatInfo.normalTexturePath = RESOURCE_LOCATION + "textures/brick_n.png";
		const MaterialID brickMatID = gameContext.renderer->InitializeMaterial(gameContext, &brickMatInfo);


		Renderer::MaterialCreateInfo workMatInfo = {};
		workMatInfo.shaderIndex = 0;
		workMatInfo.name = "Work";
		workMatInfo.diffuseTexturePath = RESOURCE_LOCATION + "textures/work_d.jpg";
		workMatInfo.specularTexturePath = RESOURCE_LOCATION + "textures/work_s.jpg";
		workMatInfo.normalTexturePath = RESOURCE_LOCATION + "textures/work_n.jpg";
		const MaterialID workMatID = gameContext.renderer->InitializeMaterial(gameContext, &workMatInfo);

		Renderer::MaterialCreateInfo plainNSimpleInfo = {};
		plainNSimpleInfo.shaderIndex = 0;
		plainNSimpleInfo.name = "Plain 'n Simple (simple with no textures set)";
		const MaterialID plainNSimpleMatID = gameContext.renderer->InitializeMaterial(gameContext, &plainNSimpleInfo);


		m_Grid = new MeshPrefab(colorMatID);
		m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
		m_Grid->GetTransform().position.y -= 0.1f;
		AddChild(m_Grid);

		m_Plane = new MeshPrefab(plainNSimpleMatID);
		m_Plane->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::PLANE);
		m_Plane->GetTransform().position.y -= 0.05f;
		AddChild(m_Plane);

		//m_Teapot = new MeshPrefab();
		//m_Teapot->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/teapot.fbx");
		//m_Teapot->SetTransform(glm::vec3(20.0f, 5.0f, 0.0f));
		//AddChild(m_Teapot);

		//m_Scene = new MeshPrefab();
		//m_Scene->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/scene_02.fbx");
		//m_Scene->SetTransform(glm::vec3(0.0f, -3.35f, 0.0f));
		//AddChild(m_Scene);

		//m_Landscape = new MeshPrefab();
		//m_Landscape->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/landscape_01.fbx");
		//m_Landscape->SetTransform(glm::vec3(0.0f, -41.0f, 500.0f));
		//AddChild(m_Landscape);

		//m_UVSphere.SetIterationCount(2);
		//m_UVSphere = new MeshPrefab();
		//m_UVSphere->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::UV_SPHERE);
		//m_UVSphere->SetTransform(Transform(vec3(-2.0f, 1.0f, 0.0f), quat(vec3(0.0f)), vec3(1.0f)));
		//AddChild(m_UVSphere);
		//
		//m_IcoSphere.Init(gameContext, SpherePrefab::Type::ICOSPHERE, vec3(2.0f, 1.0f, 0.0f), quat(vec3(0.0f)), vec3(0.5f, 0.5f, 0.5f));

		m_Cube = new MeshPrefab();
		m_Cube->SetMaterialID(plainNSimpleMatID);
		m_Cube->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::CUBE);
		m_Cube->SetTransform(Transform(glm::vec3(-4.0f, 0.5f, 0.0f), glm::quat(glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(3.0f, 1.0f, 1.0f)));
		AddChild(m_Cube);
		
		//m_Cube2 = new MeshPrefab();
		//m_Cube2->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::CUBE);
		//m_Cube2->SetTransform(Transform(glm::vec3(7.0f, 2.5f, 0.0f), glm::quat(glm::vec3(0.2f, 0.3f, 2.0f)), glm::vec3(1.0f, 5.0f, 1.0f)));
		//AddChild(m_Cube2);

		//m_ChamferBox = new MeshPrefab();
		//m_ChamferBox->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/chamfer-box.fbx");
		//m_ChamferBox->SetTransform(glm::vec3(-8.0f, 0.0f, 2.0f));
		//AddChild(m_ChamferBox);

		//m_Rock1 = new MeshPrefab();
		//m_Rock1->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/rock-01.fbx");
		//m_Rock1->SetTransform(glm::vec3(10.0f, 0.0f, 4.0f));
		//AddChild(m_Rock1);
		//
		//m_Rock2 = new MeshPrefab();
		//m_Rock2->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/rock-02.fbx");
		//m_Rock2->SetTransform(glm::vec3(10.0f, 0.0f, 8.0f));
		//AddChild(m_Rock2);

		float spacing = 14.0f;

		m_TransformManipulator_1 = new MeshPrefab(brickMatID, "Transform 1");
		m_TransformManipulator_1->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/transform-manipulator-position-with-planes.fbx");
		m_TransformManipulator_1->GetTransform().position.x = -spacing * 2.0f;
		m_TransformManipulator_1->GetTransform().position.z = -spacing * 1.0f;
		AddChild(m_TransformManipulator_1);
		
		m_TransformManipulator_2 = new MeshPrefab(workMatID, "Transform 2");
		m_TransformManipulator_2->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/transform-manipulator-position-with-planes.fbx");
		m_TransformManipulator_2->GetTransform().position.x = -spacing;
		m_TransformManipulator_2->GetTransform().position.z = -spacing * 0.5f;
		AddChild(m_TransformManipulator_2);
		
		m_TransformManipulator_3 = new MeshPrefab(brickMatID, "Transform 3");
		m_TransformManipulator_3->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/transform-manipulator-position-with-planes.fbx");
		m_TransformManipulator_3->GetTransform().position.x = 0.0f;
		m_TransformManipulator_3->GetTransform().position.z = 0.0f;
		AddChild(m_TransformManipulator_3);
		
		m_TransformManipulator_4 = new MeshPrefab(workMatID, "Transform 4");
		m_TransformManipulator_4->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/transform-manipulator-position-with-planes.fbx");
		m_TransformManipulator_4->GetTransform().position.x = spacing;
		m_TransformManipulator_4->GetTransform().position.z = spacing * 0.5f;
		AddChild(m_TransformManipulator_4);
		
		m_TransformManipulator_5 = new MeshPrefab(brickMatID, "Transform 5");
		m_TransformManipulator_5->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/transform-manipulator-position-with-planes.fbx");
		m_TransformManipulator_5->GetTransform().position.x = spacing * 2.0f;
		m_TransformManipulator_5->GetTransform().position.z = spacing * 1.0f;
		AddChild(m_TransformManipulator_5);
		
		
		m_Skybox = new MeshPrefab(skyboxMatID);
		m_Skybox->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::SKYBOX);
		AddChild(m_Skybox);


		Renderer::SceneInfo& sceneInfo = gameContext.renderer->GetSceneInfo();
		sceneInfo.m_AmbientColor = glm::vec4(0.02f, 0.03f, 0.025f, 1.0f);
		sceneInfo.m_SpecularColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);


		Renderer::PointLight light1 = {};
		light1.position.y += 5.0f;
		m_PointLight1ID = gameContext.renderer->InitializePointLight(light1);


		Renderer::PointLight light2 = {};
		light2.position.y += 5.0f;
		m_PointLight2ID = gameContext.renderer->InitializePointLight(light2);


		Renderer::DirectionalLight dirLight = {};
		dirLight.direction = glm::vec3(-0.75f, -0.25f, -0.95f);
		dirLight.diffuseCol = glm::vec3(0.5f);
		gameContext.renderer->InitializeDirectionalLight(dirLight);
	}

	void TestScene::Destroy(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void TestScene::Update(const GameContext& gameContext)
	{
		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_0))
		{
			if (m_Grid)
			{
				gameContext.renderer->Destroy(m_Grid->GetRenderID());
				RemoveChild(m_Grid, true);
				m_Grid = nullptr;
			}
			else
			{
				m_Grid = new MeshPrefab();
				m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
				m_Grid->GetTransform().position.y -= 0.05f;
				AddChild(m_Grid);
				gameContext.renderer->PostInitializeRenderObject(m_Grid->GetRenderID());
			}
		}

		Renderer::PointLight& light1 = gameContext.renderer->GetPointLight(m_PointLight1ID);
		light1.position.z = (sin(gameContext.elapsedTime)) * 25.0f;
		light1.position.x = (cos(gameContext.elapsedTime)) * 25.0f;

		Renderer::PointLight& light2 = gameContext.renderer->GetPointLight(m_PointLight2ID);
		light2.position.z = (cos(gameContext.elapsedTime + glm::pi<float>())) * 40.0f + 15.0f;
		light2.position.x = (sin(gameContext.elapsedTime + glm::pi<float>())) * 40.0f - 15.0f;
		light2.diffuseCol = glm::vec3(
			sin(gameContext.elapsedTime) * 0.5f + 0.5,
			sin(gameContext.elapsedTime * 0.35f + 0.42f) * 0.5f + 0.5,
			cos(gameContext.elapsedTime * 1.68f + 2.8f) * 0.5f + 0.5);

		//const float dt = gameContext.deltaTime;
		//const float elapsed = gameContext.elapsedTime;

		Renderer::SceneInfo& sceneInfo = gameContext.renderer->GetSceneInfo();
		sceneInfo.m_LightDir = glm::vec4(sin(gameContext.elapsedTime), -0.5f, cos(gameContext.elapsedTime * 0.8f) * 0.5f, 0.0f);

		//gameContext.renderer->SetUniform1f(m_TimeID, gameContext.elapsedTime);

		//m_Cube2->GetTransform().Rotate({ 0.1f * dt, -0.3f * dt, 0.4f * dt });
		//m_Cube2->GetTransform().Scale({ 1.0f - (sin(elapsed) * 0.6f) * dt, 1.0f - (sin(elapsed * 1.28f) * 0.5f) * dt, 1.0f - (cos(0.5f + elapsed * 2.9f) * 0.3f) * dt});

		//m_Cube->GetTransform().Rotate({ 0.3f * dt, -0.9f * dt, 0.5f * dt });
		//m_Cube->GetTransform().Scale({ 1.0f - (sin(elapsed) * 0.6f) * dt, 1.0f - (sin(elapsed * 1.28f) * 0.5f) * dt, 1.0f - (cos(0.5f + elapsed * 2.9f) * 0.3f) * dt});
	}
} // namespace flex
