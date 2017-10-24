#include "stdafx.hpp"

#include "Scene/Scenes/TestScene.hpp"

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
		// Materials
		Renderer::MaterialCreateInfo colorMatInfo = {};
		colorMatInfo.shaderName = "color";
		colorMatInfo.name = "Color";
		const MaterialID colorMatID = gameContext.renderer->InitializeMaterial(gameContext, &colorMatInfo);
		
		
		Renderer::MaterialCreateInfo skyboxMatInfo = {};
		skyboxMatInfo.name = "Skybox";
		skyboxMatInfo.shaderName = "skybox";
		
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
		brickMatInfo.shaderName = "deferred_simple";
		brickMatInfo.name = "Brick";
		brickMatInfo.diffuseTexturePath = RESOURCE_LOCATION + "textures/brick_d.png";
		brickMatInfo.specularTexturePath = RESOURCE_LOCATION + "textures/brick_s.png";
		brickMatInfo.normalTexturePath = RESOURCE_LOCATION + "textures/brick_n.png";
		const MaterialID brickMatID = gameContext.renderer->InitializeMaterial(gameContext, &brickMatInfo);
		
		
		Renderer::MaterialCreateInfo workMatInfo = {};
		workMatInfo.shaderName = "deferred_simple";
		workMatInfo.name = "Work";
		workMatInfo.diffuseTexturePath = RESOURCE_LOCATION + "textures/work_d.jpg";
		workMatInfo.specularTexturePath = RESOURCE_LOCATION + "textures/work_s.jpg";
		workMatInfo.normalTexturePath = RESOURCE_LOCATION + "textures/work_n.jpg";
		const MaterialID workMatID = gameContext.renderer->InitializeMaterial(gameContext, &workMatInfo);

		Renderer::MaterialCreateInfo simpleTexturelessInfo = {};
		simpleTexturelessInfo.shaderName = "deferred_simple";
		simpleTexturelessInfo.name = "Simple textureless";
		const MaterialID simpleTexturelessMatID = gameContext.renderer->InitializeMaterial(gameContext, &simpleTexturelessInfo);

		// Render objects
		m_Grid = new MeshPrefab(colorMatID);
		m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
		m_Grid->GetTransform().Translate(0.0f, -0.1f, 0.0f);
		AddChild(gameContext, m_Grid);
		
		m_Plane = new MeshPrefab(simpleTexturelessMatID);
		m_Plane->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::PLANE);
		m_Plane->GetTransform().Translate(0.0f, -0.05f, 0.0f);
		AddChild(gameContext, m_Plane);


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

		float spacing = 14.0f;
		
		m_TransformManipulator_1 = new MeshPrefab(brickMatID, "Transform 1");
		m_TransformManipulator_1->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/transform-manipulator-position-with-planes.fbx");
		m_TransformManipulator_1->GetTransform().Translate(-spacing * 2.0f, 0.0f, -spacing * 1.0f);
		AddChild(gameContext, m_TransformManipulator_1);
		
		m_TransformManipulator_2 = new MeshPrefab(workMatID, "Transform 2");
		m_TransformManipulator_2->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/transform-manipulator-position-with-planes.fbx");
		m_TransformManipulator_1->GetTransform().Translate(-spacing, 0.0f, -spacing * 0.5f);
		AddChild(gameContext, m_TransformManipulator_2);
		
		m_TransformManipulator_3 = new MeshPrefab(simpleTexturelessMatID, "Transform 3");
		m_TransformManipulator_3->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/transform-manipulator-position-with-planes.fbx");
		m_TransformManipulator_1->GetTransform().Translate(0.0f, 0.0f, 0.0f);
		AddChild(gameContext, m_TransformManipulator_3);
		
		m_TransformManipulator_4 = new MeshPrefab(workMatID, "Transform 4");
		m_TransformManipulator_4->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/transform-manipulator-position-with-planes.fbx");
		m_TransformManipulator_1->GetTransform().Translate(spacing, 0.0f, spacing * 0.5f);
		AddChild(gameContext, m_TransformManipulator_4);
		
		m_TransformManipulator_5 = new MeshPrefab(brickMatID, "Transform 5");
		m_TransformManipulator_5->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/transform-manipulator-position-with-planes.fbx");
		m_TransformManipulator_1->GetTransform().Translate(spacing * 2.0f, 0.0f, spacing * 1.0f);
		AddChild(gameContext, m_TransformManipulator_5);
		
		
		m_Skybox = new MeshPrefab(skyboxMatID);
		m_Skybox->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::SKYBOX);
		AddChild(gameContext, m_Skybox);


		Renderer::PointLight light1 = {};
		m_PointLight1ID = gameContext.renderer->InitializePointLight(light1);

		Renderer::PointLight light2 = {};
		m_PointLight2ID = gameContext.renderer->InitializePointLight(light2);

		Renderer::PointLight light3 = {};
		m_PointLight3ID = gameContext.renderer->InitializePointLight(light3);

		Renderer::PointLight light4 = {};
		m_PointLight4ID = gameContext.renderer->InitializePointLight(light4);


		Renderer::DirectionalLight dirLight = {};
		dirLight.direction = glm::vec4(0.75f, -0.25f, 0.95f, 1.0f);
		dirLight.color = glm::vec4(0.85f, 0.97f, 0.73f, 1.0f);
		gameContext.renderer->InitializeDirectionalLight(dirLight);
	}

	void TestScene::PostInitialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
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
				m_Grid->GetTransform().Translate(0.0f, -0.05f, 0.0f);
				AddChild(gameContext, m_Grid);
			}
		}
		
		Renderer::PointLight& light1 = gameContext.renderer->GetPointLight(m_PointLight1ID);
		light1.position.z = (sin(gameContext.elapsedTime)) * 25.0f;
		light1.position.x = (cos(gameContext.elapsedTime)) * 25.0f;
		light1.position.y = (cos(gameContext.elapsedTime * 0.6f + 1.5f) * 0.4f + 0.6f) * 20.0f;

		Renderer::PointLight& light2 = gameContext.renderer->GetPointLight(m_PointLight2ID);
		light2.position.z = (cos(gameContext.elapsedTime + PI)) * 30.0f + 15.0f;
		light2.position.x = (sin(gameContext.elapsedTime + PI)) * 30.0f - 15.0f;
		light2.position.y = (cos(gameContext.elapsedTime * 0.23f + 2.51f) * 0.4f + 0.6f) * 20.0f;

		Renderer::PointLight& light3 = gameContext.renderer->GetPointLight(m_PointLight3ID);
		light3.position.z = (sin(gameContext.elapsedTime)) * 10.0f;
		light3.position.x = (cos(gameContext.elapsedTime)) * 10.0f;
		light3.position.y = (cos(gameContext.elapsedTime * 2.56f + 1.2f) * 0.4f + 0.6f) * 20.0f;

		Renderer::PointLight& light4 = gameContext.renderer->GetPointLight(m_PointLight4ID);
		light4.position.z = (cos(gameContext.elapsedTime + glm::three_over_two_pi<float>())) * 20.0f + 5.0f;
		light4.position.x = (sin(gameContext.elapsedTime + glm::three_over_two_pi<float>())) * 20.0f - 5.0f;
		light4.position.y = (cos(gameContext.elapsedTime * 1.1f + 3.6f) * 0.4f + 0.6f) * 20.0f;
	}
} // namespace flex
