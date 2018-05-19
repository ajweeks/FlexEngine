#include "stdafx.hpp"

#include "Scene/BaseScene.hpp"

#include <glm/vec3.hpp>

#include <fstream>

#pragma warning(push, 0)
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#pragma warning(pop)

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Helpers.hpp"
#include "JSONParser.hpp"
#include "Logger.hpp"
#include "Physics/PhysicsHelpers.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/ReflectionProbe.hpp"

namespace flex
{
	BaseScene::BaseScene(const std::string& name, const std::string& jsonFilePath) :
		m_Name(name),
		m_JSONFilePath(jsonFilePath)
	{
	}

	BaseScene::~BaseScene()
	{
	}

	void BaseScene::Initialize(const GameContext& gameContext)
	{
		// Physics world
		m_PhysicsWorld = new PhysicsWorld();
		m_PhysicsWorld->Initialize(gameContext);

		m_PhysicsWorld->GetWorld()->setGravity({ 0.0f, -9.81f, 0.0f });

		dud_dud_dud_dud = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/dud_dud_dud_dud.wav");
		drmapan = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/drmapan.wav");
		blip = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/blip.wav");
		//TRG_Banks = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/music/TRG_Banks.wav");
		//AudioManager::SetSourceLooping(TRG_Banks, true);
		//AudioManager::PlaySource(TRG_Banks);

		JSONObject sceneRootObject;
		if (!JSONParser::Parse(m_JSONFilePath, sceneRootObject))
		{
			Logger::LogError("Failed to parse scene JSON file \"" + m_JSONFilePath + "\"");
			return;
		}

		std::string friendlySceneFilepath = m_JSONFilePath.substr(RESOURCE_LOCATION.length());
		Logger::LogInfo("Loading scene from JSON file: " + friendlySceneFilepath);

		const bool printSceneContentsToConsole = false;
		if (printSceneContentsToConsole)
		{
			Logger::LogInfo("Parsed scene file:");
			Logger::LogInfo(sceneRootObject.Print(0));
		}

		int sceneVersion = sceneRootObject.GetInt("version");
		if (sceneVersion != 1)
		{
			if (sceneRootObject.HasField("version"))
			{
				Logger::LogError("Unhandled scene version! Max handled version: 1, This version: " + std::to_string(sceneVersion));
			}
			else
			{
				Logger::LogError("Scene version missing from scene file. Assuming version 1");
			}
		}

		sceneRootObject.SetStringChecked("name", m_Name);

		std::vector<JSONObject> materialsArray = sceneRootObject.GetObjectArray("materials");
		for (i32 i = 0; i < materialsArray.size(); ++i)
		{
			MaterialCreateInfo matCreateInfo = {};
			ParseMaterialJSONObject(materialsArray[i], matCreateInfo);

			MaterialID matID = gameContext.renderer->InitializeMaterial(gameContext, &matCreateInfo);
			m_LoadedMaterials.push_back(matID);
		}


		// This holds all the entities in the scene which do not have a parent
		const std::vector<JSONObject>& rootEntities = sceneRootObject.GetObjectArray("entities");
		for (const JSONObject& rootEntity : rootEntities)
		{
			AddChild(CreateEntityFromJSON(gameContext, rootEntity));
		}

		if (sceneRootObject.HasField("point lights"))
		{
			const std::vector<JSONObject>& pointLightsArray = sceneRootObject.GetObjectArray("point lights");

			for (const JSONObject& pointLightObj : pointLightsArray)
			{
				PointLight pointLight = {};
				CreatePointLightFromJSON(gameContext, pointLightObj, pointLight);

				gameContext.renderer->InitializePointLight(pointLight);
			}
		}

		if (sceneRootObject.HasField("directional light"))
		{
			const JSONObject& directionalLightObj = sceneRootObject.GetObject("directional light");

			DirectionalLight dirLight = {};
			CreateDirectionalLightFromJSON(gameContext, directionalLightObj, dirLight);
			
			gameContext.renderer->InitializeDirectionalLight(dirLight);
		}


		const std::string gridMatName = "Grid";
		if (!gameContext.renderer->GetMaterialID(gridMatName, m_GridMaterialID))
		{
			MaterialCreateInfo gridMatInfo = {};
			gridMatInfo.shaderName = "color";
			gridMatInfo.name = gridMatName;
			m_GridMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &gridMatInfo);
		}

		m_Grid = new GameObject("Grid", GameObjectType::NONE);
		MeshComponent* gridMesh = m_Grid->SetMeshComponent(new MeshComponent(m_GridMaterialID, m_Grid));
		gridMesh->LoadPrefabShape(gameContext, MeshComponent::PrefabShape::GRID);
		m_Grid->GetTransform()->Translate(0.0f, -0.1f, 0.0f);
		m_Grid->SetSerializable(false);
		m_Grid->SetStatic(true);
		AddChild(m_Grid);

		const std::string worldOriginMatName = "World origin";
		if (!gameContext.renderer->GetMaterialID(worldOriginMatName, m_WorldAxisMaterialID))
		{
			MaterialCreateInfo worldAxisMatInfo = {};
			worldAxisMatInfo.shaderName = "color";
			worldAxisMatInfo.name = worldOriginMatName;
			m_WorldAxisMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &worldAxisMatInfo);
		}

		m_WorldOrigin = new GameObject("World origin", GameObjectType::NONE);
		MeshComponent* orignMesh = m_WorldOrigin->SetMeshComponent(new MeshComponent(m_WorldAxisMaterialID, m_WorldOrigin));
		orignMesh->LoadPrefabShape(gameContext, MeshComponent::PrefabShape::WORLD_AXIS_GROUND);
		m_WorldOrigin->GetTransform()->Translate(0.0f, -0.09f, 0.0f);
		m_WorldOrigin->SetSerializable(false);
		m_WorldOrigin->SetStatic(true);
		AddChild(m_WorldOrigin);


		// Players
		m_Player0 = new Player(0);
		AddChild(m_Player0);

		m_Player1 = new Player(1);
		AddChild(m_Player1);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Initialize(gameContext);
		}
	}

	void BaseScene::PostInitialize(const GameContext& gameContext)
	{
		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->PostInitialize(gameContext);

			RigidBody* rb = (*iter)->GetRigidBody();

			if (rb)
			{
				rb->GetRigidBodyInternal()->setUserPointer(*iter);
			}

			switch ((*iter)->m_Type)
			{
			case GameObjectType::VALVE:
			{
				rb->SetPhysicsFlags((u32)PhysicsFlag::TRIGGER);
				auto rbInternal = rb->GetRigidBodyInternal();
				rbInternal->setAngularFactor(btVector3(0, 1, 0));
				// Mark as trigger
				rbInternal->setCollisionFlags(
					rbInternal->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
				rbInternal->setGravity(btVector3(0, 0, 0));
			} break;
			case GameObjectType::RISING_BLOCK:
			{
				auto rbInternal = rb->GetRigidBodyInternal();
				rbInternal->setGravity(btVector3(0, 0, 0));
			} break;
			}
		}

		m_PhysicsWorld->GetWorld()->setDebugDrawer(gameContext.renderer->GetDebugDrawer());
	}

	void BaseScene::Destroy(const GameContext& gameContext)
	{
		for (auto child : m_Children)
		{
			if (child)
			{
				child->Destroy(gameContext);
				SafeDelete(child);
			}
		}
		m_Children.clear();

		AudioManager::ClearAllAudioSources();

		m_LoadedMaterials.clear();

		gameContext.renderer->ClearMaterials();
		gameContext.renderer->SetSkyboxMesh(nullptr);
		gameContext.renderer->ClearDirectionalLight();
		gameContext.renderer->ClearPointLights();

		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Destroy();
			SafeDelete(m_PhysicsWorld);
		}
	}

	void BaseScene::Update(const GameContext& gameContext)
	{
		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Update(gameContext.deltaTime);
		}

		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_G))
		{
			m_Grid->SetVisible(!m_Grid->IsVisible());
			m_WorldOrigin->SetVisible(!m_WorldOrigin->IsVisible());
		}

		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_Z))
		{
			AudioManager::PlaySource(dud_dud_dud_dud);
		}
		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_X))
		{
			AudioManager::PauseSource(dud_dud_dud_dud);
		}

		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_C))
		{
			AudioManager::PlaySource(drmapan);
		}
		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_V))
		{
			AudioManager::PauseSource(drmapan);
		}
		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_B))
		{
			AudioManager::StopSource(drmapan);
		}

		if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_L))
		{
			AudioManager::AddToSourcePitch(dud_dud_dud_dud, 0.5f * gameContext.deltaTime);
		}
		if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_K))
		{
			AudioManager::AddToSourcePitch(dud_dud_dud_dud, -0.5f * gameContext.deltaTime);
		}

		if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_P))
		{
			AudioManager::ScaleSourceGain(dud_dud_dud_dud, 1.1f);
		}
		if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_O))
		{
			AudioManager::ScaleSourceGain(dud_dud_dud_dud, 1.0f / 1.1f);
		}


		BaseCamera* camera = gameContext.cameraManager->CurrentCamera();

		// Fade grid out when far away
		{
			float maxHeightVisible = 350.0f;
			float distCamToGround = camera->GetPosition().y;
			float maxDistVisible = 300.0f;
			float distCamToOrigin = glm::distance(camera->GetPosition(), glm::vec3(0, 0, 0));

			glm::vec4 gridColorMutliplier = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f - glm::clamp(distCamToGround / maxHeightVisible, -1.0f, 1.0f));
			glm::vec4 axisColorMutliplier = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f - glm::clamp(distCamToOrigin / maxDistVisible, -1.0f, 1.0f));;
			gameContext.renderer->GetMaterial(m_WorldAxisMaterialID).colorMultiplier = axisColorMutliplier;
			gameContext.renderer->GetMaterial(m_GridMaterialID).colorMultiplier = gridColorMutliplier;
		}

		for (GameObject* child : m_Children)
		{
			if (child)
			{
				child->Update(gameContext);
			}
		}
	}

	GameObject* BaseScene::CreateEntityFromJSON(const GameContext& gameContext, const JSONObject& obj)
	{
		GameObject* newEntity = nullptr;

		std::string entityTypeStr = obj.GetString("type");
		GameObjectType entityType = StringToGameObjectType(entityTypeStr);

		std::string objectName = obj.GetString("name");

		Transform transform = Transform::Identity();
		if (obj.HasField("transform"))
		{
			JSONObject transformObj = obj.GetObject("transform");
			transform = JSONParser::ParseTransform(transformObj);
		}

		bool bVisible = true;
		obj.SetBoolChecked("visible", bVisible);
		bool bVisibleInSceneGraph = true;
		obj.SetBoolChecked("visible in scene graph", bVisibleInSceneGraph);

		MaterialID matID = InvalidMaterialID;
		i32 materialIndex = -1;
		if (obj.SetIntChecked("material index", materialIndex))
		{
			if (materialIndex >= 0 &&
				materialIndex < m_LoadedMaterials.size())
			{
				matID = m_LoadedMaterials[materialIndex];
			}
			else
			{
				matID = InvalidMaterialID;
				Logger::LogError("Invalid material index for entity " + objectName + ": " +
					std::to_string(materialIndex));
			}
		}

		newEntity = new GameObject(objectName, entityType);

		JSONObject meshObj;
		if (obj.SetObjectChecked("mesh", meshObj))
		{
			std::string meshFilePath = meshObj.GetString("file");
			if (!meshFilePath.empty())
			{
				meshFilePath = RESOURCE_LOCATION + meshFilePath;
			}
			std::string meshPrefabName = meshObj.GetString("prefab");
			bool swapNormalYZ = meshObj.GetBool("swapNormalYZ");
			bool flipNormalZ = meshObj.GetBool("flipNormalZ");
			bool flipU = meshObj.GetBool("flipU");
			bool flipV = meshObj.GetBool("flipV");

			if (matID == InvalidMaterialID)
			{
				Logger::LogError("Mesh entity requires material index: " + objectName);
			}
			else
			{
				Material& material = gameContext.renderer->GetMaterial(matID);
				Shader& shader = gameContext.renderer->GetShader(material.shaderID);
				VertexAttributes requiredVertexAttributes = shader.vertexAttributes;


				if (!meshFilePath.empty())
				{
					MeshComponent* mesh = new MeshComponent(matID, newEntity);
					mesh->SetRequiredAttributes(requiredVertexAttributes);

					MeshComponent::ImportSettings importSettings = {};
					importSettings.flipU = flipU;
					importSettings.flipV = flipV;
					importSettings.flipNormalZ = flipNormalZ;
					importSettings.swapNormalYZ = swapNormalYZ;

					mesh->LoadFromFile(gameContext,
									   meshFilePath,
									   &importSettings);

					newEntity->SetMeshComponent(mesh);
				}
				else if (!meshPrefabName.empty())
				{
					MeshComponent* mesh = new MeshComponent(matID, newEntity);
					mesh->SetRequiredAttributes(requiredVertexAttributes);

					MeshComponent::PrefabShape prefabShape = MeshComponent::StringToPrefabShape(meshPrefabName);
					mesh->LoadPrefabShape(gameContext, prefabShape);

					newEntity->SetMeshComponent(mesh);
				}
				else
				{
					Logger::LogError("Unhandled mesh field on object: " + objectName);
				}
			}
		}

		JSONObject colliderObj;
		if (obj.SetObjectChecked("collider", colliderObj))
		{
			std::string shapeStr = colliderObj.GetString("shape");
			BroadphaseNativeTypes shapeType = StringToCollisionShapeType(shapeStr);

			switch (shapeType)
			{
			case BOX_SHAPE_PROXYTYPE:
			{
				glm::vec3 halfExtents;
				colliderObj.SetVec3Checked("half extents", halfExtents);
				btVector3 btHalfExtents(halfExtents.x, halfExtents.y, halfExtents.z);
				btBoxShape* boxShape = new btBoxShape(btHalfExtents);

				newEntity->SetCollisionShape(boxShape);
			} break;
			case SPHERE_SHAPE_PROXYTYPE:
			{
				real radius = colliderObj.GetFloat("radius");
				btSphereShape* sphereShape = new btSphereShape(radius);

				newEntity->SetCollisionShape(sphereShape);
			} break;
			case CAPSULE_SHAPE_PROXYTYPE:
			{
				real radius = colliderObj.GetFloat("radius");
				real height = colliderObj.GetFloat("height");
				btCapsuleShape* capsuleShape = new btCapsuleShape(radius, height);

				newEntity->SetCollisionShape(capsuleShape);
			} break;
			case CONE_SHAPE_PROXYTYPE:
			{
				real radius = colliderObj.GetFloat("radius");
				real height = colliderObj.GetFloat("height");
				btConeShape* coneShape = new btConeShape(radius, height);

				newEntity->SetCollisionShape(coneShape);
			} break;
			case CYLINDER_SHAPE_PROXYTYPE:
			{
				glm::vec3 halfExtents;
				colliderObj.SetVec3Checked("half extents", halfExtents);
				btVector3 btHalfExtents(halfExtents.x, halfExtents.y, halfExtents.z);
				btCylinderShape* cylinderShape = new btCylinderShape(btHalfExtents);

				newEntity->SetCollisionShape(cylinderShape);
			} break;
			default:
			{
				Logger::LogError("Unhandled BroadphaseNativeType: " + shapeStr);
			} break;
			}

			bool bIsTrigger = colliderObj.GetBool("trigger");
			// TODO: Create btGhostObject to use for trigger
		}

		JSONObject rigidBodyObj;
		if (obj.SetObjectChecked("rigid body", rigidBodyObj))
		{
			if (newEntity->GetCollisionShape() == nullptr)
			{
				Logger::LogError("Serialized object contains \"rigid body\" field but no collider: " + objectName);
			}
			else
			{
				real mass = rigidBodyObj.GetFloat("mass");
				bool bKinematic = rigidBodyObj.GetBool("kinematic");

				RigidBody* rigidBody = newEntity->SetRigidBody(new RigidBody());
				rigidBody->SetMass(mass);
				rigidBody->SetKinematic(bKinematic);
				rigidBody->SetStatic(newEntity->IsStatic());
			}
		}

		switch (entityType)
		{
		case GameObjectType::OBJECT:
		{
			// Nothing special to do
		} break;
		case GameObjectType::SKYBOX:
		{
			if (matID == InvalidMaterialID)
			{
				Logger::LogError("Failed to create skybox material from serialized values! Can't create skybox.");
			}
			else
			{
				Material& material = gameContext.renderer->GetMaterial(matID);
				if (!material.generateHDRCubemapSampler &&
					!material.generateCubemapSampler)
				{
					Logger::LogWarning("Invalid skybox material! Must generate cubemap sampler");
				}

				MeshComponent* skyboxMesh = new MeshComponent(matID, newEntity);
				skyboxMesh->LoadPrefabShape(gameContext, MeshComponent::PrefabShape::SKYBOX);
				newEntity->SetMeshComponent(skyboxMesh);

				gameContext.renderer->SetSkyboxMesh(newEntity);

				glm::vec3 skyboxRotEuler;
				if (obj.SetVec3Checked("rotation", skyboxRotEuler))
				{
					glm::quat skyboxRotation = glm::quat(skyboxRotEuler);
					newEntity->GetTransform()->SetGlobalRotation(skyboxRotation);
				}
			}
		} break;
		case GameObjectType::REFLECTION_PROBE:
		{
			// Chrome ball material
			MaterialCreateInfo reflectionProbeMaterialCreateInfo = {};
			reflectionProbeMaterialCreateInfo.name = "Reflection probe sphere";
			reflectionProbeMaterialCreateInfo.shaderName = "pbr";
			reflectionProbeMaterialCreateInfo.constAlbedo = glm::vec3(0.75f, 0.75f, 0.75f);
			reflectionProbeMaterialCreateInfo.constMetallic = 1.0f;
			reflectionProbeMaterialCreateInfo.constRoughness = 0.0f;
			reflectionProbeMaterialCreateInfo.constAO = 1.0f;
			MaterialID reflectionProbeMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &reflectionProbeMaterialCreateInfo);

			// Probe capture material
			MaterialCreateInfo probeCaptureMatCreateInfo = {};
			probeCaptureMatCreateInfo.name = "Reflection probe capture";
			probeCaptureMatCreateInfo.shaderName = "deferred_combine_cubemap";
			probeCaptureMatCreateInfo.generateReflectionProbeMaps = true;
			probeCaptureMatCreateInfo.generateHDRCubemapSampler = true;
			probeCaptureMatCreateInfo.generatedCubemapSize = glm::vec2(512.0f, 512.0f); // TODO: Add support for non-512.0f size
			probeCaptureMatCreateInfo.generateCubemapDepthBuffers = true;
			probeCaptureMatCreateInfo.enableIrradianceSampler = true;
			probeCaptureMatCreateInfo.generateIrradianceSampler = true;
			probeCaptureMatCreateInfo.generatedIrradianceCubemapSize = { 32, 32 };
			probeCaptureMatCreateInfo.enablePrefilteredMap = true;
			probeCaptureMatCreateInfo.generatePrefilteredMap = true;
			probeCaptureMatCreateInfo.generatedPrefilteredCubemapSize = { 128, 128 };
			probeCaptureMatCreateInfo.enableBRDFLUT = true;
			probeCaptureMatCreateInfo.frameBuffers = {
				{ "positionMetallicFrameBufferSampler", nullptr },
				{ "normalRoughnessFrameBufferSampler", nullptr },
				{ "albedoAOFrameBufferSampler", nullptr },
			};
			MaterialID captureMatID = gameContext.renderer->InitializeMaterial(gameContext, &probeCaptureMatCreateInfo);


			MeshComponent* sphereMesh = new MeshComponent(reflectionProbeMaterialID, newEntity);
			MeshComponent::ImportSettings importSettings = {};
			importSettings.swapNormalYZ = true;
			importSettings.flipNormalZ = true;
			sphereMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/ico-sphere.gltf", &importSettings);
			newEntity->SetMeshComponent(sphereMesh);
			//m_Transform.Scale(1.5f);
			// Reflection probes get created at bootup
			//SetSerializable(false);

			//if (!m_StartVisible)
			//{
			//	SetVisible(m_StartVisible);
			//}

			std::string captureName = objectName + "_capture";
			GameObject* captureObject = new GameObject(captureName, GameObjectType::NONE);
			captureObject->SetVisible(false);

			RenderObjectCreateInfo captureObjectCreateInfo = {};
			captureObjectCreateInfo.vertexBufferData = nullptr;
			captureObjectCreateInfo.materialID = captureMatID;
			captureObjectCreateInfo.gameObject = captureObject;
			captureObjectCreateInfo.visibleInSceneExplorer = false;

			RenderID captureRenderID = gameContext.renderer->InitializeRenderObject(gameContext, &captureObjectCreateInfo);
			captureObject->SetRenderID(captureRenderID);

			newEntity->AddChild(captureObject);

			gameContext.renderer->SetReflectionProbeMaterial(captureMatID);
		} break;
		case GameObjectType::VALVE:
		{
			MeshComponent* valveMesh = new MeshComponent(matID, newEntity);
			valveMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/valve.gltf");
			newEntity->SetMeshComponent(valveMesh);

			btVector3 btHalfExtents(1.5f, 1.0f, 1.5f);
			btCylinderShape* cylinderShape = new btCylinderShape(btHalfExtents);

			newEntity->SetCollisionShape(cylinderShape);

			RigidBody* rigidBody = newEntity->SetRigidBody(new RigidBody());
			rigidBody->SetMass(1.0f);
			rigidBody->SetKinematic(false);
			rigidBody->SetStatic(false);
		} break;
		case GameObjectType::RISING_BLOCK:
		{
			MeshComponent* cubeMesh = new MeshComponent(matID, newEntity);
			cubeMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/cube.gltf");
			newEntity->SetMeshComponent(cubeMesh);

			btVector3 btHalfExtents(1.0f, 1.0f, 1.0f);
			btBoxShape* boxShape = new btBoxShape(btHalfExtents);

			newEntity->SetCollisionShape(boxShape);

			RigidBody* rigidBody = newEntity->SetRigidBody(new RigidBody());
			rigidBody->SetMass(1.0f);
			rigidBody->SetKinematic(true);
			rigidBody->SetStatic(false);

			JSONObject blockInfo = obj.GetObject("block info");
			std::string valveName = blockInfo.GetString("valve name");
			GameObject* valve;
			for (GameObject* child : m_Children)
			{
				if (child->m_Name.compare(valveName) == 0)
				{
					newEntity->m_RisingBlockMembers.valve = child;
					break;
				}
			}

			if (!newEntity->m_RisingBlockMembers.valve)
			{
				Logger::LogError("Rising block contains invalid valve name! Has it been created yet? " + valveName);
			}

			newEntity->m_RisingBlockMembers.maxHeight = blockInfo.GetFloat("max height");
			newEntity->m_RisingBlockMembers.riseSpeed = blockInfo.GetFloat("rise speed");

		} break;
		case GameObjectType::NONE:
		{
			// Assume this object is an empty parent object which holds no data itself but a transform
		} break;
		default:
		{
			//Logger::LogError("Unhandled entity type encountered when parsing scene file: " + entityTypeStr);
		} break;
		}

		if (newEntity)
		{
			newEntity->SetVisible(bVisible, false);
			newEntity->SetVisibleInSceneExplorer(bVisibleInSceneGraph);
			newEntity->m_Transform = transform;

			bool bStatic = false;
			if (obj.SetBoolChecked("static", bStatic))
			{
				newEntity->SetStatic(bStatic);
			}

			if (obj.HasField("children"))
			{
				std::vector<JSONObject> children = obj.GetObjectArray("children");
				for (const auto& child : children)
				{
					newEntity->AddChild(CreateEntityFromJSON(gameContext, child));
				}
			}
		}

		return newEntity;
	}

	void BaseScene::ParseMaterialJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut)
	{
		material.SetStringChecked("name", createInfoOut.name);
		material.SetStringChecked("shader", createInfoOut.shaderName);

		struct FilePathMaterialParam
		{
			std::string* member;
			std::string name;
		};

		std::vector<FilePathMaterialParam> filePathParams =
		{
			{ &createInfoOut.diffuseTexturePath, "diffuse texture filepath" },
			{ &createInfoOut.normalTexturePath, "normal texture filepath" },
			{ &createInfoOut.albedoTexturePath, "albedo texture filepath" },
			{ &createInfoOut.metallicTexturePath, "metallic texture filepath" },
			{ &createInfoOut.roughnessTexturePath, "roughness texture filepath" },
			{ &createInfoOut.aoTexturePath, "ao texture filepath" },
			{ &createInfoOut.hdrEquirectangularTexturePath, "hdr equirectangular texture filepath" },
			{ &createInfoOut.environmentMapPath, "environment map path" },
		};

		for (u32 i = 0; i < filePathParams.size(); ++i)
		{
			if (material.HasField(filePathParams[i].name))
			{
				*filePathParams[i].member = RESOURCE_LOCATION +
					material.GetString(filePathParams[i].name);
			}
		}

		material.SetBoolChecked("generate diffuse sampler", createInfoOut.generateDiffuseSampler);
		material.SetBoolChecked("enable diffuse sampler", createInfoOut.enableDiffuseSampler);
		material.SetBoolChecked("generate normal sampler", createInfoOut.generateNormalSampler);
		material.SetBoolChecked("enable normal sampler", createInfoOut.enableNormalSampler);
		material.SetBoolChecked("generate albedo sampler", createInfoOut.generateAlbedoSampler);
		material.SetBoolChecked("enable albedo sampler", createInfoOut.enableAlbedoSampler);
		material.SetBoolChecked("generate metallic sampler", createInfoOut.generateMetallicSampler);
		material.SetBoolChecked("enable metallic sampler", createInfoOut.enableMetallicSampler);
		material.SetBoolChecked("generate roughness sampler", createInfoOut.generateRoughnessSampler);
		material.SetBoolChecked("enable roughness sampler", createInfoOut.enableRoughnessSampler);
		material.SetBoolChecked("generate ao sampler", createInfoOut.generateAOSampler);
		material.SetBoolChecked("enable ao sampler", createInfoOut.enableAOSampler);
		material.SetBoolChecked("generate hdr equirectangular sampler", createInfoOut.generateHDREquirectangularSampler);
		material.SetBoolChecked("enable hdr equirectangular sampler", createInfoOut.enableHDREquirectangularSampler);
		material.SetBoolChecked("generate hdr cubemap sampler", createInfoOut.generateHDRCubemapSampler);
		material.SetBoolChecked("enable irradiance sampler", createInfoOut.enableIrradianceSampler);
		material.SetBoolChecked("generate irradiance sampler", createInfoOut.generateIrradianceSampler);
		material.SetBoolChecked("enable brdf lut", createInfoOut.enableBRDFLUT);
		material.SetBoolChecked("render to cubemap", createInfoOut.renderToCubemap);
		material.SetBoolChecked("enable cubemap sampler", createInfoOut.enableCubemapSampler);
		material.SetBoolChecked("enable cubemap trilinear filtering", createInfoOut.enableCubemapTrilinearFiltering);
		material.SetBoolChecked("generate cubemap sampler", createInfoOut.generateCubemapSampler);
		material.SetBoolChecked("generate cubemap depth buffers", createInfoOut.generateCubemapDepthBuffers);
		material.SetBoolChecked("generate prefiltered map", createInfoOut.generatePrefilteredMap);
		material.SetBoolChecked("enable prefiltered map", createInfoOut.enablePrefilteredMap);
		material.SetBoolChecked("generate reflection probe maps", createInfoOut.generateReflectionProbeMaps);

		material.SetVec2Checked("generated irradiance cubemap size", createInfoOut.generatedIrradianceCubemapSize);
		material.SetVec2Checked("generated prefiltered map size", createInfoOut.generatedPrefilteredCubemapSize);
		material.SetVec2Checked("generated cubemap size", createInfoOut.generatedCubemapSize);
		material.SetVec4Checked("color multiplier", createInfoOut.colorMultiplier);
		material.SetVec3Checked("const albedo", createInfoOut.constAlbedo);
		material.SetFloatChecked("const metallic", createInfoOut.constMetallic);
		material.SetFloatChecked("const roughness", createInfoOut.constRoughness);
		material.SetFloatChecked("const ao", createInfoOut.constAO);
	}

	i32 BaseScene::GetMaterialIndex(const Material& material, const GameContext& gameContext)
	{
		i32 materialIndex = -1;
		for (i32 j = 0; j < m_LoadedMaterials.size(); ++j)
		{
			Material& loadedMat = gameContext.renderer->GetMaterial(m_LoadedMaterials[j]);
			if (loadedMat.Equals(material, gameContext))
			{
				materialIndex = j;
				break;
			}
		}
		return materialIndex;
	}

	void BaseScene::CreatePointLightFromJSON(const GameContext& gameContext, const JSONObject& obj, PointLight& pointLight)
	{
		std::string posStr = obj.GetString("position");
		pointLight.position = glm::vec4(ParseVec3(posStr), 0);

		obj.SetVec4Checked("color", pointLight.color);

		obj.SetFloatChecked("brightness", pointLight.brightness);

		if (obj.HasField("enabled"))
		{
			pointLight.enabled = obj.GetBool("enabled") ? 1 : 0;
		}

		obj.SetStringChecked("brightness", pointLight.name);

		obj.SetStringChecked("name", pointLight.name);
	}

	void BaseScene::CreateDirectionalLightFromJSON(const GameContext& gameContext, const JSONObject& obj, DirectionalLight& directionalLight)
	{
		std::string dirStr = obj.GetString("direction");
		directionalLight.direction = glm::vec4(ParseVec3(dirStr), 0);

		obj.SetVec4Checked("color", directionalLight.color);

		obj.SetFloatChecked("brightness", directionalLight.brightness);

		if (obj.HasField("enabled"))
		{
			directionalLight.enabled = obj.GetBool("enabled") ? 1 : 0;
		}
	}

	void BaseScene::SerializeToFile(const GameContext& gameContext)
	{
		bool success = false;

		JSONObject rootSceneObject = {};

		i32 fileVersion = 1;

		rootSceneObject.fields.push_back(JSONField("version", JSONValue(fileVersion)));
		rootSceneObject.fields.push_back(JSONField("name", JSONValue(m_Name)));


		JSONField materialsField = {};
		materialsField.label = "materials";
		std::vector<JSONObject> materialsArray;
		for (MaterialID matID : m_LoadedMaterials)
		{
			Material& material = gameContext.renderer->GetMaterial(matID);
			materialsArray.push_back(SerializeMaterial(material, gameContext));
		}
		materialsField.value = JSONValue(materialsArray);
		rootSceneObject.fields.push_back(materialsField);


		JSONField entitiesField = {};
		entitiesField.label = "entities";
		std::vector<JSONObject> entitiesArray;
		for (GameObject* child : m_Children)
		{
			if (child->IsSerializable())
			{
				entitiesArray.push_back(SerializeObject(child, gameContext));
			}
		}
		entitiesField.value = JSONValue(entitiesArray);
		rootSceneObject.fields.push_back(entitiesField);


		JSONField pointLightsField = {};
		pointLightsField.label = "point lights";
		std::vector<JSONObject> pointLightsArray;
		for (i32 i = 0; i < gameContext.renderer->GetNumPointLights(); ++i)
		{
			PointLight& pointLight = gameContext.renderer->GetPointLight(i);
			pointLightsArray.push_back(SerializePointLight(pointLight, gameContext));
		}
		pointLightsField.value = JSONValue(pointLightsArray);
		rootSceneObject.fields.push_back(pointLightsField);

		DirectionalLight& dirLight = gameContext.renderer->GetDirectionalLight();
		JSONField directionalLightsField("directional light",
			JSONValue(SerializeDirectionalLight(dirLight, gameContext)));
		rootSceneObject.fields.push_back(directionalLightsField);


		std::string fileContents = rootSceneObject.Print(0);

		std::ofstream fileStream;
		fileStream.open(m_JSONFilePath, std::ofstream::out | std::ofstream::trunc);

		std::string cleanFileName = m_JSONFilePath.substr(RESOURCE_LOCATION.length());
		Logger::LogInfo("Serializing scene to " + cleanFileName);

		if (fileStream.is_open())
		{
			fileStream.write(fileContents.c_str(), fileContents.size());
			success = true;
		}
		else
		{
			Logger::LogError("Failed to open file for writing: " + m_JSONFilePath + ", Can't serialize scene");
			success = false;
		}

		Logger::LogInfo("Done serializing scene");

		fileStream.close();

		if (success)
		{
			AudioManager::PlaySource(blip);
		}
		else
		{
			AudioManager::PlaySource(dud_dud_dud_dud);
		}
	}

	JSONObject BaseScene::SerializeObject(GameObject* gameObject, const GameContext& gameContext)
	{
		if (!gameObject->IsSerializable())
		{
			Logger::LogError("Attempted to serialize non-serializable object");
			return{};
		}

		JSONObject object;
		std::string childName = gameObject->m_Name;

		object.fields.push_back(JSONField("name", JSONValue(childName)));
		object.fields.push_back(JSONField("type", JSONValue(GameObjectTypeToString(gameObject->m_Type))));
		object.fields.push_back(JSONField("visible", JSONValue(gameObject->IsVisible())));
		// TODO: Only save/read this value when editor is included in build
		if (!gameObject->IsVisibleInSceneExplorer())
		{
			object.fields.push_back(JSONField("visible in scene graph", 
				JSONValue(gameObject->IsVisibleInSceneExplorer())));
		}

		if (gameObject->IsStatic())
		{
			object.fields.push_back(JSONField("static", JSONValue(true)));
		}
		
		JSONField transformField;
		if (JSONParser::SerializeTransform(gameObject->GetTransform(), transformField))
		{
			object.fields.push_back(transformField);
		}

		MeshComponent* meshComponent = gameObject->GetMeshComponent();
		if (meshComponent)
		{
			JSONField meshField = {};
			meshField.label = "mesh";

			JSONObject meshValue = {};

			MeshComponent::Type meshType = meshComponent->GetType();
			if (meshType == MeshComponent::Type::FILE)
			{
				std::string meshFilepath = meshComponent->GetFilepath().substr(RESOURCE_LOCATION.length());
				meshValue.fields.push_back(JSONField("file", JSONValue(meshFilepath)));
			}
			// TODO: CLEANUP: Remove "prefab" meshes entirely (always load from file)
			else if (meshType == MeshComponent::Type::PREFAB)
			{
				std::string prefabShapeStr = MeshComponent::PrefabShapeToString(meshComponent->GetShape());
				meshValue.fields.push_back(JSONField("prefab", JSONValue(prefabShapeStr)));
			}
			else
			{
				Logger::LogError("Unhandled mesh prefab type when attempting to serialize scene!");
			}

			MeshComponent::ImportSettings importSettings = meshComponent->GetImportSettings();
			meshValue.fields.push_back(JSONField("swapNormalYZ", JSONValue(importSettings.swapNormalYZ)));
			meshValue.fields.push_back(JSONField("flipNormalZ", JSONValue(importSettings.flipNormalZ)));
			meshValue.fields.push_back(JSONField("flipU", JSONValue(importSettings.flipU)));
			meshValue.fields.push_back(JSONField("flipV", JSONValue(importSettings.flipV)));

			meshField.value = JSONValue(meshValue);
			object.fields.push_back(meshField);
		}

		{
			MaterialID matID = InvalidMaterialID;
			RenderObjectCreateInfo renderObjectCreateInfo;
			if (meshComponent)
			{
				matID = meshComponent->GetMaterialID();
			}
			else if (gameContext.renderer->GetRenderObjectCreateInfo(gameObject->GetRenderID(), renderObjectCreateInfo))
			{
				matID = renderObjectCreateInfo.materialID;
			}

			if (matID != InvalidMaterialID)
			{
				const Material& material = gameContext.renderer->GetMaterial(matID);
				i32 materialIndex = GetMaterialIndex(material, gameContext);
				if (materialIndex == -1)
				{
					Logger::LogError("Mesh object contains material not present in BaseScene::m_LoadedMaterials! Parsing this file will fail! (" + childName + ")");
				}
				else
				{
					object.fields.push_back(JSONField("material index", JSONValue(materialIndex)));
				}
			}
		}

		btCollisionShape* collisionShape = gameObject->GetCollisionShape();
		if (collisionShape)
		{
			JSONObject colliderObj;

			int shapeType = collisionShape->getShapeType();
			std::string shapeTypeStr = CollisionShapeTypeToString(shapeType);

			colliderObj.fields.push_back(JSONField("shape", JSONValue(shapeTypeStr)));

			switch (shapeType)
			{
			case BOX_SHAPE_PROXYTYPE:
			{
				btVector3 btHalfExtents = ((btBoxShape*)collisionShape)->getHalfExtentsWithMargin();
				glm::vec3 halfExtents = BtVec3ToVec3(btHalfExtents);
				std::string halfExtentsStr = Vec3ToString(halfExtents);
				colliderObj.fields.push_back(JSONField("half extents", JSONValue(halfExtentsStr)));
			} break;
			case SPHERE_SHAPE_PROXYTYPE:
			{
				real radius = ((btSphereShape*)collisionShape)->getRadius();
				colliderObj.fields.push_back(JSONField("radius", JSONValue(radius)));
			} break;
			case CAPSULE_SHAPE_PROXYTYPE:
			{
				real radius = ((btCapsuleShape*)collisionShape)->getRadius();
				real height = ((btCapsuleShape*)collisionShape)->getHalfHeight(); // TODO: Double?
				colliderObj.fields.push_back(JSONField("radius", JSONValue(radius)));
				colliderObj.fields.push_back(JSONField("height", JSONValue(height)));
			} break;
			case CONE_SHAPE_PROXYTYPE:
			{
				real radius = ((btConeShape*)collisionShape)->getRadius();
				real height = ((btConeShape*)collisionShape)->getHeight();
				colliderObj.fields.push_back(JSONField("radius", JSONValue(radius)));
				colliderObj.fields.push_back(JSONField("height", JSONValue(height)));
			} break;
			case CYLINDER_SHAPE_PROXYTYPE:
			{
				btVector3 btHalfExtents = ((btCylinderShape*)collisionShape)->getHalfExtentsWithMargin();
				glm::vec3 halfExtents = BtVec3ToVec3(btHalfExtents);
				std::string halfExtentsStr = Vec3ToString(halfExtents);
				colliderObj.fields.push_back(JSONField("half extents", JSONValue(halfExtentsStr)));
			} break;
			default:
			{
				Logger::LogError("Unhandled BroadphaseNativeType: " + std::to_string(shapeType));
			} break;
			}

			//bool bTrigger = false;
			//colliderObj.fields.push_back(JSONField("trigger", JSONValue(bTrigger)));
			// TODO: Handle triggers

			object.fields.push_back(JSONField("collider", JSONValue(colliderObj)));
		}

		RigidBody* rigidBody = gameObject->GetRigidBody();
		if (rigidBody)
		{
			JSONObject rigidBodyObj;

			if (collisionShape == nullptr)
			{
				Logger::LogError("Can't serialize object which has a rigid body but no collider! (" + gameObject->GetName() + ")");
			}
			else
			{
				real mass = rigidBody->GetMass();
				bool bKinematic = rigidBody->IsKinematic();
				bool bStatic = rigidBody->IsStatic();

				rigidBodyObj.fields.push_back(JSONField("mass", JSONValue(mass)));
				rigidBodyObj.fields.push_back(JSONField("kinematic", JSONValue(bKinematic)));
				rigidBodyObj.fields.push_back(JSONField("static", JSONValue(bStatic)));
			}

			object.fields.push_back(JSONField("rigid body", JSONValue(rigidBodyObj)));
		}

		// Type-specific data
		switch (gameObject->m_Type)
		{
		case GameObjectType::RISING_BLOCK:
		{
			std::vector<JSONField> fields;

			fields.push_back(JSONField("valve name", JSONValue(gameObject->m_RisingBlockMembers.valve->GetName())));
			fields.push_back(JSONField("max height", JSONValue(gameObject->m_RisingBlockMembers.maxHeight)));
			fields.push_back(JSONField("rise speed", JSONValue(gameObject->m_RisingBlockMembers.riseSpeed)));

			object.fields.push_back(JSONField("block info", JSONValue(fields)));
		} break;
		}


		const std::vector<GameObject*>& gameObjectChildren = gameObject->GetChildren();
		if (!gameObjectChildren.empty())
		{
			JSONField childrenField = {};
			childrenField.label = "children";

			std::vector<JSONObject> children;

			for (GameObject* child : gameObjectChildren)
			{
				if (child->IsSerializable())
				{
					children.push_back(SerializeObject(child, gameContext));
				}
			}

			// It's possible that all children are non-serializable
			if (!children.empty())
			{
				childrenField.value = JSONValue(children);
				object.fields.push_back(childrenField);
			}
		}

		return object;
	}

	JSONObject BaseScene::SerializeMaterial(const Material& material, const GameContext& gameContext)
	{
		JSONObject materialObject = {};

		materialObject.fields.push_back(JSONField("name", JSONValue(material.name)));

		const Shader& shader = gameContext.renderer->GetShader(material.shaderID);
		materialObject.fields.push_back(JSONField("shader", JSONValue(shader.name)));

		// TODO: Find out way of determining if the following four  values
		// are used by the shader (only currently used by PBR I think)
		std::string constAlbedoStr = Vec3ToString(material.constAlbedo);
		materialObject.fields.push_back(JSONField("const albedo",
			JSONValue(constAlbedoStr)));
		materialObject.fields.push_back(JSONField("const metallic",
			JSONValue(material.constMetallic)));
		materialObject.fields.push_back(JSONField("const roughness",
			JSONValue(material.constRoughness)));
		materialObject.fields.push_back(JSONField("const ao",
			JSONValue(material.constAO)));

		static const bool defaultEnableAlbedo = true;
		if (shader.needAlbedoSampler && material.enableAlbedoSampler != defaultEnableAlbedo)
		{
			std::string constAlbedoStr = Vec4ToString(material.constAlbedo);
			materialObject.fields.push_back(JSONField("enable albedo sampler",
				JSONValue(material.enableAlbedoSampler)));
		}

		static const bool defaultEnableMetallicSampler = true;
		if (shader.needMetallicSampler && material.enableMetallicSampler != defaultEnableMetallicSampler)
		{
			materialObject.fields.push_back(JSONField("enable metallic sampler",
				JSONValue(material.enableMetallicSampler)));
		}

		static const bool defaultEnableRoughness = true;
		if (shader.needRoughnessSampler && material.enableRoughnessSampler != defaultEnableRoughness)
		{
			materialObject.fields.push_back(JSONField("enable roughness sampler",
				JSONValue(material.enableRoughnessSampler)));
		}

		static const bool defaultEnableAO = true;
		if (shader.needAOSampler && material.enableAOSampler != defaultEnableAO)
		{
			materialObject.fields.push_back(JSONField("enable ao sampler",
				JSONValue(material.enableAOSampler)));
		}

		if (shader.needAlbedoSampler && !material.albedoTexturePath.empty())
		{
			std::string albedoTexturePath = material.albedoTexturePath.substr(RESOURCE_LOCATION.length());
			materialObject.fields.push_back(JSONField("albedo texture filepath",
				JSONValue(albedoTexturePath)));
		}

		if (shader.needMetallicSampler && !material.metallicTexturePath.empty())
		{
			std::string metallicTexturePath = material.metallicTexturePath.substr(RESOURCE_LOCATION.length());
			materialObject.fields.push_back(JSONField("metallic texture filepath",
				JSONValue(metallicTexturePath)));
		}

		if (shader.needRoughnessSampler && !material.roughnessTexturePath.empty())
		{
			std::string roughnessTexturePath = material.roughnessTexturePath.substr(RESOURCE_LOCATION.length());
			materialObject.fields.push_back(JSONField("roughness texture filepath",
				JSONValue(roughnessTexturePath)));
		}

		if (shader.needAOSampler && !material.aoTexturePath.empty())
		{
			std::string aoTexturePath = material.aoTexturePath.substr(RESOURCE_LOCATION.length());
			materialObject.fields.push_back(JSONField("ao texture filepath",
				JSONValue(aoTexturePath)));
		}

		if (material.generateHDRCubemapSampler)
		{
			materialObject.fields.push_back(JSONField("generate hdr cubemap sampler",
				JSONValue(material.generateHDRCubemapSampler)));
		}

		if (shader.needCubemapSampler)
		{
			materialObject.fields.push_back(JSONField("enable cubemap sampler",
				JSONValue(material.enableCubemapSampler)));

			materialObject.fields.push_back(JSONField("enable cubemap trilinear filtering",
				JSONValue(material.enableCubemapTrilinearFiltering)));

			std::string cubemapSamplerSizeStr = Vec2ToString(material.cubemapSamplerSize);
			materialObject.fields.push_back(JSONField("generated cubemap size",
				JSONValue(cubemapSamplerSizeStr)));
		}

		if (shader.needIrradianceSampler || material.irradianceSamplerSize.x > 0)
		{
			materialObject.fields.push_back(JSONField("generate irradiance sampler",
				JSONValue(material.generateIrradianceSampler)));

			std::string irradianceSamplerSizeStr = Vec2ToString(material.irradianceSamplerSize);
			materialObject.fields.push_back(JSONField("generated irradiance cubemap size",
				JSONValue(irradianceSamplerSizeStr)));
		}

		if (shader.needPrefilteredMap || material.prefilteredMapSize.x > 0)
		{
			materialObject.fields.push_back(JSONField("generate prefiltered map",
				JSONValue(material.generatePrefilteredMap)));

			std::string prefilteredMapSizeStr = Vec2ToString(material.prefilteredMapSize);
			materialObject.fields.push_back(JSONField("generated prefiltered map size",
				JSONValue(prefilteredMapSizeStr)));
		}

		if (!material.environmentMapPath.empty())
		{
			std::string cleanedEnvMapPath = material.environmentMapPath.substr(RESOURCE_LOCATION.length());
			materialObject.fields.push_back(JSONField("environment map path",
				JSONValue(cleanedEnvMapPath)));
		}

		return materialObject;
	}

	JSONObject BaseScene::SerializePointLight(PointLight& pointLight, const GameContext& gameContext)
	{
		JSONObject object;

		object.fields.push_back(JSONField("name", JSONValue(pointLight.name)));

		std::string posStr = Vec3ToString(pointLight.position);
		object.fields.push_back(JSONField("position", JSONValue(posStr)));

		std::string colorStr = Vec3ToString(pointLight.color);
		object.fields.push_back(JSONField("color", JSONValue(colorStr)));

		object.fields.push_back(JSONField("enabled", JSONValue(pointLight.enabled != 0)));
		object.fields.push_back(JSONField("brightness", JSONValue(pointLight.brightness)));

		return object;
	}

	JSONObject BaseScene::SerializeDirectionalLight(DirectionalLight& directionalLight, const GameContext& gameContext)
	{
		JSONObject object;

		std::string dirStr = Vec3ToString(directionalLight.direction);
		object.fields.push_back(JSONField("direction", JSONValue(dirStr)));

		std::string colorStr = Vec3ToString(directionalLight.color);
		object.fields.push_back(JSONField("color", JSONValue(colorStr)));

		object.fields.push_back(JSONField("enabled", JSONValue(directionalLight.enabled != 0)));
		object.fields.push_back(JSONField("brightness", JSONValue(directionalLight.brightness)));

		return object;
	}

	GameObject* BaseScene::AddChild(GameObject* gameObject)
	{
		if (!gameObject)
		{
			return nullptr;
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == gameObject)
			{
				Logger::LogWarning("Attempting to add child to scene again");
				return nullptr;
			}
		}

		m_Children.push_back(gameObject);

		return gameObject;
	}

	void BaseScene::RemoveChild(GameObject* gameObject, bool deleteChild)
	{
		auto iter = m_Children.begin();
		while (iter != m_Children.end())
		{
			if (*iter == gameObject)
			{
				if (deleteChild)
				{
					SafeDelete(*iter);
				}

				iter = m_Children.erase(iter);
				return;
			}
			else
			{
				++iter;
			}
		}

		Logger::LogWarning("Attempting to remove non-existent child from scene");
	}

	void BaseScene::RemoveAllChildren(bool deleteChildren)
	{
		auto iter = m_Children.begin();
		while (iter != m_Children.end())
		{
			if (deleteChildren)
			{
				delete *iter;
			}

			iter = m_Children.erase(iter);
		}
	}

	GameObject* BaseScene::FirstObjectWithTag(const std::string& tag)
	{
		for (GameObject* gameObject : m_Children)
		{
			GameObject* result = FindObjectWithTag(tag, gameObject);
			if (result)
			{
				return result;
			}
		}

		return nullptr;
	}

	GameObject* BaseScene::FindObjectWithTag(const std::string& tag, GameObject* gameObject)
	{
		if (gameObject->HasTag(tag))
		{
			return gameObject;
		}

		for (GameObject* child : gameObject->m_Children)
		{
			GameObject* result = FindObjectWithTag(tag, child);
			if (result)
			{
				return result;
			}
		}
	}

	std::vector<GameObject*>& BaseScene::GetRootObjects()
	{
		return m_Children;
	}

	void BaseScene::GetInteractibleObjects(std::vector<GameObject*>& interactibleObjects)
	{
		for (auto child : m_Children)
		{
			if (child->m_bInteractable)
			{
				interactibleObjects.push_back(child);
			}
		}
	}

	std::string BaseScene::GetName() const
	{
		return m_Name;
	}

	PhysicsWorld* BaseScene::GetPhysicsWorld()
	{
		return m_PhysicsWorld;
	}

} // namespace flex
