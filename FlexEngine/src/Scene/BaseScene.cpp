#include "stdafx.hpp"

#include "Scene/BaseScene.hpp"

#include <fstream>

#pragma warning(push, 0)
#include <glm/vec3.hpp>

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
#include "FlexEngine.hpp"
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

namespace flex
{
	BaseScene::BaseScene(const std::string& fileName) :
		m_FileName(fileName)
	{
	}

	BaseScene::~BaseScene()
	{
	}

	void BaseScene::Initialize(const GameContext& gameContext)
	{
		ParseFoundPrefabFiles();

		// Physics world
		m_PhysicsWorld = new PhysicsWorld();
		m_PhysicsWorld->Initialize(gameContext);

		m_PhysicsWorld->GetWorld()->setGravity({ 0.0f, -9.81f, 0.0f });

		// Use save file if exists, otherwise use default
		const std::string savedShortPath = "scenes/saved/" + m_FileName;
		const std::string defaultShortPath = "scenes/default/" + m_FileName;
		const std::string savedPath = RESOURCE_LOCATION + savedShortPath;
		const std::string defaultPath = RESOURCE_LOCATION + defaultShortPath;
		m_bUsingSaveFile = FileExists(savedPath);

		std::string shortFilePath;
		std::string filePath;
		if (m_bUsingSaveFile)
		{
			shortFilePath = savedShortPath;
			filePath = savedPath;
		}
		else
		{
			shortFilePath = defaultShortPath;
			filePath = defaultPath;
		}

		Logger::LogInfo("Loading scene from: " + savedShortPath);

		JSONObject sceneRootObject;
		if (!JSONParser::Parse(filePath, sceneRootObject))
		{
			Logger::LogError("Failed to parse scene file: " + savedShortPath);
			return;
		}

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
		for (u32 i = 0; i < materialsArray.size(); ++i)
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
			AddRootObject(CreateGameObjectFromJSON(gameContext, rootEntity));
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


		{
			const std::string gridMatName = "Grid";
			if (!gameContext.renderer->GetMaterialID(gridMatName, m_GridMaterialID))
			{
				MaterialCreateInfo gridMatInfo = {};
				gridMatInfo.shaderName = "color";
				gridMatInfo.name = gridMatName;
				m_GridMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &gridMatInfo);
			}

			m_Grid = new GameObject("Grid", GameObjectType::OBJECT);
			MeshComponent* gridMesh = m_Grid->SetMeshComponent(new MeshComponent(m_GridMaterialID, m_Grid));
			RenderObjectCreateInfo createInfo = {};
			createInfo.editorObject = true;
			gridMesh->LoadPrefabShape(gameContext, MeshComponent::PrefabShape::GRID, &createInfo);
			m_Grid->GetTransform()->Translate(0.0f, -0.1f, 0.0f);
			m_Grid->SetSerializable(false);
			m_Grid->SetStatic(true);
			AddRootObject(m_Grid);
		}


		{
			const std::string worldOriginMatName = "World origin";
			if (!gameContext.renderer->GetMaterialID(worldOriginMatName, m_WorldAxisMaterialID))
			{
				MaterialCreateInfo worldAxisMatInfo = {};
				worldAxisMatInfo.shaderName = "color";
				worldAxisMatInfo.name = worldOriginMatName;
				m_WorldAxisMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &worldAxisMatInfo);
			}

			m_WorldOrigin = new GameObject("World origin", GameObjectType::OBJECT);
			MeshComponent* orignMesh = m_WorldOrigin->SetMeshComponent(new MeshComponent(m_WorldAxisMaterialID, m_WorldOrigin));
			RenderObjectCreateInfo createInfo = {};
			createInfo.editorObject = true;
			orignMesh->LoadPrefabShape(gameContext, MeshComponent::PrefabShape::WORLD_AXIS_GROUND, &createInfo);
			m_WorldOrigin->GetTransform()->Translate(0.0f, -0.09f, 0.0f);
			m_WorldOrigin->SetSerializable(false);
			m_WorldOrigin->SetStatic(true);
			AddRootObject(m_WorldOrigin);
		}

		m_Player0 = new Player(0);
		AddRootObject(m_Player0);

		m_Player1 = new Player(1);
		AddRootObject(m_Player1);

		for (auto iter = m_RootObjects.begin(); iter != m_RootObjects.end(); ++iter)
		{
			(*iter)->Initialize(gameContext);
		}
	}

	void BaseScene::PostInitialize(const GameContext& gameContext)
	{
		for (auto iter = m_RootObjects.begin(); iter != m_RootObjects.end(); ++iter)
		{
			GameObject* gameObject = *iter;

			gameObject->PostInitialize(gameContext);
		}

		m_PhysicsWorld->GetWorld()->setDebugDrawer(gameContext.renderer->GetDebugDrawer());
	}

	void BaseScene::Destroy(const GameContext& gameContext)
	{
		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject)
			{
				rootObject->Destroy(gameContext);
				SafeDelete(rootObject);
			}
		}
		m_RootObjects.clear();

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
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}
		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_X))
		{
			AudioManager::PauseSource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}

		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_C))
		{
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::drmapan));
		}
		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_V))
		{
			AudioManager::PauseSource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::drmapan));
		}
		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_B))
		{
			AudioManager::StopSource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::drmapan));
		}

		if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_L))
		{
			AudioManager::AddToSourcePitch(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud),
										   0.5f * gameContext.deltaTime);
		}
		if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_K))
		{
			AudioManager::AddToSourcePitch(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud),
										   -0.5f * gameContext.deltaTime);
		}

		if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_P))
		{
			AudioManager::ScaleSourceGain(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud),
										  1.1f);
		}
		if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_O))
		{
			AudioManager::ScaleSourceGain(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud),
										  1.0f / 1.1f);
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

		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject)
			{
				rootObject->Update(gameContext);
			}
		}
	}

	GameObject* BaseScene::CreateGameObjectFromJSON(const GameContext& gameContext, const JSONObject& obj, MaterialID overriddenMatID /* = InvalidMaterialID */)
	{
		GameObject* newGameObject = nullptr;

		std::string entityTypeStr = obj.GetString("type");

		if (entityTypeStr.compare("prefab") == 0)
		{
			std::string prefabTypeStr = obj.GetString("prefab type");
			JSONObject* prefab = nullptr;

			for (JSONObject& parsedPrefab : m_ParsedPrefabs)
			{
				if (parsedPrefab.GetString("name").compare(prefabTypeStr) == 0)
				{
					prefab = &parsedPrefab;
				}
			}

			if (prefab == nullptr)
			{
				Logger::LogError("Invalid prefab type: " + prefabTypeStr);

				return nullptr;
			}
			else
			{
				std::string name = obj.GetString("name");

				MaterialID matID = ParseMatID(obj);

				GameObject* prefabInstance = CreateGameObjectFromJSON(gameContext, *prefab, matID);
				prefabInstance->m_bLoadedFromPrefab = true;
				prefabInstance->m_PrefabName = prefabInstance->m_Name;

				prefabInstance->m_Name = name;

				bool bVisible = true;
				obj.SetBoolChecked("visible", bVisible);
				prefabInstance->SetVisible(bVisible, false);

				JSONObject transformObj;
				if (obj.SetObjectChecked("transform", transformObj))
				{
					prefabInstance->m_Transform = JSONParser::ParseTransform(transformObj);
				}

				GameObjectType type = StringToGameObjectType(prefab->GetString("type"));

				ParseUniqueObjectFields(gameContext, obj, type, matID, prefabInstance);

				return prefabInstance;
			}
		}

		GameObjectType gameObjectType = StringToGameObjectType(entityTypeStr);

		std::string objectName = obj.GetString("name");

		bool bVisible = true;
		obj.SetBoolChecked("visible", bVisible);
		bool bVisibleInSceneGraph = true;
		obj.SetBoolChecked("visible in scene graph", bVisibleInSceneGraph);

		MaterialID matID = InvalidMaterialID;
		if (overriddenMatID != InvalidMaterialID)
		{
			matID = overriddenMatID;
		}
		else
		{
			matID = ParseMatID(obj);
		}

		newGameObject = new GameObject(objectName, gameObjectType);


		JSONObject transformObj;
		if (obj.SetObjectChecked("transform", transformObj))
		{
			newGameObject->m_Transform = JSONParser::ParseTransform(transformObj);
		}

		JSONObject meshObj;
		if (obj.SetObjectChecked("mesh", meshObj))
		{
			ParseMeshObject(gameContext, meshObj, newGameObject, matID);
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

				newGameObject->SetCollisionShape(boxShape);
			} break;
			case SPHERE_SHAPE_PROXYTYPE:
			{
				real radius = colliderObj.GetFloat("radius");
				btSphereShape* sphereShape = new btSphereShape(radius);

				newGameObject->SetCollisionShape(sphereShape);
			} break;
			case CAPSULE_SHAPE_PROXYTYPE:
			{
				real radius = colliderObj.GetFloat("radius");
				real height = colliderObj.GetFloat("height");
				btCapsuleShape* capsuleShape = new btCapsuleShape(radius, height);

				newGameObject->SetCollisionShape(capsuleShape);
			} break;
			case CONE_SHAPE_PROXYTYPE:
			{
				real radius = colliderObj.GetFloat("radius");
				real height = colliderObj.GetFloat("height");
				btConeShape* coneShape = new btConeShape(radius, height);

				newGameObject->SetCollisionShape(coneShape);
			} break;
			case CYLINDER_SHAPE_PROXYTYPE:
			{
				glm::vec3 halfExtents;
				colliderObj.SetVec3Checked("half extents", halfExtents);
				btVector3 btHalfExtents(halfExtents.x, halfExtents.y, halfExtents.z);
				btCylinderShape* cylinderShape = new btCylinderShape(btHalfExtents);

				newGameObject->SetCollisionShape(cylinderShape);
			} break;
			default:
			{
				Logger::LogError("Unhandled BroadphaseNativeType: " + shapeStr);
			} break;
			}

			//bool bIsTrigger = colliderObj.GetBool("trigger");
			// TODO: Create btGhostObject to use for trigger
		}

		JSONObject rigidBodyObj;
		if (obj.SetObjectChecked("rigid body", rigidBodyObj))
		{
			if (newGameObject->GetCollisionShape() == nullptr)
			{
				Logger::LogError("Serialized object contains \"rigid body\" field but no collider: " + objectName);
			}
			else
			{
				real mass = rigidBodyObj.GetFloat("mass");
				bool bKinematic = rigidBodyObj.GetBool("kinematic");

				RigidBody* rigidBody = newGameObject->SetRigidBody(new RigidBody());
				rigidBody->SetMass(mass);
				rigidBody->SetKinematic(bKinematic);
				rigidBody->SetStatic(newGameObject->IsStatic());
			}
		}

		VertexAttributes requiredVertexAttributes = 0;
		if (matID != InvalidMaterialID)
		{
			Material& material = gameContext.renderer->GetMaterial(matID);
			Shader& shader = gameContext.renderer->GetShader(material.shaderID);
			requiredVertexAttributes = shader.vertexAttributes;
		}

		ParseUniqueObjectFields(gameContext, obj, gameObjectType, matID, newGameObject);

		if (newGameObject)
		{
			newGameObject->SetVisible(bVisible, false);
			newGameObject->SetVisibleInSceneExplorer(bVisibleInSceneGraph);

			bool bStatic = false;
			if (obj.SetBoolChecked("static", bStatic))
			{
				newGameObject->SetStatic(bStatic);
			}

			if (obj.HasField("children"))
			{
				std::vector<JSONObject> children = obj.GetObjectArray("children");
				for (const auto& child : children)
				{
					newGameObject->AddChild(CreateGameObjectFromJSON(gameContext, child));
				}
			}
		}

		return newGameObject;
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
			{ &createInfoOut.albedoTexturePath, "albedo texture filepath" },
			{ &createInfoOut.metallicTexturePath, "metallic texture filepath" },
			{ &createInfoOut.roughnessTexturePath, "roughness texture filepath" },
			{ &createInfoOut.aoTexturePath, "ao texture filepath" },
			{ &createInfoOut.normalTexturePath, "normal texture filepath" },
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

		material.SetBoolChecked("generate albedo sampler", createInfoOut.generateAlbedoSampler);
		material.SetBoolChecked("enable albedo sampler", createInfoOut.enableAlbedoSampler);
		material.SetBoolChecked("generate metallic sampler", createInfoOut.generateMetallicSampler);
		material.SetBoolChecked("enable metallic sampler", createInfoOut.enableMetallicSampler);
		material.SetBoolChecked("generate roughness sampler", createInfoOut.generateRoughnessSampler);
		material.SetBoolChecked("enable roughness sampler", createInfoOut.enableRoughnessSampler);
		material.SetBoolChecked("generate ao sampler", createInfoOut.generateAOSampler);
		material.SetBoolChecked("enable ao sampler", createInfoOut.enableAOSampler);
		material.SetBoolChecked("generate normal sampler", createInfoOut.generateNormalSampler);
		material.SetBoolChecked("enable normal sampler", createInfoOut.enableNormalSampler);
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

	MeshComponent* BaseScene::ParseMeshObject(const GameContext& gameContext, const JSONObject& meshObject, GameObject* newGameObject, MaterialID matID)
	{
		MeshComponent* result = nullptr;

		std::string meshFilePath = meshObject.GetString("file");
		if (!meshFilePath.empty())
		{
			meshFilePath = RESOURCE_LOCATION + meshFilePath;
		}
		std::string meshPrefabName = meshObject.GetString("prefab");
		bool swapNormalYZ = meshObject.GetBool("swapNormalYZ");
		bool flipNormalZ = meshObject.GetBool("flipNormalZ");
		bool flipU = meshObject.GetBool("flipU");
		bool flipV = meshObject.GetBool("flipV");

		if (matID == InvalidMaterialID)
		{
			Logger::LogError("Mesh object requires material index: " + newGameObject->GetName());
		}
		else
		{
			Material& material = gameContext.renderer->GetMaterial(matID);
			Shader& shader = gameContext.renderer->GetShader(material.shaderID);
			VertexAttributes requiredVertexAttributes = shader.vertexAttributes;

			if (!meshFilePath.empty())
			{
				result = new MeshComponent(matID, newGameObject);
				result->SetRequiredAttributes(requiredVertexAttributes);

				MeshComponent::ImportSettings importSettings = {};
				importSettings.flipU = flipU;
				importSettings.flipV = flipV;
				importSettings.flipNormalZ = flipNormalZ;
				importSettings.swapNormalYZ = swapNormalYZ;

				result->LoadFromFile(gameContext,
								   meshFilePath,
								   &importSettings);

				newGameObject->SetMeshComponent(result);
			}
			else if (!meshPrefabName.empty())
			{
				result = new MeshComponent(matID, newGameObject);
				result->SetRequiredAttributes(requiredVertexAttributes);

				MeshComponent::PrefabShape prefabShape = MeshComponent::StringToPrefabShape(meshPrefabName);
				result->LoadPrefabShape(gameContext, prefabShape);

				newGameObject->SetMeshComponent(result);
			}
			else
			{
				Logger::LogError("Unhandled mesh field on object: " + newGameObject->GetName());
			}
		}

		return result;
	}

	i32 BaseScene::GetMaterialArrayIndex(const Material& material, const GameContext& gameContext)
	{
		i32 materialArrayIndex = -1;
		for (u32 j = 0; j < m_LoadedMaterials.size(); ++j)
		{
			Material& loadedMat = gameContext.renderer->GetMaterial(m_LoadedMaterials[j]);
			if (loadedMat.Equals(material))
			{
				materialArrayIndex = (i32)j;
				break;
			}
		}
		return materialArrayIndex;
	}

	void BaseScene::ParseFoundPrefabFiles()
	{
		m_ParsedPrefabs.clear();

		std::vector<std::string> foundFiles;
		if (FindFilesInDirectory(RESOURCE_LOCATION + "scenes/prefabs/", foundFiles, ".json"))
		{
			for (const std::string& foundFilePath : foundFiles)
			{
				std::string fileName = foundFilePath;
				StripLeadingDirectories(fileName);
				Logger::LogInfo("Parsing prefab: " + fileName);

				JSONObject obj;
				if (JSONParser::Parse(foundFilePath, obj))
				{
					m_ParsedPrefabs.push_back(obj);
				}
				else
				{
					Logger::LogError("Failed to parse prefab file: " + foundFilePath);
				}
			}
		}
		else
		{
			Logger::LogError("Failed to find files in \"scenes/prefabs/\" !");
		}
	}

	MaterialID BaseScene::ParseMatID(const JSONObject& object)
	{
		MaterialID matID = InvalidMaterialID;
		i32 materialArrayIndex = -1;
		if (object.SetIntChecked("material array index", materialArrayIndex))
		{
			if (materialArrayIndex >= 0 &&
				materialArrayIndex < (i32)m_LoadedMaterials.size())
			{
				matID = m_LoadedMaterials[materialArrayIndex];
			}
			else
			{
				matID = InvalidMaterialID;
				Logger::LogError("Invalid material index for entity " + object.GetString("name") + ": " +
								 std::to_string(materialArrayIndex));
			}
		}
		return matID;
	}

	void BaseScene::ParseUniqueObjectFields(const GameContext& gameContext, const JSONObject& object, GameObjectType type, MaterialID matID, GameObject* newGameObject)
	{
		Material& material = gameContext.renderer->GetMaterial(matID);
		Shader& shader = gameContext.renderer->GetShader(material.shaderID);
		VertexAttributes requiredVertexAttributes = shader.vertexAttributes;

		switch (type)
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
				if (!material.generateHDRCubemapSampler &&
					!material.generateCubemapSampler)
				{
					Logger::LogWarning("Invalid skybox material! Material must be set to generate cubemap sampler");
				}

				MeshComponent* skyboxMesh = new MeshComponent(matID, newGameObject);
				skyboxMesh->SetRequiredAttributes(requiredVertexAttributes);
				skyboxMesh->LoadPrefabShape(gameContext, MeshComponent::PrefabShape::SKYBOX);
				assert(newGameObject->GetMeshComponent() == nullptr);
				newGameObject->SetMeshComponent(skyboxMesh);

				gameContext.renderer->SetSkyboxMesh(newGameObject);

				glm::vec3 skyboxRotEuler;
				if (object.SetVec3Checked("rotation", skyboxRotEuler))
				{
					glm::quat skyboxRotation = glm::quat(skyboxRotEuler);
					newGameObject->GetTransform()->SetWorldRotation(skyboxRotation);
				}
			}
		} break;
		case GameObjectType::REFLECTION_PROBE:
		{
			// Chrome ball material
			//MaterialCreateInfo reflectionProbeMaterialCreateInfo = {};
			//reflectionProbeMaterialCreateInfo.name = "Reflection probe sphere";
			//reflectionProbeMaterialCreateInfo.shaderName = "pbr";
			//reflectionProbeMaterialCreateInfo.constAlbedo = glm::vec3(0.75f, 0.75f, 0.75f);
			//reflectionProbeMaterialCreateInfo.constMetallic = 1.0f;
			//reflectionProbeMaterialCreateInfo.constRoughness = 0.0f;
			//reflectionProbeMaterialCreateInfo.constAO = 1.0f;
			//MaterialID reflectionProbeMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &reflectionProbeMaterialCreateInfo);

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

			MeshComponent* sphereMesh = new MeshComponent(matID, newGameObject);
			sphereMesh->SetRequiredAttributes(requiredVertexAttributes);

			assert(newGameObject->GetMeshComponent() == nullptr);
			sphereMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/ico-sphere.gltf");
			newGameObject->SetMeshComponent(sphereMesh);

			std::string captureName = newGameObject->m_Name + "_capture";
			GameObject* captureObject = new GameObject(captureName, GameObjectType::NONE);
			captureObject->SetSerializable(false);
			captureObject->SetVisible(false);

			RenderObjectCreateInfo captureObjectCreateInfo = {};
			captureObjectCreateInfo.vertexBufferData = nullptr;
			captureObjectCreateInfo.materialID = captureMatID;
			captureObjectCreateInfo.gameObject = captureObject;
			captureObjectCreateInfo.visibleInSceneExplorer = false;

			RenderID captureRenderID = gameContext.renderer->InitializeRenderObject(gameContext, &captureObjectCreateInfo);
			captureObject->SetRenderID(captureRenderID);

			newGameObject->AddChild(captureObject);

			newGameObject->m_ReflectionProbeMembers.captureMatID = captureMatID;

			gameContext.renderer->SetReflectionProbeMaterial(captureMatID);
		} break;
		case GameObjectType::VALVE:
		{
			JSONObject valveInfo;
			if (object.SetObjectChecked("valve info", valveInfo))
			{
				glm::vec2 valveRange;
				valveInfo.SetVec2Checked("range", valveRange);
				newGameObject->m_ValveMembers.minRotation = valveRange.x;
				newGameObject->m_ValveMembers.maxRotation = valveRange.y;
				if (glm::abs(newGameObject->m_ValveMembers.maxRotation - newGameObject->m_ValveMembers.minRotation) <= 0.0001f)
				{
					Logger::LogWarning("Valve's rotation range is 0, it will not be able to rotate!");
				}
				if (newGameObject->m_ValveMembers.minRotation > newGameObject->m_ValveMembers.maxRotation)
				{
					Logger::LogWarning("Valve's minimum rotation range is greater than its maximum! Undefined behavior");
				}
			}

			if (!newGameObject->m_MeshComponent)
			{
				MeshComponent* valveMesh = new MeshComponent(matID, newGameObject);
				valveMesh->SetRequiredAttributes(requiredVertexAttributes);
				valveMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/valve.gltf");
				assert(newGameObject->GetMeshComponent() == nullptr);
				newGameObject->SetMeshComponent(valveMesh);
			}

			if (!newGameObject->m_CollisionShape)
			{
				btVector3 btHalfExtents(1.5f, 1.0f, 1.5f);
				btCylinderShape* cylinderShape = new btCylinderShape(btHalfExtents);

				newGameObject->SetCollisionShape(cylinderShape);
			}

			if (!newGameObject->m_RigidBody)
			{
				RigidBody* rigidBody = newGameObject->SetRigidBody(new RigidBody());
				rigidBody->SetMass(1.0f);
				rigidBody->SetKinematic(false);
				rigidBody->SetStatic(false);
			}
		} break;
		case GameObjectType::RISING_BLOCK:
		{
			if (!newGameObject->m_MeshComponent)
			{
				MeshComponent* cubeMesh = new MeshComponent(matID, newGameObject);
				cubeMesh->SetRequiredAttributes(requiredVertexAttributes);
				cubeMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/cube.gltf");
				newGameObject->SetMeshComponent(cubeMesh);
			}

			if (!newGameObject->m_RigidBody)
			{
				RigidBody* rigidBody = newGameObject->SetRigidBody(new RigidBody());
				rigidBody->SetMass(1.0f);
				rigidBody->SetKinematic(true);
				rigidBody->SetStatic(false);
			}

			std::string valveName;
			JSONObject blockInfo;
			if (object.SetObjectChecked("block info", blockInfo))
			{
				valveName = blockInfo.GetString("valve name");

				if (valveName.empty())
				{
					Logger::LogWarning("Rising block field's valve name is empty!");
				}
				else
				{
					for (GameObject* rootObject : m_RootObjects)
					{
						if (rootObject->m_Name.compare(valveName) == 0)
						{
							newGameObject->m_RisingBlockMembers.valve = rootObject;
							break;
						}
					}
				}
			}
			else
			{
				Logger::LogWarning("Rising block \"block info\" field missing!");
			}

			blockInfo.SetBoolChecked("affected by gravity", newGameObject->m_RisingBlockMembers.bAffectedByGravity);

			if (!newGameObject->m_RisingBlockMembers.valve)
			{
				Logger::LogError("Rising block contains invalid valve name! Has it been created yet? " + valveName);
			}

			blockInfo.SetVec3Checked("move axis", newGameObject->m_RisingBlockMembers.moveAxis);

			if (newGameObject->m_RisingBlockMembers.moveAxis == glm::vec3(0.0f))
			{
				Logger::LogWarning("Rising block's move axis is not set! It won't be able to move");
			}
		} break;
		case GameObjectType::GLASS_WINDOW:
		{
			JSONObject windowInfo = object.GetObject("window info");

			bool bBroken = false;
			windowInfo.SetBoolChecked("broken", bBroken);

			if (!newGameObject->m_MeshComponent)
			{
				MeshComponent* windowMesh = new MeshComponent(matID, newGameObject);
				windowMesh->SetRequiredAttributes(requiredVertexAttributes);
				windowMesh->LoadFromFile(gameContext, RESOURCE_LOCATION +
					(bBroken ? "models/glass-window-broken.gltf" : "models/glass-window-whole.gltf"));
				newGameObject->SetMeshComponent(windowMesh);
			}

			if (!newGameObject->m_RigidBody)
			{
				RigidBody* rigidBody = newGameObject->SetRigidBody(new RigidBody());
				rigidBody->SetMass(1.0f);
				rigidBody->SetKinematic(true);
				rigidBody->SetStatic(false);
			}
		} break;
		}
	}

	void BaseScene::CreatePointLightFromJSON(const GameContext& gameContext, const JSONObject& obj, PointLight& pointLight)
	{
		UNREFERENCED_PARAMETER(gameContext);

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
		UNREFERENCED_PARAMETER(gameContext);

		std::string dirStr = obj.GetString("direction");
		directionalLight.direction = glm::vec4(ParseVec3(dirStr), 0);

		obj.SetVec4Checked("color", directionalLight.color);

		obj.SetFloatChecked("brightness", directionalLight.brightness);

		if (obj.HasField("enabled"))
		{
			directionalLight.enabled = obj.GetBool("enabled") ? 1 : 0;
		}
	}

	void BaseScene::SerializeToFile(const GameContext& gameContext, bool bSaveOverDefault /* = false */)
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
		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject->IsSerializable())
			{
				entitiesArray.push_back(SerializeObject(rootObject, gameContext));
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

		std::string shortSavedFileName;
		if (bSaveOverDefault)
		{
			shortSavedFileName = "scenes/default/" + m_FileName;
		}
		else
		{
			shortSavedFileName = "scenes/saved/" + m_FileName;
		}
		Logger::LogInfo("Serializing scene to " + shortSavedFileName);

		std::string savedFilePathName = RESOURCE_LOCATION + shortSavedFileName;
		savedFilePathName = RelativePathToAbsolute(savedFilePathName);
		success = WriteFile(savedFilePathName, fileContents, false);

		if (success)
		{
			Logger::LogInfo("Done serializing scene");
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::blip));
			m_bUsingSaveFile = true;
		}
		else
		{
			Logger::LogError("Failed to open file for writing: " + savedFilePathName + ", Can't serialize scene");
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}
	}

	JSONObject BaseScene::SerializeObject(GameObject* gameObject, const GameContext& gameContext)
	{
		if (!gameObject->IsSerializable())
		{
			Logger::LogError("Attempted to serialize non-serializable object");
			return{};
		}

		// Non-basic objects shouldn't serialize certain fields which are constant across all instances
		bool bIsBasicObject = (gameObject->m_Type != GameObjectType::OBJECT &&
						  gameObject->m_Type != GameObjectType::NONE);

		JSONObject object;
		std::string objectName = gameObject->m_Name;

		object.fields.push_back(JSONField("name", JSONValue(objectName)));

		if (gameObject->m_bLoadedFromPrefab)
		{
			object.fields.push_back(JSONField("type", JSONValue(std::string("prefab"))));
			object.fields.push_back(JSONField("prefab type", JSONValue(gameObject->m_PrefabName)));
		}
		else
		{
			object.fields.push_back(JSONField("type", JSONValue(GameObjectTypeToString(gameObject->m_Type))));
		}

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
		if (meshComponent && 
			!bIsBasicObject &&
			!gameObject->m_bLoadedFromPrefab)
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
				i32 materialArrayIndex = GetMaterialArrayIndex(material, gameContext);
				if (materialArrayIndex == -1)
				{
				//	Logger::LogError("Mesh object contains material not present in "
				//					 "BaseScene::m_LoadedMaterials! Parsing this file will fail! "
				//					 "Object name: " + objectName + 
				//					 ", matID: " + std::to_string(matID));
				}
				else
				{
					object.fields.push_back(JSONField("material array index", JSONValue(materialArrayIndex)));
				}
			}
		}

		btCollisionShape* collisionShape = gameObject->GetCollisionShape();
		if (collisionShape &&
			!gameObject->m_bLoadedFromPrefab)
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
		if (rigidBody &&
			!gameObject->m_bLoadedFromPrefab)
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
		case GameObjectType::VALVE:
		{
			JSONObject blockInfo;

			glm::vec2 valveRange(gameObject->m_ValveMembers.minRotation,
								 gameObject->m_ValveMembers.maxRotation);
			blockInfo.fields.push_back(JSONField("range", JSONValue(Vec2ToString(valveRange))));

			object.fields.push_back(JSONField("valve info", JSONValue(blockInfo)));
		} break;
		case GameObjectType::RISING_BLOCK:
		{
			JSONObject blockInfo;

			blockInfo.fields.push_back(JSONField("valve name", JSONValue(gameObject->m_RisingBlockMembers.valve->GetName())));
			blockInfo.fields.push_back(JSONField("move axis", JSONValue(Vec3ToString(gameObject->m_RisingBlockMembers.moveAxis))));
			blockInfo.fields.push_back(JSONField("affected by gravity", JSONValue(gameObject->m_RisingBlockMembers.bAffectedByGravity)));

			object.fields.push_back(JSONField("block info", JSONValue(blockInfo)));
		} break;
		case GameObjectType::GLASS_WINDOW:
		{
			JSONObject windowInfo;

			windowInfo.fields.push_back(JSONField("broken", JSONValue(gameObject->m_GlassWindowMembers.bBroken)));

			object.fields.push_back(JSONField("window info", JSONValue(windowInfo)));
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
		UNREFERENCED_PARAMETER(gameContext);

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

		static const bool defaultEnableAlbedo = false;
		if (shader.needAlbedoSampler && material.enableAlbedoSampler != defaultEnableAlbedo)
		{
			materialObject.fields.push_back(JSONField("enable albedo sampler",
											JSONValue(material.enableAlbedoSampler)));
		}

		static const bool defaultEnableMetallicSampler = false;
		if (shader.needMetallicSampler && material.enableMetallicSampler != defaultEnableMetallicSampler)
		{
			materialObject.fields.push_back(JSONField("enable metallic sampler",
											JSONValue(material.enableMetallicSampler)));
		}

		static const bool defaultEnableRoughness = false;
		if (shader.needRoughnessSampler && material.enableRoughnessSampler != defaultEnableRoughness)
		{
			materialObject.fields.push_back(JSONField("enable roughness sampler",
											JSONValue(material.enableRoughnessSampler)));
		}

		static const bool defaultEnableAO = false;
		if (shader.needAOSampler && material.enableAOSampler != defaultEnableAO)
		{
			materialObject.fields.push_back(JSONField("enable ao sampler",
											JSONValue(material.enableAOSampler)));
		}

		static const bool defaultEnableNormal = false;
		if (shader.needNormalSampler && material.enableNormalSampler != defaultEnableNormal)
		{
			materialObject.fields.push_back(JSONField("enable normal sampler",
											JSONValue(material.enableNormalSampler)));
		}

		static const bool defaultGenerateAlbedo = false;
		if (shader.needAlbedoSampler && material.generateAlbedoSampler != defaultGenerateAlbedo)
		{
			materialObject.fields.push_back(JSONField("generate albedo sampler",
											JSONValue(material.generateAlbedoSampler)));
		}

		static const bool defaultGenerateMetallicSampler = false;
		if (shader.needMetallicSampler && material.generateMetallicSampler != defaultGenerateMetallicSampler)
		{
			materialObject.fields.push_back(JSONField("generate metallic sampler",
											JSONValue(material.generateMetallicSampler)));
		}

		static const bool defaultGenerateRoughness = false;
		if (shader.needRoughnessSampler && material.generateRoughnessSampler != defaultGenerateRoughness)
		{
			materialObject.fields.push_back(JSONField("generate roughness sampler",
											JSONValue(material.generateRoughnessSampler)));
		}

		static const bool defaultGenerateAO = false;
		if (shader.needAOSampler && material.generateAOSampler != defaultGenerateAO)
		{
			materialObject.fields.push_back(JSONField("generate ao sampler",
											JSONValue(material.generateAOSampler)));
		}

		static const bool defaultGenerateNormal = false;
		if (shader.needNormalSampler && material.generateNormalSampler != defaultGenerateNormal)
		{
			materialObject.fields.push_back(JSONField("generate normal sampler",
											JSONValue(material.generateNormalSampler)));
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

		if (shader.needNormalSampler && !material.normalTexturePath.empty())
		{
			std::string normalTexturePath = material.normalTexturePath.substr(RESOURCE_LOCATION.length());
			materialObject.fields.push_back(JSONField("normal texture filepath",
											JSONValue(normalTexturePath)));
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
		UNREFERENCED_PARAMETER(gameContext);

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
		UNREFERENCED_PARAMETER(gameContext);

		JSONObject object;

		std::string dirStr = Vec3ToString(directionalLight.direction);
		object.fields.push_back(JSONField("direction", JSONValue(dirStr)));

		std::string colorStr = Vec3ToString(directionalLight.color);
		object.fields.push_back(JSONField("color", JSONValue(colorStr)));

		object.fields.push_back(JSONField("enabled", JSONValue(directionalLight.enabled != 0)));
		object.fields.push_back(JSONField("brightness", JSONValue(directionalLight.brightness)));

		return object;
	}

	GameObject* BaseScene::AddRootObject(GameObject* gameObject)
	{
		if (!gameObject)
		{
			return nullptr;
		}

		for (auto iter = m_RootObjects.begin(); iter != m_RootObjects.end(); ++iter)
		{
			if (*iter == gameObject)
			{
				Logger::LogWarning("Attempted to add same root object to scene more than once");
				return nullptr;
			}
		}

		m_RootObjects.push_back(gameObject);

		return gameObject;
	}

	void BaseScene::RemoveRootObject(GameObject* gameObject, bool deleteRootObject)
	{
		auto iter = m_RootObjects.begin();
		while (iter != m_RootObjects.end())
		{
			if (*iter == gameObject)
			{
				if (deleteRootObject)
				{
					SafeDelete(*iter);
				}

				iter = m_RootObjects.erase(iter);
				return;
			}
			else
			{
				++iter;
			}
		}

		Logger::LogWarning("Attempting to remove non-existent child from scene");
	}

	void BaseScene::RemoveAllRootObjects(bool deleteRootObjects)
	{
		auto iter = m_RootObjects.begin();
		while (iter != m_RootObjects.end())
		{
			if (deleteRootObjects)
			{
				delete *iter;
			}

			iter = m_RootObjects.erase(iter);
		}
	}

	GameObject* BaseScene::FirstObjectWithTag(const std::string& tag)
	{
		for (GameObject* gameObject : m_RootObjects)
		{
			GameObject* result = FindObjectWithTag(tag, gameObject);
			if (result)
			{
				return result;
			}
		}

		return nullptr;
	}

	Player* BaseScene::GetPlayer(i32 index)
	{
		if (index == 0)
		{
			return m_Player0;
		}
		else if (index == 1)
		{
			return m_Player1;
		}
		else
		{
			Logger::LogError("Requested invalid player from BaseScene with index: " + std::to_string(index));
			return nullptr;
		}
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

		return nullptr;
	}

	std::vector<GameObject*>& BaseScene::GetRootObjects()
	{
		return m_RootObjects;
	}

	void BaseScene::GetInteractibleObjects(std::vector<GameObject*>& interactibleObjects)
	{
		for (auto rootObject : m_RootObjects)
		{
			if (rootObject->m_bInteractable)
			{
				interactibleObjects.push_back(rootObject);
			}
		}
	}

	std::string BaseScene::GetName() const
	{
		return m_Name;
	}

	std::string BaseScene::GetFilePath() const
	{
		if (m_bUsingSaveFile)
		{
			return RESOURCE_LOCATION + "scenes/saved/" + m_FileName;
		}
		else
		{
			return RESOURCE_LOCATION + "scenes/default/" + m_FileName;
		}
	}

	std::string BaseScene::GetShortFilePath() const
	{
		if (m_bUsingSaveFile)
		{
			return "scenes/saved/" + m_FileName;
		}
		else
		{
			return "scenes/default/" + m_FileName;
		}
	}

	bool BaseScene::IsUsingSaveFile() const
	{
		return m_bUsingSaveFile;
	}

	PhysicsWorld* BaseScene::GetPhysicsWorld()
	{
		return m_PhysicsWorld;
	}

} // namespace flex
