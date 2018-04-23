#include "stdafx.hpp"

#include "Scene/Scenes/Scene_02.hpp"

#include <glm/vec3.hpp>

#include "Cameras/CameraManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Scene/ReflectionProbe.hpp"
#include "Logger.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/RigidBody.hpp"

namespace flex
{
	Scene_02::Scene_02(const GameContext& gameContext) :
		BaseScene("Scene_02", "")
	{
		UNREFERENCED_PARAMETER(gameContext);
		Logger::LogError("FATAL: DEPRECATED CLASS INSTANTITED");
		assert(false);
	}

	Scene_02::~Scene_02()
	{
	}

	void Scene_02::Initialize(const GameContext& gameContext)
	{
		// Lights
		PointLight light1 = {};
		light1.position = glm::vec4(-6.0f, -6.0f, -8.0f, 0.0f);
		light1.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light1);

		PointLight light2 = {};
		light2.position = glm::vec4(-6.0f, 6.0f, -8.0f, 0.0f);
		light2.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light2);

		PointLight light3 = {};
		light3.position = glm::vec4(6.0f, 6.0f, -8.0f, 0.0f);
		light3.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light3);

		PointLight light4 = {};
		light4.position = glm::vec4(6.0f, -6.0f, -8.0f, 0.0f);
		light4.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light4);

#if 1 // Skybox
		MaterialCreateInfo skyboxHDRMatInfo = {};
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
		MaterialCreateInfo cornellBoxMatInfo = {};
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
		MaterialCreateInfo gridMatInfo = {};
		gridMatInfo.shaderName = "color";
		gridMatInfo.name = "Color";
		m_GridMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &gridMatInfo);

		m_Grid = new MeshPrefab(m_GridMaterialID, "Grid");
		m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
		m_Grid->GetTransform().Translate(0.0f, -0.1f, 0.0f);
		AddChild(gameContext, m_Grid);

		MaterialCreateInfo worldAxisMatInfo = {};
		worldAxisMatInfo.shaderName = "color";
		worldAxisMatInfo.name = "Color";
		m_WorldAxisMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &worldAxisMatInfo);

		m_WorldOrigin = new MeshPrefab(m_WorldAxisMaterialID, "World origin");
		m_WorldOrigin->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::WORLD_AXIS_GROUND);
		m_WorldOrigin->GetTransform().Translate(0.0f, -0.09f, 0.0f);
		AddChild(gameContext, m_WorldOrigin);
#endif

#if 0 // Cerebus
		MaterialCreateInfo cerebusMatTexturedInfo = {};
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
				MaterialCreateInfo pbrMatInfo = {};
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
				MaterialCreateInfo pbrMatInfo = {};
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
		MaterialCreateInfo pbrMatInfo = {};
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

		box1Collider = {};
		box1Collider.CreateBoxCollider(gameContext, { 6.0f, 0.1f, 6.0f });

		box2Collider = {};
		box2Collider.CreateBoxCollider(gameContext, { 1.5f, 1.1f, 3.0f });

		box3Collider = {};
		box3Collider.CreateBoxCollider(gameContext, { 0.5f, 1.5f, 2.0f });

		glm::vec3 box1Scale = FromBtVec3(box1Collider.GetScale());
		glm::vec3 box2Scale = FromBtVec3(box2Collider.GetScale());
		glm::vec3 box3Scale = FromBtVec3(box3Collider.GetScale());

		btTransform rb1Transform = btTransform::getIdentity();
		rb1Transform.setOrigin({ 20, 0, 0 });
		btTransform rb2Transform = btTransform::getIdentity();
		rb2Transform.setOrigin({ 20, 10, 0 });
		btTransform rb3Transform = btTransform::getIdentity();
		rb3Transform.setOrigin({ 20, 15, 0 });

		rb1 = new RigidBody(1, 1);
		rb1->SetMass(0.0f);
		rb1->Initialize(box1Collider.GetShape(), gameContext, rb1Transform, false, true);

		rb2 = new RigidBody(1, 1);
		rb2->SetMass(1.0f);
		rb2->Initialize(box2Collider.GetShape(), gameContext, rb2Transform);

		rb3 = new RigidBody(1, 1);
		rb3->SetMass(0.85f);
		rb3->Initialize(box3Collider.GetShape(), gameContext, rb3Transform);

		{
			MaterialCreateInfo pbrMatInfo = {};
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

			m_Box1 = new MeshPrefab(boxMat1ID, "Box 1");
			//m_Box1->IgnoreAttributes((u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT);
			m_Box1->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/cube.fbx", true, true);
			AddChild(gameContext, m_Box1);
			m_Box1->GetTransform().SetGlobalScale(box1Scale);

			m_Box2 = new MeshPrefab(boxMat2ID, "Box 2");
			//m_Box2->IgnoreAttributes((u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT);
			m_Box2->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/cube.fbx", true, true);
			AddChild(gameContext, m_Box2);
			m_Box2->GetTransform().SetGlobalScale(box2Scale);

			m_Box3 = new MeshPrefab(boxMat3ID, "Box 3");
			//m_Box3->IgnoreAttributes((u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT);
			m_Box3->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/cube.fbx", true, true);
			AddChild(gameContext, m_Box3);
			m_Box3->GetTransform().SetGlobalScale(box3Scale);
		}

		m_Box1->GetTransform().MatchRigidBody(rb1, true);
		m_Box2->GetTransform().MatchRigidBody(rb2, true);
		m_Box3->GetTransform().MatchRigidBody(rb3, true);
	}

	void Scene_02::PostInitialize(const GameContext& gameContext)
	{
		gameContext.renderer->SetReflectionProbeMaterial(m_ReflectionProbe->GetCaptureMaterialID());

		m_ReflectionProbe->GetTransform().Translate(0.0f, 7.5f, 0.0f);
	}

	void Scene_02::Destroy(const GameContext& gameContext)
	{
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

		if (rb3)
		{
			rb3->Destroy(gameContext);
			SafeDelete(rb3);
		}
	}

	void Scene_02::Update(const GameContext& gameContext)
	{
		BaseCamera* camera = gameContext.cameraManager->CurrentCamera();

		m_Box1->GetTransform().MatchRigidBody(rb1);
		m_Box2->GetTransform().MatchRigidBody(rb2);
		m_Box3->GetTransform().MatchRigidBody(rb3);

		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_SPACE))
		{
			rb2->GetRigidBodyInternal()->activate();
			rb2->GetRigidBodyInternal()->clearForces();
			rb2->GetRigidBodyInternal()->applyCentralForce({ 0, 600, 0 });

			rb3->GetRigidBodyInternal()->activate();
			rb3->GetRigidBodyInternal()->clearForces();
			rb3->GetRigidBodyInternal()->applyCentralForce({ 0, 600, 0 });
		}


		float maxHeightVisible = 300.0f;
		float distCamToGround = camera->GetPosition().y;
		float maxDistVisible = 300.0f;
		float distCamToOrigin = glm::distance(camera->GetPosition(), glm::vec3(0, 0, 0));

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
			startCamPos = camera->GetPosition();
			//Logger::LogInfo(std::to_string(startMousePos.x) + " " + std::to_string(startMousePos.y));
		}
		else if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_CONTROL))
		{
			const real moveSpeed = 0.075f;

			const glm::vec3 pPos = camera->GetPosition();
			const glm::vec2 mouseMove = gameContext.inputManager->GetMousePosition() - startMousePos;
			//Logger::LogInfo(std::to_string(mouseMove.x) + " " + std::to_string(mouseMove.y));

			const glm::uvec2 windowSize = gameContext.window->GetSize();

			const glm::vec3 camRight = camera->GetRight();
			const glm::vec3 camUp = camera->GetUp();
			const glm::vec3 camForward = camera->GetForward();

			camera->SetPosition(pPos + 
				camRight * (mouseMove.x / windowSize.x * moveSpeed) +
				camUp  * (-mouseMove.y / windowSize.y * moveSpeed) +
				camForward * (0.0f));
			camera->LookAt(glm::vec3(0.0f, 4.0f, 0.0f), gameContext.deltaTime * 5.0f);
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
