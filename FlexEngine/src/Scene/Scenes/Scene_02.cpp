#include "stdafx.hpp"

#include "Scene/Scenes/Scene_02.hpp"

#include <glm/vec3.hpp>

#include "Scene/ReflectionProbe.hpp"
#include "Logger.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/RigidBody.hpp"

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

#if 1 // Skybox
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
		skyboxHDRMatInfo.environmentMapPath = RESOURCE_LOCATION + "textures/hdri/Milkyway/Milkyway_Light.hdr";
		m_SkyboxMatID_1 = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);

		//m_SkyboxMatID_2 = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);

		//skyboxHDRMatInfo.environmentMapPath = RESOURCE_LOCATION + "textures/hdri/wobbly_bridge_8k.hdr";
		//m_SkyboxMatID_3 = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);

		//skyboxHDRMatInfo.environmentMapPath = RESOURCE_LOCATION + "textures/hdri/Arches_E_PineTree/Arches_E_PineTree_3k.hdr";
		//m_SkyboxMatID_4 = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);

		//skyboxHDRMatInfo.environmentMapPath = RESOURCE_LOCATION + "textures/hdri/Ice_Lake/Ice_Lake_Ref.hdr";
		//m_SkyboxMatID_5 = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);

		//RESOURCE_LOCATION + "textures/hdri/rustig_koppie_1k.hdr";
		//RESOURCE_LOCATION + "textures/hdri/wobbly_bridge_8k.hdr";
		//RESOURCE_LOCATION + "textures/hdri/Arches_E_PineTree/Arches_E_PineTree_3k.hdr";
		//RESOURCE_LOCATION + "textures/hdri/Ice_Lake/Ice_Lake_Ref.hdr";
		//RESOURCE_LOCATION + "textures/hdri/Protospace_B/Protospace_B_Ref.hdr";

		m_CurrentSkyboxMatID = 0;
#endif

#if 0 // Cornell Box
		Renderer::MaterialCreateInfo cornellBoxMatInfo = {};
		cornellBoxMatInfo.shaderName = "deferred_simple";
		cornellBoxMatInfo.name = "Cornell Box";
		const MaterialID cornellBoxMatID = gameContext.renderer->InitializeMaterial(gameContext, &cornellBoxMatInfo);

		MeshPrefab* cornellBoxMesh = new MeshPrefab(cornellBoxMatID, "Cornell Box");
		cornellBoxMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/CornellBox/CornellBox-01.fbx", true, true);
		cornellBoxMesh->GetTransform().Scale(15.0f);
		cornellBoxMesh->GetTransform().Rotate(0.0f, glm::radians(180.0f), 0.0f);
		AddChild(gameContext, cornellBoxMesh);
#endif

#if 1 // Grid
		Renderer::MaterialCreateInfo gridMatInfo = {};
		gridMatInfo.shaderName = "color";
		gridMatInfo.name = "Color";
		m_GridMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &gridMatInfo);

		m_Grid = new MeshPrefab(m_GridMaterialID, "Grid");
		m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
		m_Grid->GetTransform().Translate(0.0f, -0.1f, 0.0f);
		AddChild(gameContext, m_Grid);

		Renderer::MaterialCreateInfo worldAxisMatInfo = {};
		worldAxisMatInfo.shaderName = "color";
		worldAxisMatInfo.name = "Color";
		m_WorldAxisMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &worldAxisMatInfo);

		m_Grid = new MeshPrefab(m_WorldAxisMaterialID, "Grid origin");
		m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::WORLD_AXIS_GROUND);
		m_Grid->GetTransform().Translate(0.0f, -0.1f, 0.0f);
		AddChild(gameContext, m_Grid);
#endif

#if 0 // Cerebus
		Renderer::MaterialCreateInfo cerebusMatTexturedInfo = {};
		cerebusMatTexturedInfo.name = "Cerebus";
		cerebusMatTexturedInfo.shaderName = "pbr";
		cerebusMatTexturedInfo.enableAlbedoSampler = true;
		cerebusMatTexturedInfo.generateAlbedoSampler = true;
		cerebusMatTexturedInfo.albedoTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_A.tga";
		cerebusMatTexturedInfo.enableMetallicSampler = true;
		cerebusMatTexturedInfo.generateMetallicSampler = true;
		cerebusMatTexturedInfo.metallicTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_M.tga";
		cerebusMatTexturedInfo.enableRoughnessSampler = true;
		cerebusMatTexturedInfo.generateRoughnessSampler = true;
		cerebusMatTexturedInfo.roughnessTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_R.tga";
		cerebusMatTexturedInfo.enableAOSampler = true;
		cerebusMatTexturedInfo.generateAOSampler = true;
		cerebusMatTexturedInfo.aoTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Raw/Cerberus_AO.tga";
		cerebusMatTexturedInfo.enableNormalSampler = true;
		cerebusMatTexturedInfo.generateNormalSampler = true;
		cerebusMatTexturedInfo.normalTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_N.tga";
		const MaterialID cerebusMatID = gameContext.renderer->InitializeMaterial(gameContext, &cerebusMatTexturedInfo);
#endif

#if 0 // Cerebus 1
		m_Cerberus = new MeshPrefab(cerebusMatID, "Cerberus");
		m_Cerberus->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Cerberus_LP_WithB&T.fbx", true, true, false, true);
		AddChild(gameContext, m_Cerberus);
		m_Cerberus->GetTransform().Scale(0.075f, 0.075f, 0.075f);
		m_Cerberus->GetTransform().Translate(0.0f, 0.0f, 0.0f);
#endif

#if 0 // Cerebus 2
		MeshPrefab* extraCerberus = new MeshPrefab(cerebusMatID, "Cerberus 2");
		extraCerberus->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Cerberus_LP_WithB&T.fbx", true, true, false, true);
		AddChild(gameContext, extraCerberus);
		extraCerberus->GetTransform().Scale(0.075f, 0.075f, 0.075f);
		extraCerberus->GetTransform().Translate(10.0f, 18.0f, -5.0f);
		m_Cerberus->GetTransform().Rotate(0.0, -0.5f, 0.0f);
#endif

#if 1 // Spheres
		const i32 sphereCountX = 7;
		const i32 sphereCountY = 2;
		const i32 sphereCountZ = 1;
		const real sphereSpacing = 3.0f;
		const glm::vec3 offset = glm::vec3(
			-sphereCountX / 2 * sphereSpacing, 
			-sphereCountY / 2 * sphereSpacing + 4.0f, 
			-sphereCountZ / 2 * sphereSpacing);
		const size_t sphereCount = sphereCountX * sphereCountY * sphereCountZ;
		m_Spheres.resize(sphereCount);
		for (size_t i = 0; i < sphereCount; ++i)
		{
			i32 x = i % sphereCountX;
			i32 y = i32(i / sphereCountX) % sphereCountY;
			i32 z = i32(i / (sphereCountX * sphereCountY));

			const std::string iStr = std::to_string(i);

			MaterialID matID;

			if ((x + y + z) % 2 == 0)
			{
				Renderer::MaterialCreateInfo pbrMatInfo = {};
				pbrMatInfo.shaderName = "pbr";
				pbrMatInfo.name = "PBR " + iStr;
				//pbrMatInfo.constAlbedo = glm::vec3(0.25f, 0.14f, 0.95f);
				//pbrMatInfo.constAlbedo = glm::vec3(0.95f, 0.22f, 0.2f);
				pbrMatInfo.constAlbedo = glm::vec3(0.5f, 0.25f, 0.5f);
				pbrMatInfo.constMetallic = 1.0f;
				pbrMatInfo.constRoughness = glm::clamp(real(x + y) / (sphereCountX + sphereCountY - 2) * 0.75f, 0.05f, 0.9f);
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
				pbrMatInfo.constRoughness = glm::clamp((real(x + y) / (sphereCountX + sphereCountY - 2)) * 0.5f, 0.05f, 0.9f);
				pbrMatInfo.constAO = 1.0f;
				matID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);
			}

			m_Spheres[i] = new MeshPrefab(matID, "Sphere " + iStr);

			m_Spheres[i]->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/sphere.fbx", true, true);
			m_Spheres[i]->GetTransform().SetLocalPosition(offset + glm::vec3(x * sphereSpacing, y * sphereSpacing, z * sphereSpacing));
			AddChild(gameContext, m_Spheres[i]);
		}
#endif

#if 0 // White sphere
		Renderer::MaterialCreateInfo pbrMatInfo = {};
		pbrMatInfo.shaderName = "pbr";
		pbrMatInfo.name = "White PBR";
		pbrMatInfo.constAlbedo = glm::vec3(0.95, 0.95, 0.95f);
		pbrMatInfo.constMetallic = 0.0f;
		pbrMatInfo.constRoughness = 0.95f;
		pbrMatInfo.constAO = 1.0f;
		MaterialID whiteSphereMatID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);

		MeshPrefab* whiteSphere = new MeshPrefab(whiteSphereMatID, "White sphere");
		whiteSphere->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/sphere.fbx", true, true);
		whiteSphere->GetTransform().Translate(0.0, 19.0, 0.0f);
		whiteSphere->GetTransform().Scale(3.5f);
		AddChild(gameContext, whiteSphere);
#endif

#if 1 // Reflection probe
		// Generated last so it can use generated skybox maps
		m_ReflectionProbe = new ReflectionProbe(true);
		AddChild(gameContext, m_ReflectionProbe);
#endif

		gameContext.renderer->SetSkyboxMaterial(m_SkyboxMatID_1);

		m_PhysicsWorld = new PhysicsWorld();
		m_PhysicsWorld->Initialize(gameContext);

		m_PhysicsWorld->GetWorld()->setGravity({ 0.0f, -9.81f, 0.0f });
		m_BoxShape = gameContext.physicsManager->CreateBoxShape({ 1.0f, 1.0f, 1.0f });
		// TODO: Add rigid bodies to the scene and attach meshes!

		btTransform groundPlaneTransform = btTransform::getIdentity();
		groundPlaneTransform.setOrigin({ 20, 0, 0 });
		btTransform rb1Transform = btTransform::getIdentity();
		rb1Transform.setOrigin({ 20, 10, 0 });
		btTransform rb2Transform = btTransform::getIdentity();
		rb2Transform.setOrigin({ 20, 15, 0 });

		rb1 = new RigidBody(1, 1);
		rb1->SetMass(1.0f);
		rb1->Initialize(m_BoxShape, gameContext, rb1Transform);

		rb2 = new RigidBody(1, 1);
		rb2->SetMass(0.85f);
		rb2->Initialize(m_BoxShape, gameContext, rb2Transform);

		groundPlaneRB = new RigidBody(1, 1);
		groundPlaneRB->SetMass(0.0f);
		groundPlaneRB->Initialize(m_BoxShape, gameContext, groundPlaneTransform, false, true);

		{
			Renderer::MaterialCreateInfo pbrMatInfo = {};
			pbrMatInfo.shaderName = "pbr";
			pbrMatInfo.name = "PBR box";
			//pbrMatInfo.constAlbedo = glm::vec3(0.25f, 0.14f, 0.95f);
			//pbrMatInfo.constAlbedo = glm::vec3(0.95f, 0.22f, 0.2f);
			pbrMatInfo.constAlbedo = glm::vec3(0.2f, 0.89f, 0.95f);
			pbrMatInfo.constMetallic = 1.0f;
			pbrMatInfo.constRoughness = 0.1f;
			pbrMatInfo.constAO = 1.0f;
			MaterialID boxMat1ID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);

			pbrMatInfo.constMetallic = 1.0f;
			pbrMatInfo.constRoughness = 0.22f;
			pbrMatInfo.constAlbedo = glm::vec3(0.92f, 0.93f, 0.95f);
			MaterialID boxMat2ID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);

			pbrMatInfo.constMetallic = 1.0f;
			pbrMatInfo.constRoughness = 0.22f;
			pbrMatInfo.constAlbedo = glm::vec3(0.9f, 0.25f, 0.1f);
			MaterialID boxMat3ID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);

			m_GroundPlane = new MeshPrefab(boxMat1ID, "Ground Plane");
			m_GroundPlane->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/cube.fbx", true, true);
			m_GroundPlane->GetTransform().SetLocalPosition(glm::vec3(20, 0, 0));
			AddChild(gameContext, m_GroundPlane);
			m_GroundPlane->GetTransform().SetGlobalScale(glm::vec3(3.0f, 0.4f, 3.0f));

			m_Box1 = new MeshPrefab(boxMat2ID, "Box 2");
			m_Box1->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/cube.fbx", true, true);
			m_Box1->GetTransform().SetLocalPosition(glm::vec3(20, 10, 0));
			AddChild(gameContext, m_Box1);
			m_Box1->GetTransform().SetGlobalScale(glm::vec3(0.4f, 0.4f, 1.0f));

			m_Box2 = new MeshPrefab(boxMat3ID, "Box 2");
			m_Box2->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/cube.fbx", true, true);
			m_Box2->GetTransform().SetLocalPosition(glm::vec3(20, 15, 0));
			AddChild(gameContext, m_Box2);
			m_Box2->GetTransform().SetGlobalScale(glm::vec3(1.0f, 0.4f, 0.4f));

			//glm::vec3 pos;
			//glm::quat rot;
			//glm::vec3 scale;
			//rb1->GetTransform(pos, rot, scale);
			//rb2->GetTransform(pos, rot, scale);

			//rb1->SetPosition(m_Box1->GetTransform().GetGlobalPosition());
			//rb1->SetRotation(m_Box1->GetTransform().GetGlobalRotation());
			//rb1->SetScale(m_Box1->GetTransform().GetGlobalScale());

			//rb2->SetPosition(m_Box2->GetTransform().GetGlobalPosition());
			//rb2->SetRotation(m_Box2->GetTransform().GetGlobalRotation());
			//rb2->SetScale(m_Box2->GetTransform().GetGlobalScale());
			
		}
	}

	void Scene_02::PostInitialize(const GameContext& gameContext)
	{
		gameContext.renderer->SetReflectionProbeMaterial(m_ReflectionProbe->GetCaptureMaterialID());

		m_ReflectionProbe->GetTransform().Translate(0.0f, 7.5f, 0.0f);
	}

	void Scene_02::Destroy(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
		if (m_ReflectionProbe)
		{
			m_ReflectionProbe->Destroy(gameContext);
		}

		if (groundPlaneRB)
		{
			groundPlaneRB->Destroy(gameContext);
			SafeDelete(groundPlaneRB);
		}

		if (rb1)
		{
			rb1->Destroy(gameContext);
			SafeDelete(rb1);
		}

		if (rb2)
		{
			rb2->Destroy(gameContext);
			SafeDelete(rb2);
		}
	}

	void Scene_02::Update(const GameContext& gameContext)
	{
		// Circle
		//real moveSpeed = 1.25f;
		//real hDist = 10.0f;
		//real vDist = 8.0f;

		//const glm::vec3 pPos = gameContext.camera->GetPosition();

		//gameContext.camera->SetPosition({
		//	cos(gameContext.elapsedTime * moveSpeed) * hDist + 0.0f, 
		//	sin(gameContext.elapsedTime * moveSpeed) * vDist + 10.0f,
		//	pPos.z });
		//gameContext.camera->LookAt(glm::vec3(0.0f, 12.5f, 0.0f));

		glm::vec3 pos;
		glm::quat rot;
		glm::vec3 scale;
		rb1->GetTransform(pos, rot, scale);

		m_Box1->GetTransform().SetGlobalPosition(pos);
		m_Box1->GetTransform().SetGlobalRotation(rot);

		rb2->GetTransform(pos, rot, scale);
		m_Box2->GetTransform().SetGlobalPosition(pos);
		m_Box2->GetTransform().SetGlobalRotation(rot);

		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_SPACE))
		{
			rb1->GetRigidBodyInternal()->activate(false);
			rb1->GetRigidBodyInternal()->clearForces();
			rb1->GetRigidBodyInternal()->applyCentralForce({ 0, 600, 0 });

			rb2->GetRigidBodyInternal()->activate(false);
			rb2->GetRigidBodyInternal()->clearForces();
			rb2->GetRigidBodyInternal()->applyCentralForce({ 0, 600, 0 });
		}


		float maxHeightVisible = 300.0f;
		float distCamToGround = gameContext.camera->GetPosition().y;
		float maxDistVisible = 300.0f;
		float distCamToOrigin = glm::distance(gameContext.camera->GetPosition(), glm::vec3(0, 0, 0));

		glm::vec4 gridColorMutliplier = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f - glm::clamp(distCamToGround / maxHeightVisible, -1.0f, 1.0f));
		glm::vec4 axisColorMutliplier = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f - glm::clamp(distCamToOrigin / maxDistVisible, -1.0f, 1.0f));;
		gameContext.renderer->GetMaterial(m_WorldAxisMaterialID).colorMultiplier = axisColorMutliplier;
		gameContext.renderer->GetMaterial(m_GridMaterialID).colorMultiplier = gridColorMutliplier;

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
			const real moveSpeed = 0.075f;

			const glm::vec3 pPos = gameContext.camera->GetPosition();
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

		//if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_G))
		//{
		//	++m_CurrentSkyboxMatID;
		//	m_CurrentSkyboxMatID %= 5;
		//	MaterialID newMatID = 0;
		//	if (m_CurrentSkyboxMatID == 0) newMatID = m_SkyboxMatID_1;
		//	if (m_CurrentSkyboxMatID == 1) newMatID = m_SkyboxMatID_2;
		//	if (m_CurrentSkyboxMatID == 2) newMatID = m_SkyboxMatID_3;
		//	if (m_CurrentSkyboxMatID == 3) newMatID = m_SkyboxMatID_4;
		//	if (m_CurrentSkyboxMatID == 4) newMatID = m_SkyboxMatID_5;

		//	Logger::LogInfo("index: " + std::to_string(m_CurrentSkyboxMatID) + " new mat id: " + std::to_string(newMatID));
		//	gameContext.renderer->SetSkyboxMaterial(newMatID);
		//	gameContext.renderer->PostInitializeRenderObject(gameContext, m_Skybox->GetRenderID());
		//	m_Skybox->SetMaterialID(newMatID, gameContext);
		//}
	}
} // namespace flex
