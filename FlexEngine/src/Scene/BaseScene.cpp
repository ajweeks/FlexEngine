#include "stdafx.hpp"

#include "Scene/BaseScene.hpp"

#include <fstream>

#pragma warning(push, 0)
#include <glm/vec3.hpp>

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#pragma warning(pop)

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Helpers.hpp"
#include "JSONParser.hpp"
#include "Physics/PhysicsHelpers.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Profiler.hpp"
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

	void BaseScene::Initialize()
	{
		ParseFoundPrefabFiles();

		if (m_PhysicsWorld)
		{
			PrintWarn("Scene is being initialized more than once!\n");
		}

		// Physics world
		m_PhysicsWorld = new PhysicsWorld();
		m_PhysicsWorld->Initialize();

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

		if (FileExists(filePath))
		{
			Print("Loading scene from %s\n", shortFilePath.c_str());

			JSONObject sceneRootObject;
			if (!JSONParser::Parse(filePath, sceneRootObject))
			{
				PrintError("Failed to parse scene file: %s\n", shortFilePath.c_str());
				return;
			}

			const bool printSceneContentsToConsole = false;
			if (printSceneContentsToConsole)
			{
				Print("Parsed scene file:\n");
				Print(sceneRootObject.Print(0).c_str());
			}

			int sceneVersion = sceneRootObject.GetInt("version");
			if (sceneVersion != 1)
			{
				if (sceneRootObject.HasField("version"))
				{
					PrintError("Unhandled scene version: %i! Max handled version: 1\n", sceneVersion);
				}
				else
				{
					PrintError("Scene version missing from scene file. Assuming version 1\n");
				}
			}

			sceneRootObject.SetStringChecked("name", m_Name);

			std::vector<JSONObject> materialsArray = sceneRootObject.GetObjectArray("materials");
			for (u32 i = 0; i < materialsArray.size(); ++i)
			{
				MaterialCreateInfo matCreateInfo = {};
				Material::ParseJSONObject(materialsArray[i], matCreateInfo);

				MaterialID matID = g_Renderer->InitializeMaterial(&matCreateInfo);
				m_LoadedMaterials.push_back(matID);
			}


			// This holds all the objects in the scene which do not have a parent
			const std::vector<JSONObject>& rootObjects = sceneRootObject.GetObjectArray("objects");
			for (const JSONObject& rootObject : rootObjects)
			{
				AddRootObject(GameObject::CreateObjectFromJSON(rootObject, this, InvalidMaterialID));
			}

			if (sceneRootObject.HasField("point lights"))
			{
				const std::vector<JSONObject>& pointLightsArray = sceneRootObject.GetObjectArray("point lights");

				for (const JSONObject& pointLightObj : pointLightsArray)
				{
					PointLight pointLight = {};
					CreatePointLightFromJSON(pointLightObj, pointLight);

					g_Renderer->InitializePointLight(pointLight);
				}
			}

			if (sceneRootObject.HasField("directional light"))
			{
				const JSONObject& directionalLightObj = sceneRootObject.GetObject("directional light");

				DirectionalLight dirLight = {};
				CreateDirectionalLightFromJSON(directionalLightObj, dirLight);

				g_Renderer->InitializeDirectionalLight(dirLight);
			}
		}
		else
		{
			// File doesn't exist, create a new blank one
			Print("Creating new scene at: %s\n", shortFilePath.c_str());

			// Skybox
			{
				MaterialCreateInfo skyboxMatCreateInfo = {};
				skyboxMatCreateInfo.name = "Skybox";
				skyboxMatCreateInfo.shaderName = "skybox";
				skyboxMatCreateInfo.generateHDRCubemapSampler = true;
				skyboxMatCreateInfo.enableCubemapSampler = true;
				skyboxMatCreateInfo.enableCubemapTrilinearFiltering = true;
				skyboxMatCreateInfo.generatedCubemapSize = glm::vec2(512.0f);
				skyboxMatCreateInfo.generateIrradianceSampler = true;
				skyboxMatCreateInfo.generatedIrradianceCubemapSize = glm::vec2(32.0f);
				skyboxMatCreateInfo.generatePrefilteredMap = true;
				skyboxMatCreateInfo.generatedPrefilteredCubemapSize = glm::vec2(128.0f);
				skyboxMatCreateInfo.environmentMapPath = RESOURCE_LOCATION + "textures/hdri/Milkyway/Milkyway_Light.hdr";
				MaterialID skyboxMatID = g_Renderer->InitializeMaterial(&skyboxMatCreateInfo);

				m_LoadedMaterials.push_back(skyboxMatID);

				Skybox* skybox = new Skybox("Skybox");
				
				JSONObject emptyObj = {};
				skybox->ParseJSON(emptyObj, this, skyboxMatID);

				AddRootObject(skybox);
			}

			// Reflection probe
			{
				MaterialCreateInfo reflectionProbeMatCreateInfo = {};
				reflectionProbeMatCreateInfo.name = "Reflection Probe";
				reflectionProbeMatCreateInfo.shaderName = "pbr";
				reflectionProbeMatCreateInfo.constAlbedo = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
				reflectionProbeMatCreateInfo.constMetallic = 1.0f;
				reflectionProbeMatCreateInfo.constRoughness = 0.0f;
				reflectionProbeMatCreateInfo.constAO = 1.0f;
				MaterialID sphereMatID = g_Renderer->InitializeMaterial(&reflectionProbeMatCreateInfo);

				m_LoadedMaterials.push_back(sphereMatID);

				ReflectionProbe* reflectionProbe = new ReflectionProbe("Reflection Probe 01");
				
				JSONObject emptyObj = {};
				reflectionProbe->ParseJSON(emptyObj, this, sphereMatID);

				AddRootObject(reflectionProbe);
			}
		}


		{
			const std::string gridMatName = "Grid";
			if (!g_Renderer->GetMaterialID(gridMatName, m_GridMaterialID))
			{
				MaterialCreateInfo gridMatInfo = {};
				gridMatInfo.shaderName = "color";
				gridMatInfo.name = gridMatName;
				m_GridMaterialID = g_Renderer->InitializeMaterial(&gridMatInfo);
			}

			m_Grid = new GameObject("Grid", GameObjectType::OBJECT);
			MeshComponent* gridMesh = m_Grid->SetMeshComponent(new MeshComponent(m_GridMaterialID, m_Grid));
			RenderObjectCreateInfo createInfo = {};
			createInfo.editorObject = true;
			gridMesh->LoadPrefabShape(MeshComponent::PrefabShape::GRID, &createInfo);
			m_Grid->GetTransform()->Translate(0.0f, -0.1f, 0.0f);
			m_Grid->SetSerializable(false);
			m_Grid->SetStatic(true);
			AddRootObject(m_Grid);
		}


		{
			const std::string worldOriginMatName = "World origin";
			if (!g_Renderer->GetMaterialID(worldOriginMatName, m_WorldAxisMaterialID))
			{
				MaterialCreateInfo worldAxisMatInfo = {};
				worldAxisMatInfo.shaderName = "color";
				worldAxisMatInfo.name = worldOriginMatName;
				m_WorldAxisMaterialID = g_Renderer->InitializeMaterial(&worldAxisMatInfo);
			}

			m_WorldOrigin = new GameObject("World origin", GameObjectType::OBJECT);
			MeshComponent* orignMesh = m_WorldOrigin->SetMeshComponent(new MeshComponent(m_WorldAxisMaterialID, m_WorldOrigin));
			RenderObjectCreateInfo createInfo = {};
			createInfo.editorObject = true;
			orignMesh->LoadPrefabShape(MeshComponent::PrefabShape::WORLD_AXIS_GROUND, &createInfo);
			m_WorldOrigin->GetTransform()->Translate(0.0f, -0.09f, 0.0f);
			m_WorldOrigin->SetSerializable(false);
			m_WorldOrigin->SetStatic(true);
			AddRootObject(m_WorldOrigin);
		}

		//m_Player0 = new Player(0);
		//AddRootObject(m_Player0);
		//
		//m_Player1 = new Player(1);
		//AddRootObject(m_Player1);

		for (auto iter = m_RootObjects.begin(); iter != m_RootObjects.end(); ++iter)
		{
			(*iter)->Initialize();
		}

		m_bLoaded = true;
	}

	void BaseScene::PostInitialize()
	{
		for (auto iter = m_RootObjects.begin(); iter != m_RootObjects.end(); ++iter)
		{
			GameObject* gameObject = *iter;

			gameObject->PostInitialize();
		}

		m_PhysicsWorld->GetWorld()->setDebugDrawer(g_Renderer->GetDebugDrawer());
	}

	void BaseScene::Destroy()
	{
		m_bLoaded = false;

		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject)
			{
				rootObject->Destroy();
				SafeDelete(rootObject);
			}
		}
		m_RootObjects.clear();

		m_LoadedMaterials.clear();

		g_Renderer->ClearMaterials();
		g_Renderer->SetSkyboxMesh(nullptr);
		g_Renderer->ClearDirectionalLight();
		g_Renderer->ClearPointLights();

		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Destroy();
			SafeDelete(m_PhysicsWorld);
		}
	}

	void BaseScene::Update()
	{
		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Update(g_DeltaTime);
		}

		if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_Z))
		{
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}
		if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_X))
		{
			AudioManager::PauseSource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}

		if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_C))
		{
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::drmapan));
		}
		if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_V))
		{
			AudioManager::PauseSource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::drmapan));
		}
		if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_B))
		{
			AudioManager::StopSource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::drmapan));
		}

		if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_L))
		{
			AudioManager::AddToSourcePitch(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud),
										   0.5f * g_DeltaTime);
		}
		if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_K))
		{
			AudioManager::AddToSourcePitch(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud),
										   -0.5f * g_DeltaTime);
		}

		if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_P))
		{
			AudioManager::ScaleSourceGain(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud),
										  1.1f);
		}
		if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_O))
		{
			AudioManager::ScaleSourceGain(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud),
										  1.0f / 1.1f);
		}


		BaseCamera* camera = g_CameraManager->CurrentCamera();

		// Fade grid out when far away
		{
			float maxHeightVisible = 350.0f;
			float distCamToGround = camera->GetPosition().y;
			float maxDistVisible = 300.0f;
			float distCamToOrigin = glm::distance(camera->GetPosition(), glm::vec3(0, 0, 0));

			glm::vec4 gridColorMutliplier = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f - glm::clamp(distCamToGround / maxHeightVisible, -1.0f, 1.0f));
			glm::vec4 axisColorMutliplier = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f - glm::clamp(distCamToOrigin / maxDistVisible, -1.0f, 1.0f));;
			g_Renderer->GetMaterial(m_WorldAxisMaterialID).colorMultiplier = axisColorMutliplier;
			g_Renderer->GetMaterial(m_GridMaterialID).colorMultiplier = gridColorMutliplier;
		}

		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject)
			{
				rootObject->Update();
			}
		}
	}

	bool BaseScene::DestroyGameObject(GameObject* targetObject,
									 bool bDeleteChildren)
	{
		for (GameObject* gameObject : m_RootObjects)
		{
			if (DestroyGameObjectRecursive(gameObject, targetObject, bDeleteChildren))
			{
				return true;
			}
		}
		return false;
	}

	bool BaseScene::IsLoaded() const
	{
		return m_bLoaded;
	}

	bool BaseScene::DestroyGameObjectRecursive(GameObject* currentObject,
											  GameObject* targetObject,
											  bool bDeleteChildren)
	{
		if (currentObject == targetObject)
		{
			// Target's parent pointer will be cleared upon removing from parent, cache it before that happens
			GameObject* targetParent = targetObject->m_Parent;
			if (targetObject->m_Parent)
			{
				targetParent->RemoveChild(targetObject);
			}
			else
			{
				auto result = Contains(m_RootObjects, targetObject);
				if (result != m_RootObjects.end())
				{
					m_RootObjects.erase(result);
				}
			}

			// Set children's parents
			if (!bDeleteChildren)
			{
				for (GameObject* childObject : targetObject->m_Children)
				{
					if (targetParent)
					{
						targetParent->AddChild(childObject);
					}
					else
					{
						AddRootObject(childObject);
					}
				}
			}

			if (targetObject == g_EngineInstance->GetSelectedObject())
			{
				g_EngineInstance->SetSelectedObject(nullptr);
			}

			// If children are still in m_Children array when
			// targetObject is destroyed they will also be destroyed
			if (!bDeleteChildren)
			{
				targetObject->m_Children.clear();
			}

			targetObject->Destroy();

			SafeDelete(targetObject);
			return true;
		}

		for (GameObject* childObject : currentObject->m_Children)
		{
			if (DestroyGameObjectRecursive(childObject, targetObject, bDeleteChildren))
			{
				return true;
			}
		}

		return false;
	}

	i32 BaseScene::GetMaterialArrayIndex(const Material& material)
	{
		i32 materialArrayIndex = -1;
		for (u32 j = 0; j < m_LoadedMaterials.size(); ++j)
		{
			Material& loadedMat = g_Renderer->GetMaterial(m_LoadedMaterials[j]);
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
				Print("Parsing prefab: %s\n", fileName.c_str());

				JSONObject obj;
				if (JSONParser::Parse(foundFilePath, obj))
				{
					m_ParsedPrefabs.push_back(obj);
				}
				else
				{
					PrintError("Failed to parse prefab file: %s\n", foundFilePath.c_str());
				}
			}
		}
		else
		{
			PrintError("Failed to find files in \"scenes/prefabs/\"!\n");
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
				PrintError("Invalid material index for object %s: %i\n",
						   object.GetString("name").c_str(), materialArrayIndex);
			}
		}
		return matID;
	}

	void BaseScene::CreatePointLightFromJSON(const JSONObject& obj, PointLight& pointLight)
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

	void BaseScene::CreateDirectionalLightFromJSON(const JSONObject& obj, DirectionalLight& directionalLight)
	{
		std::string dirStr = obj.GetString("direction");
		directionalLight.direction = glm::vec4(ParseVec3(dirStr), 0);

		std::string posStr = obj.GetString("position");
		if (!posStr.empty())
		{
			directionalLight.position = ParseVec3(posStr);
		}

		obj.SetVec4Checked("color", directionalLight.color);

		obj.SetFloatChecked("brightness", directionalLight.brightness);

		if (obj.HasField("enabled"))
		{
			directionalLight.enabled = obj.GetBool("enabled") ? 1 : 0;
		}
	}

	void BaseScene::SerializeToFile(bool bSaveOverDefault /* = false */)
	{
		bool success = false;

		const std::string profileBlockName = "serialize scene to file: " + m_FileName;
		PROFILE_BEGIN(profileBlockName);

		JSONObject rootSceneObject = {};

		i32 fileVersion = 1;

		rootSceneObject.fields.push_back(JSONField("version", JSONValue(fileVersion)));
		rootSceneObject.fields.push_back(JSONField("name", JSONValue(m_Name)));


		JSONField materialsField = {};
		materialsField.label = "materials";
		std::vector<JSONObject> materialsArray;
		for (MaterialID matID : m_LoadedMaterials)
		{
			Material& material = g_Renderer->GetMaterial(matID);
			materialsArray.push_back(material.SerializeToJSON());
		}
		materialsField.value = JSONValue(materialsArray);
		rootSceneObject.fields.push_back(materialsField);


		std::vector<JSONObject> objectsArray;
		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject->IsSerializable())
			{
				if (rootObject->IsSerializable())
				{
					objectsArray.push_back(rootObject->SerializeToJSON(this));
				}
			}
		}
		rootSceneObject.fields.push_back(JSONField("objects", JSONValue(objectsArray)));


		JSONField pointLightsField = {};
		pointLightsField.label = "point lights";
		std::vector<JSONObject> pointLightsArray;
		for (i32 i = 0; i < g_Renderer->GetNumPointLights(); ++i)
		{
			PointLight& pointLight = g_Renderer->GetPointLight(i);
			pointLightsArray.push_back(SerializePointLight(pointLight));
		}
		pointLightsField.value = JSONValue(pointLightsArray);
		rootSceneObject.fields.push_back(pointLightsField);

		DirectionalLight& dirLight = g_Renderer->GetDirectionalLight();
		JSONField directionalLightsField("directional light",
			JSONValue(SerializeDirectionalLight(dirLight)));
		rootSceneObject.fields.push_back(directionalLightsField);

		std::string fileContents = rootSceneObject.Print(0);

		std::string defaultShortSaveFilePath = "scenes/default/" + m_FileName;
		std::string savedShortSaveFilePath = "scenes/saved/" + m_FileName;
		std::string shortSavedFileName;
		if (bSaveOverDefault)
		{
			shortSavedFileName = defaultShortSaveFilePath;
		}
		else
		{
			shortSavedFileName = savedShortSaveFilePath;
		}
		Print("Serializing scene to %s\n", shortSavedFileName.c_str());
		
		if (bSaveOverDefault)
		{
			m_bUsingSaveFile = false;

			if (FileExists(RESOURCE_LOCATION + savedShortSaveFilePath))
			{
				DeleteFile(RESOURCE_LOCATION + savedShortSaveFilePath);
			}
		}


		std::string savedFilePathName = RESOURCE_LOCATION + shortSavedFileName;
		savedFilePathName = RelativePathToAbsolute(savedFilePathName);
		success = WriteFile(savedFilePathName, fileContents, false);

		if (success)
		{
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::blip));

			if (!bSaveOverDefault)
			{
				m_bUsingSaveFile = true;
			}

			PROFILE_END(profileBlockName);
			Profiler::PrintBlockDuration(profileBlockName);
		}
		else
		{
			PrintError("Failed to open file for writing at \"%s\", Can't serialize scene\n", savedFilePathName.c_str());
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}
	}

	void BaseScene::DeleteSaveFiles()
	{
		std::string defaultShortSaveFilePath = "scenes/default/" + m_FileName;
		std::string savedShortSaveFilePath = "scenes/saved/" + m_FileName;

		bool bDefaultFileExists = FileExists(RESOURCE_LOCATION + defaultShortSaveFilePath);
		bool bSavedFileExists = FileExists(RESOURCE_LOCATION + savedShortSaveFilePath);

		if (bSavedFileExists ||
			bDefaultFileExists)
		{
			Print("Deleting scene's save files from %s\n", m_FileName.c_str());

			if (bDefaultFileExists)
			{
				DeleteFile(RESOURCE_LOCATION + defaultShortSaveFilePath);
			}

			if (bSavedFileExists)
			{
				DeleteFile(RESOURCE_LOCATION + savedShortSaveFilePath);
			}
		}

		m_bUsingSaveFile = false;
	}

	JSONObject BaseScene::SerializePointLight(PointLight& pointLight)
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

	JSONObject BaseScene::SerializeDirectionalLight(DirectionalLight& directionalLight)
	{
		JSONObject object;

		std::string dirStr = Vec3ToString(directionalLight.direction);
		object.fields.push_back(JSONField("direction", JSONValue(dirStr)));

		std::string posStr = Vec3ToString(directionalLight.position);
		object.fields.push_back(JSONField("position", JSONValue(posStr)));

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
				PrintWarn("Attempted to add same root object (%s) to scene more than once\n", gameObject->m_Name.c_str());
				return nullptr;
			}
		}

		gameObject->SetParent(nullptr);
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

		PrintWarn("Attempting to remove non-existent child from scene %s\n", m_Name.c_str());
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

	std::vector<MaterialID> BaseScene::GetMaterialIDs()
	{
		return m_LoadedMaterials;
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
			PrintError("Requested invalid player from BaseScene with index: %i\n", index);
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

	void BaseScene::SetName(const std::string& name)
	{
		assert(!name.empty());
		m_Name = name;
	}

	std::string BaseScene::GetName() const
	{
		return m_Name;
	}

	std::string BaseScene::GetDefaultRelativeFilePath() const
	{
		return RESOURCE_LOCATION + "scenes/default/" + m_FileName;
	}

	std::string BaseScene::GetRelativeFilePath() const
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

	std::string BaseScene::GetShortRelativeFilePath() const
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

	bool BaseScene::SetFileName(const std::string& fileName, bool bDeletePreviousFiles)
	{
		bool success = false;

		if (fileName == m_FileName)
		{
			return true;
		}

		std::string absDefaultFilePathFrom = RelativePathToAbsolute(GetDefaultRelativeFilePath());
		std::string defaultAbsFileDir = absDefaultFilePathFrom;
		ExtractDirectoryString(defaultAbsFileDir);
		std::string absDefaultFilePathTo = defaultAbsFileDir + fileName;
		if (CopyFile(absDefaultFilePathFrom, absDefaultFilePathTo))
		{
			if (m_bUsingSaveFile)
			{
				std::string absSavedFilePathFrom = RelativePathToAbsolute(GetRelativeFilePath());
				std::string savedAbsFileDir = absSavedFilePathFrom;
				ExtractDirectoryString(savedAbsFileDir);
				std::string absSavedFilePathTo = savedAbsFileDir + fileName;
				if (CopyFile(absSavedFilePathFrom, absSavedFilePathTo))
				{
					success = true;
				}
			}
			else
			{
				success = true;
			}

			// Don't delete files unless copies worked
			if (success)
			{
				if (bDeletePreviousFiles)
				{
					std::string pAbsDefaultFilePath = RelativePathToAbsolute(GetDefaultRelativeFilePath());
					DeleteFile(pAbsDefaultFilePath, false);

					if (m_bUsingSaveFile)
					{
						std::string pAbsSavedFilePath = RelativePathToAbsolute(GetRelativeFilePath());
						DeleteFile(pAbsSavedFilePath, false);
					}
				}

				m_FileName = fileName;
			}
		}

		return success;
	}

	std::string BaseScene::GetFileName() const
	{
		return m_FileName;
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
