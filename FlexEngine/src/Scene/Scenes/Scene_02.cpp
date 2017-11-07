#include "stdafx.hpp"

#include "Scene/Scenes/Scene_02.hpp"
#include "Scene/ReflectionProbe.hpp"

#include <glm/vec3.hpp>

namespace flex
{
	Scene_02::Scene_02(const GameContext& gameContext) :
		BaseScene("Scene_02")
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	Scene_02::~Scene_02()
	{
	}

	void Scene_02::Initialize(const GameContext& gameContext)
	{
		// Lights
		Renderer::PointLight light1 = {};
		light1.position = glm::vec4(-6.0f, -6.0f, -8.0f, 0.0f);
		light1.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light1);

		Renderer::PointLight light2 = {};
		light2.position = glm::vec4(-6.0f, 6.0f, -8.0f, 0.0f);
		light2.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light2);

		Renderer::PointLight light3 = {};
		light3.position = glm::vec4(6.0f, 6.0f, -8.0f, 0.0f);
		light3.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light3);

		Renderer::PointLight light4 = {};
		light4.position = glm::vec4(6.0f, -6.0f, -8.0f, 0.0f);
		light4.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light4);

		// Materials
		Renderer::MaterialCreateInfo colorMatInfo = {};
		colorMatInfo.shaderName = "color";
		colorMatInfo.name = "Color";
		const MaterialID colorMatID = gameContext.renderer->InitializeMaterial(gameContext, &colorMatInfo);

		Renderer::MaterialCreateInfo skyboxHDRMatInfo = {};
		skyboxHDRMatInfo.name = "HDR Skybox";
		skyboxHDRMatInfo.shaderName = "background";
		skyboxHDRMatInfo.generateHDRCubemapSampler = true;
		skyboxHDRMatInfo.enableCubemapSampler = true;
		skyboxHDRMatInfo.enableCubemapTrilinearFiltering = true;
		skyboxHDRMatInfo.generatedCubemapSize = { 512, 512 };
		skyboxHDRMatInfo.generateIrradianceSampler = true;
		skyboxHDRMatInfo.generatedIrradianceCubemapSize = { 32, 32 };
		skyboxHDRMatInfo.generatePrefilteredMap = true;
		skyboxHDRMatInfo.generatedPrefilteredCubemapSize = { 128, 128 };
		const MaterialID skyboxHDRMatID = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);

		//Renderer::MaterialCreateInfo cerebusMatTexturedInfo = {};
		//cerebusMatTexturedInfo.name = "Cerebus";
		//cerebusMatTexturedInfo.shaderName = "pbr";
		//cerebusMatTexturedInfo.enableAlbedoSampler = true;
		//cerebusMatTexturedInfo.generateAlbedoSampler = true;
		//cerebusMatTexturedInfo.albedoTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_A.tga";
		//cerebusMatTexturedInfo.enableMetallicSampler = true;
		//cerebusMatTexturedInfo.generateMetallicSampler = true;
		//cerebusMatTexturedInfo.metallicTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_M.tga";
		//cerebusMatTexturedInfo.enableRoughnessSampler = true;
		//cerebusMatTexturedInfo.generateRoughnessSampler = true;
		//cerebusMatTexturedInfo.roughnessTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_R.tga";
		//cerebusMatTexturedInfo.enableAOSampler = true;
		//cerebusMatTexturedInfo.generateAOSampler = true;
		//cerebusMatTexturedInfo.aoTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Raw/Cerberus_AO.tga";
		//cerebusMatTexturedInfo.enableNormalSampler = true;
		//cerebusMatTexturedInfo.generateNormalSampler = true;
		//cerebusMatTexturedInfo.normalTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_N.tga";
		//const MaterialID cerebusMatID = gameContext.renderer->InitializeMaterial(gameContext, &cerebusMatTexturedInfo);

		//Renderer::MaterialCreateInfo brickMatInfo = {};
		//brickMatInfo.shaderName = "deferred_simple";
		//brickMatInfo.name = "Brick";
		//brickMatInfo.enableDiffuseSampler = true;
		//brickMatInfo.generateDiffuseSampler = true;
		//brickMatInfo.diffuseTexturePath = RESOURCE_LOCATION + "textures/brick_d.png";
		//brickMatInfo.enableNormalSampler = true;
		//brickMatInfo.generateNormalSampler = true;
		//brickMatInfo.normalTexturePath = RESOURCE_LOCATION + "textures/brick_n.png";
		//const MaterialID brickMatID = gameContext.renderer->InitializeMaterial(gameContext, &brickMatInfo);

		//Renderer::MaterialCreateInfo cornellBoxMatInfo = {};
		//cornellBoxMatInfo.shaderName = "deferred_simple";
		//cornellBoxMatInfo.name = "Cornell Box";
		//const MaterialID cornellBoxMatID = gameContext.renderer->InitializeMaterial(gameContext, &cornellBoxMatInfo);


		//MeshPrefab* cornellBoxMesh = new MeshPrefab(cornellBoxMatID, "Cornell Box");
		//cornellBoxMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/CornellBox/CornellBox-01.fbx", true, true);
		//cornellBoxMesh->GetTransform().Scale(15.0f);
		//cornellBoxMesh->GetTransform().Rotate(0.0f, glm::radians(180.0f), 0.0f);
		//AddChild(gameContext, cornellBoxMesh);

		//m_Grid = new MeshPrefab(colorMatID, "Grid");
		//m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
		//m_Grid->GetTransform().Translate(0.0f, -0.1f, 0.0f);
		//m_Grid->GetTransform().Scale(0.2f);
		//AddChild(gameContext, m_Grid);

		m_Skybox = new MeshPrefab(skyboxHDRMatID, "Skybox");
		m_Skybox->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::SKYBOX);
		AddChild(gameContext, m_Skybox);

		//m_Cerberus = new MeshPrefab(cerebusMatID, "Cerberus");
		//m_Cerberus->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Cerberus_LP_WithB&T.fbx", true, true, false, true);
		//AddChild(gameContext, m_Cerberus);
		//m_Cerberus->GetTransform().Scale(0.075f, 0.075f, 0.075f);
		//m_Cerberus->GetTransform().Translate(-10.0f, 14.0f, 0.0f);

		//MeshPrefab* extraCerberus = new MeshPrefab(cerebusMatID, "Cerberus 2");
		//extraCerberus->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Cerberus_LP_WithB&T.fbx", true, true, false, true);
		//AddChild(gameContext, extraCerberus);
		//extraCerberus->GetTransform().Scale(0.075f, 0.075f, 0.075f);
		//extraCerberus->GetTransform().Translate(10.0f, 18.0f, -5.0f);
		//m_Cerberus->GetTransform().Rotate(0.0, -0.5f, 0.0f);

		const int sphereCountX = 3;
		const int sphereCountY = 3;
		const int sphereCountZ = 3;
		const float sphereSpacing = 3.0f;
		const glm::vec3 offset = glm::vec3(
			-sphereCountX / 2 * sphereSpacing, 
			-sphereCountY / 2 * sphereSpacing + 4.0f, 
			-sphereCountZ / 2 * sphereSpacing);
		const size_t sphereCount = sphereCountX * sphereCountY * sphereCountZ;
		m_Spheres.resize(sphereCount);
		for (size_t i = 0; i < sphereCount; ++i)
		{
			int x = i % sphereCountX;
			int y = int(i / sphereCountX) % sphereCountY;
			int z = int(i / (sphereCountX * sphereCountY));

			const std::string iStr = std::to_string(i);

			MaterialID matID;

			if ((x + y + z) % 2 == 0)
			{
				Renderer::MaterialCreateInfo pbrMatInfo = {};
				pbrMatInfo.shaderName = "pbr";
				pbrMatInfo.name = "PBR " + iStr;
				pbrMatInfo.constAlbedo = glm::vec3(0.25f, 0.14f, 0.5f);
				pbrMatInfo.constMetallic = 0.98f;
				pbrMatInfo.constRoughness = glm::clamp(float(x) / (sphereCountX - 1) * 0.25f, 0.05f, 0.9f);
				pbrMatInfo.constAO = 1.0f;
				matID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);
			}
			else
			{
				Renderer::MaterialCreateInfo pbrMatInfo = {};
				pbrMatInfo.shaderName = "pbr";
				pbrMatInfo.name = "PBR " + iStr;
				//pbrMatInfo.constAlbedo = glm::vec3(0.95f, 0.95f, 0.95f); // Chrome
				pbrMatInfo.constAlbedo = glm::vec3(1.0f, .71f, 0.29f); // Gold
				pbrMatInfo.constMetallic = 1.0f;
				pbrMatInfo.constRoughness = glm::clamp(float(x) / (sphereCountX - 1) * 0.25f, 0.05f, 0.9f);
				pbrMatInfo.constAO = 1.0f;
				matID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);
			}

			m_Spheres[i] = new MeshPrefab(matID, "Sphere " + iStr);

			m_Spheres[i]->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/sphere.fbx", true, true);
			m_Spheres[i]->GetTransform().SetLocalPosition(offset + glm::vec3(x * sphereSpacing, y * sphereSpacing, z * sphereSpacing));
			AddChild(gameContext, m_Spheres[i]);
		}

		//Renderer::MaterialCreateInfo pbrMatInfo = {};
		//pbrMatInfo.shaderName = "pbr";
		//pbrMatInfo.name = "White PBR";
		//pbrMatInfo.constAlbedo = glm::vec3(0.95, 0.95, 0.95f);
		//pbrMatInfo.constMetallic = 0.0f;
		//pbrMatInfo.constRoughness = 0.95f;
		//pbrMatInfo.constAO = 1.0f;
		//MaterialID whiteSphereMatID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);

		//MeshPrefab* whiteSphere = new MeshPrefab(whiteSphereMatID, "White sphere");
		//whiteSphere->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/sphere.fbx", true, true);
		//whiteSphere->GetTransform().Translate(0.0, 19.0, 0.0f);
		//whiteSphere->GetTransform().Scale(3.5f);
		//AddChild(gameContext, whiteSphere);

		// Reflection probes
		// Generate last so it can use generated skybox maps
		m_ReflectionProbe = new ReflectionProbe();
		AddChild(gameContext, m_ReflectionProbe);
		m_ReflectionProbe->GetTransform().Translate(0.0f, 10.0f, 0.0f);
		m_ReflectionProbe->GetTransform().Scale(3.5f);
	}

	void Scene_02::PostInitialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void Scene_02::Destroy(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void Scene_02::Update(const GameContext& gameContext)
	{
		// Circle
		//float moveSpeed = 1.25f;
		//float hDist = 10.0f;
		//float vDist = 8.0f;

		//const glm::vec3 pPos = gameContext.camera->GetPosition();

		//gameContext.camera->SetPosition({
		//	cos(gameContext.elapsedTime * moveSpeed) * hDist + 0.0f, 
		//	sin(gameContext.elapsedTime * moveSpeed) * vDist + 10.0f,
		//	pPos.z });
		//gameContext.camera->LookAt(glm::vec3(0.0f, 12.5f, 0.0f));

		// Orbit
		static glm::vec3 startCamPos(0.0f);
		static glm::vec2 startMousePos(0.0f);
		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_LEFT_CONTROL))
		{
			startMousePos = gameContext.inputManager->GetMousePosition();
			startCamPos = gameContext.camera->GetPosition();
			//Logger::LogInfo(std::to_string(startMousePos.x) + " " + std::to_string(startMousePos.y));
		}
		else if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_CONTROL))
		{
			const float moveSpeed = 0.075f;

			const glm::vec3 pPos = gameContext.camera->GetPosition();
			startCamPos.z = pPos.z;
			const glm::vec2 mouseMove = gameContext.inputManager->GetMousePosition() - startMousePos;
			//Logger::LogInfo(std::to_string(mouseMove.x) + " " + std::to_string(mouseMove.y));

			const glm::uvec2 windowSize = gameContext.window->GetSize();

			const glm::vec3 camRight = gameContext.camera->GetRight();
			const glm::vec3 camUp = gameContext.camera->GetUp();
			const glm::vec3 camForward = gameContext.camera->GetForward();

			gameContext.camera->SetPosition(pPos + 
				camRight * (mouseMove.x / windowSize.x * moveSpeed) +
				camUp  * (-mouseMove.y / windowSize.y * moveSpeed) +
				camForward * (0.0f));
			gameContext.camera->LookAt(glm::vec3(0.0f, 4.0f, 0.0f), gameContext.deltaTime * 5.0f);
		}
	}
} // namespace flex
