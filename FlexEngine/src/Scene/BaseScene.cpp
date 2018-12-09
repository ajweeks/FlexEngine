#include "stdafx.hpp"

#include "Scene/BaseScene.hpp"

#include <fstream>

#pragma warning(push, 0)
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

#include <glm/vec3.hpp>
#include <glm/gtx/norm.hpp> // for distance2
#pragma warning(pop)

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Physics/PhysicsHelpers.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Profiler.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Track/BezierCurve.hpp"

namespace flex
{
	std::vector<JSONObject> BaseScene::s_ParsedMaterials;
	std::vector<JSONObject> BaseScene::s_ParsedMeshes;
	std::vector<JSONObject> BaseScene::s_ParsedPrefabs;

	BaseScene::BaseScene(const std::string& fileName) :
		m_FileName(fileName)
	{
	}

	BaseScene::~BaseScene()
	{
	}

	void BaseScene::Initialize()
	{
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
		const std::string savedPath = RESOURCE_STR(savedShortPath);
		const std::string defaultPath = RESOURCE_STR(defaultShortPath);
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

			// TODO: Only initialize materials currently present in this scene
			for (JSONObject& materialObj : s_ParsedMaterials)
			{
				MaterialCreateInfo matCreateInfo = {};
				Material::ParseJSONObject(materialObj, matCreateInfo);

				MaterialID matID = g_Renderer->InitializeMaterial(&matCreateInfo);
				m_LoadedMaterials.push_back(matID);
			}


			// This holds all the objects in the scene which do not have a parent
			const std::vector<JSONObject>& rootObjects = sceneRootObject.GetObjectArray("objects");
			for (const JSONObject& rootObjectJSON : rootObjects)
			{
				GameObject* rootObj = GameObject::CreateObjectFromJSON(rootObjectJSON, this, InvalidMaterialID);
				rootObj->GetTransform()->UpdateParentTransform();
				AddRootObject(rootObj);
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
				//MaterialCreateInfo skyboxMatCreateInfo = {};
				//skyboxMatCreateInfo.name = "Skybox";
				//skyboxMatCreateInfo.shaderName = "skybox";
				//skyboxMatCreateInfo.generateHDRCubemapSampler = true;
				//skyboxMatCreateInfo.enableCubemapSampler = true;
				//skyboxMatCreateInfo.enableCubemapTrilinearFiltering = true;
				//skyboxMatCreateInfo.generatedCubemapSize = glm::vec2(512.0f);
				//skyboxMatCreateInfo.generateIrradianceSampler = true;
				//skyboxMatCreateInfo.generatedIrradianceCubemapSize = glm::vec2(32.0f);
				//skyboxMatCreateInfo.generatePrefilteredMap = true;
				//skyboxMatCreateInfo.generatedPrefilteredCubemapSize = glm::vec2(128.0f);
				//skyboxMatCreateInfo.environmentMapPath = RESOURCE_LOCATION  "textures/hdri/Milkyway/Milkyway_Light.hdr";
				//MaterialID skyboxMatID = g_Renderer->InitializeMaterial(&skyboxMatCreateInfo);

				//m_LoadedMaterials.push_back(skyboxMatID);

				MaterialID skyboxMatID = InvalidMaterialID;
				g_Renderer->GetMaterialID("skybox 01", skyboxMatID);

				assert(skyboxMatID != InvalidMaterialID);

				Skybox* skybox = new Skybox("Skybox");

				JSONObject emptyObj = {};
				skybox->ParseJSON(emptyObj, this, skyboxMatID);

				AddRootObject(skybox);
			}

			// Reflection probe
			{
				//MaterialCreateInfo reflectionProbeMatCreateInfo = {};
				//reflectionProbeMatCreateInfo.name = "Reflection Probe";
				//reflectionProbeMatCreateInfo.shaderName = "pbr";
				//reflectionProbeMatCreateInfo.constAlbedo = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
				//reflectionProbeMatCreateInfo.constMetallic = 1.0f;
				//reflectionProbeMatCreateInfo.constRoughness = 0.0f;
				//reflectionProbeMatCreateInfo.constAO = 1.0f;
				//MaterialID sphereMatID = g_Renderer->InitializeMaterial(&reflectionProbeMatCreateInfo);

				//m_LoadedMaterials.push_back(sphereMatID);

				MaterialID sphereMatID = InvalidMaterialID;
				g_Renderer->GetMaterialID("pbr chrome", sphereMatID);

				assert(sphereMatID != InvalidMaterialID);

				ReflectionProbe* reflectionProbe = new ReflectionProbe("Reflection Probe 01");

				JSONObject emptyObj = {};
				reflectionProbe->ParseJSON(emptyObj, this, sphereMatID);
				reflectionProbe->GetTransform()->SetLocalPosition(glm::vec3(0.0f, 8.0f, 0.0f));

				AddRootObject(reflectionProbe);
			}

			DirectionalLight dirLight = {};
			dirLight.rotation = glm::quat(glm::vec3(130.0f, -65.0f, 120.0f));
			dirLight.position = glm::vec3(0.0f, 15.0f, 0.0f);
			dirLight.brightness = 5.0f;
			g_Renderer->InitializeDirectionalLight(dirLight);
		}

		m_Player0 = new Player(0, glm::vec3(0.0f, 2.0f, 0.0f));
		AddRootObject(m_Player0);

		//m_Player1 = new Player(1);
		//AddRootObject(m_Player1);

		m_TrackManager = TrackManager();
		{
			BezierCurve curve0({ 5.0f, 0.1f, 5.0f }, { 15.0f, 0.1f, 10.0f }, { 25.0f, 0.1f, 10.0f }, { 35.0f, 0.1f, 10.0f });
			BezierCurve curve1({ 35.0f, 0.1f, 10.0f }, { 45.0f, 0.1f, 10.0f }, { 55.0f, 0.1f, 10.0f }, { 60.0f, 0.1f, 10.0f });
			BezierCurve curve2({ 60.0f, 0.1f, 10.0f }, { 70.0f, 0.1f, 10.0f }, { 73.0f, 0.1f, 8.0f }, { 73.0f, 0.1f, 2.0f });
			BezierCurve curve3({ 73.0f, 0.1f, 2.0f }, { 73.0f, 0.1f, -6.0f }, { 60.0f, 0.1f, -8.0f }, { 50.0f, 0.1f, -8.0f });
			m_TrackManager.AddTrack(BezierCurveList({ curve0 , curve1, curve2, curve3 }));
		}
		{
			BezierCurve curve0({ 35.0f, 0.1f, 10.0f }, { 35.0f, 0.1f, 15.0f }, { 30.0f, 0.1f, 20.0f }, { 25.0f, 0.1f, 22.0f });
			BezierCurve curve1({ 25.0f, 0.1f, 22.0f }, { 20.0f, 0.1f, 24.0f }, { 15.0f, 0.1f, 25.0f }, { 10.0f, 0.1f, 25.0f });
			m_TrackManager.AddTrack(BezierCurveList({ curve0 , curve1 }));
		}
		{
			BezierCurve curve0({ 35.0f, 0.1f, 10.0f }, { 40.0f, 0.1f, -2.0f }, { 45.0f, 0.1f, 4.0f }, { 50.0f, 0.1f, -8.0f });
			m_TrackManager.AddTrack(BezierCurveList({ curve0 }));
		}
		m_TrackManager.FindJunctions();


		for (GameObject* rootObject : m_RootObjects)
		{
			rootObject->Initialize();
		}
		UpdateRootObjectSiblingIndices();

		m_bLoaded = true;
	}

	void BaseScene::PostInitialize()
	{
		for (GameObject* rootObject : m_RootObjects)
		{
			rootObject->PostInitialize();
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

		if (g_InputManager->GetKeyPressed(Input::KeyCode::KEY_Z))
		{
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}
		if (g_InputManager->GetKeyPressed(Input::KeyCode::KEY_X))
		{
			AudioManager::PauseSource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}

		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject)
			{
				rootObject->Update();
			}
		}

		m_TrackManager.DrawDebug();
	}

	bool BaseScene::DestroyGameObject(GameObject* targetObject,
									 bool bDeleteChildren)
	{
		for (GameObject* gameObject : m_RootObjects)
		{
			if (DestroyGameObjectRecursive(gameObject, targetObject, bDeleteChildren))
			{
				UpdateRootObjectSiblingIndices();
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
				auto iter = Find(m_RootObjects, targetObject);
				if (iter != m_RootObjects.end())
				{
					m_RootObjects.erase(iter);
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

			if (g_EngineInstance->IsObjectSelected(targetObject))
			{
				g_EngineInstance->DeselectObject(targetObject);
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

	void BaseScene::ParseFoundMeshFiles()
	{
		s_ParsedMeshes.clear();

		std::string meshesFilePath = RESOURCE_LOCATION  "scenes/meshes.json";
		if (FileExists(meshesFilePath))
		{
			std::string cleanedFilePath = meshesFilePath;
			StripLeadingDirectories(cleanedFilePath);
			Print("Parsing meshes file at %s\n", cleanedFilePath.c_str());

			JSONObject obj;
			if (JSONParser::Parse(meshesFilePath, obj))
			{
				auto meshObjects = obj.GetObjectArray("meshes");
				for (auto meshObject : meshObjects)
				{
					s_ParsedMeshes.push_back(meshObject);
				}
			}
			else
			{
				PrintError("Failed to parse mesh file: %s\n", meshesFilePath.c_str());
				return;
			}
		}
		else
		{
			PrintError("Failed to parse meshes file at %s\n", meshesFilePath.c_str());
			return;
		}

		Print("Parsed %i meshes\n", s_ParsedMeshes.size());
	}

	void BaseScene::ParseFoundMaterialFiles()
	{
		s_ParsedMaterials.clear();

		std::string materialsFilePath = RESOURCE_LOCATION  "scenes/materials.json";
		if (FileExists(materialsFilePath))
		{
			std::string cleanedFilePath = materialsFilePath;
			StripLeadingDirectories(cleanedFilePath);
			Print("Parsing materials file at %s\n", cleanedFilePath.c_str());

			JSONObject obj;
			if (JSONParser::Parse(materialsFilePath, obj))
			{
				auto materialObjects = obj.GetObjectArray("materials");
				for (auto materialObject : materialObjects)
				{
					s_ParsedMaterials.push_back(materialObject);
				}
			}
			else
			{
				PrintError("Failed to parse materials file: %s\n", materialsFilePath.c_str());
				return;
			}
		}
		else
		{
			PrintError("Failed to parse materials file at %s\n", materialsFilePath.c_str());
			return;
		}

		Print("Parsed %i materials\n", s_ParsedMaterials.size());
	}

	void BaseScene::ParseFoundPrefabFiles()
	{
		s_ParsedPrefabs.clear();

		std::vector<std::string> foundFiles;
		if (FindFilesInDirectory(RESOURCE_LOCATION  "scenes/prefabs/", foundFiles, ".json"))
		{
			for (const std::string& foundFilePath : foundFiles)
			{
				std::string fileName = foundFilePath;
				StripLeadingDirectories(fileName);
				Print("Parsing prefab: %s\n", fileName.c_str());

				JSONObject obj;
				if (JSONParser::Parse(foundFilePath, obj))
				{
					s_ParsedPrefabs.push_back(obj);
				}
				else
				{
					PrintError("Failed to parse prefab file: %s\n", foundFilePath.c_str());
					return;
				}
			}
		}
		else
		{
			PrintError("Failed to find files in \"scenes/prefabs/\"!\n");
			return;
		}

		Print("Parsed %i prefabs\n", s_ParsedPrefabs.size());
	}

	bool BaseScene::SerializeMeshFile()
	{
		std::string meshesFilePath = RESOURCE_LOCATION  "scenes/meshes.json";

		JSONObject meshesObj = {};

		meshesObj.fields.emplace_back("version", JSONValue(m_FileVersion));

		static const std::string meshPrefix = RESOURCE_LOCATION  "meshes/";
		// Overwrite all meshes in current scene in case any values were tweaked
		std::vector<GameObject*> allObjects = g_SceneManager->CurrentScene()->GetAllObjects();
		for (GameObject* obj : allObjects)
		{
			MeshComponent* mesh = obj->GetMeshComponent();
			if (mesh)
			{
				std::string meshFileName = mesh->GetRelativeFilePath();
				if (!meshFileName.empty())
				{
					meshFileName = meshFileName.substr(meshPrefix.length());
					bool bFound = false;
					for (JSONObject& parsedMeshObj : s_ParsedMeshes)
					{
						if (parsedMeshObj.GetString("file").compare(meshFileName) == 0)
						{
							parsedMeshObj = mesh->SerializeToJSON();
							bFound = true;
							break;
						}
					}

					if (!bFound)
					{
						JSONObject serializedMeshObj = mesh->SerializeToJSON();
						s_ParsedMeshes.push_back(serializedMeshObj);
					}

				}
			}
		}

		meshesObj.fields.emplace_back("meshes", JSONValue(s_ParsedMeshes));

		std::string fileContents = meshesObj.Print(0);

		if (WriteFile(meshesFilePath, fileContents, false))
		{
			Print("Serialized mesh file %s\n", meshesFilePath.c_str());
		}
		else
		{
			PrintWarn("Failed to serialize mesh file %s\n", meshesFilePath.c_str());
			return false;
		}

		return true;
	}

	bool BaseScene::SerializeMaterialFile()
	{
		std::string materialsFilePath = RESOURCE_LOCATION  "scenes/materials.json";

		JSONObject materialsObj = {};

		materialsObj.fields.emplace_back("version", JSONValue(m_FileVersion));

		// Overwrite all materials in current scene in case any values were tweaked
		std::vector<MaterialID> currentSceneMatIDs = g_SceneManager->CurrentScene()->GetMaterialIDs();
		for (MaterialID matID : currentSceneMatIDs)
		{
			bool bExistsInFile = false;
			Material& mat = g_Renderer->GetMaterial(matID);
			for (JSONObject& parsedMatObj : s_ParsedMaterials)
			{
				if (parsedMatObj.GetString("name").compare(mat.name) == 0)
				{
					parsedMatObj = mat.SerializeToJSON();
					bExistsInFile = true;
					break;
				}
			}

			if (!bExistsInFile)
			{
				s_ParsedMaterials.push_back(mat.SerializeToJSON());
			}
		}

		materialsObj.fields.emplace_back("materials", JSONValue(s_ParsedMaterials));

		std::string fileContents = materialsObj.Print(0);

		if (WriteFile(materialsFilePath, fileContents, false))
		{
			Print("Serialized materials file %s\n", materialsFilePath.c_str());
		}
		else
		{
			PrintWarn("Failed to serialize materials file %s\n", materialsFilePath.c_str());
			return false;
		}

		return true;
	}

	bool BaseScene::SerializePrefabFile()
	{
		// TODO: Implement
		PrintWarn("SerializePrefabFile is unimplemented!\n");

		return false;
	}

	std::vector<GameObject*> BaseScene::GetAllObjects()
	{
		std::vector<GameObject*> result;

		for (GameObject* obj : m_RootObjects)
		{
			obj->AddSelfAndChildrenToVec(result);
		}

		return result;
	}

	TrackManager* BaseScene::GetTrackManager()
	{
		return &m_TrackManager;
	}

	MaterialID BaseScene::FindMaterialIDByName(const JSONObject& object)
	{
		MaterialID matID = InvalidMaterialID;
		std::string materialName;
		if (object.SetStringChecked("material", materialName))
		{
			if (!materialName.empty())
			{
				for (MaterialID loadedMatID : m_LoadedMaterials)
				{
					Material& mat = g_Renderer->GetMaterial(loadedMatID);
					if (mat.name.compare(materialName) == 0)
					{
						matID = loadedMatID;
						break;
					}
				}
			}
			else
			{
				matID = InvalidMaterialID;
				PrintError("Invalid material name for object %s: %s\n",
						   object.GetString("name").c_str(), materialName.c_str());
			}
		}
		return matID;
	}

	void BaseScene::UpdateRootObjectSiblingIndices()
	{
		for (i32 i = 0; i < (i32)m_RootObjects.size(); ++i)
		{
			m_RootObjects[i]->UpdateSiblingIndices(i);
		}
	}

	void BaseScene::CreatePointLightFromJSON(const JSONObject& obj, PointLight& pointLight)
	{
		std::string posStr = obj.GetString("pos");
		pointLight.position = glm::vec4(ParseVec3(posStr), 0);

		obj.SetVec4Checked("color", pointLight.color);

		obj.SetFloatChecked("brightness", pointLight.brightness);

		if (obj.HasField("enabled"))
		{
			pointLight.enabled = obj.GetBool("enabled") ? 1 : 0;
		}
	}

	void BaseScene::CreateDirectionalLightFromJSON(const JSONObject& obj, DirectionalLight& directionalLight)
	{
		std::string dirStr = obj.GetString("rotation");
		directionalLight.rotation = glm::quat(ParseVec3(dirStr));

		std::string posStr = obj.GetString("pos");
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

		obj.SetBoolChecked("cast shadows", directionalLight.bCastShadow);
		obj.SetFloatChecked("shadow darkness", directionalLight.shadowDarkness);

		if (obj.HasField("shadow map near"))
		{
			obj.SetFloatChecked("shadow map near", directionalLight.shadowMapNearPlane);
		}

		if (obj.HasField("shadow map far"))
		{
			obj.SetFloatChecked("shadow map far", directionalLight.shadowMapFarPlane);
		}

		if (obj.HasField("shadow map zoom"))
		{
			obj.SetFloatChecked("shadow map zoom", directionalLight.shadowMapZoom);
		}
	}

	void BaseScene::SerializeToFile(bool bSaveOverDefault /* = false */)
	{
		bool success = false;

		success |= BaseScene::SerializeMeshFile();
		success |= BaseScene::SerializeMaterialFile();
		success |= BaseScene::SerializePrefabFile();

		const std::string profileBlockName = "serialize scene to file: " + m_FileName;
		PROFILE_BEGIN(profileBlockName);

		JSONObject rootSceneObject = {};

		rootSceneObject.fields.emplace_back("version", JSONValue(m_FileVersion));
		rootSceneObject.fields.emplace_back("name", JSONValue(m_Name));

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
		rootSceneObject.fields.emplace_back("objects", JSONValue(objectsArray));


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

		const std::string defaultSaveFilePathShort = "scenes/default/" + m_FileName;
		const std::string savedSaveFilePathShort = "scenes/saved/" + m_FileName;
		const std::string defaultSaveFilePath = RESOURCE_STR(defaultSaveFilePathShort);
		const std::string savedSaveFilePath = RESOURCE_STR(savedSaveFilePathShort);
		std::string shortSavedFileName;
		if (bSaveOverDefault)
		{
			shortSavedFileName = defaultSaveFilePathShort;
		}
		else
		{
			shortSavedFileName = savedSaveFilePathShort;
		}
		Print("Serializing scene to %s\n", shortSavedFileName.c_str());

		if (bSaveOverDefault)
		{
			m_bUsingSaveFile = false;

			if (FileExists(savedSaveFilePath))
			{
				DeleteFile(savedSaveFilePath);
			}
		}


		std::string savedFilePathName = RESOURCE_STR(shortSavedFileName);
		savedFilePathName = RelativePathToAbsolute(savedFilePathName);
		success = WriteFile(savedFilePathName, fileContents, false);

		if (success)
		{
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::blip));
			g_Renderer->AddEditorString("Saved " + m_Name);

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
		const std::string defaultSaveFilePath = RESOURCE("scenes/default/" + m_FileName);
		const std::string savedSaveFilePath = RESOURCE("scenes/saved/" + m_FileName);

		bool bDefaultFileExists = FileExists(defaultSaveFilePath);
		bool bSavedFileExists = FileExists(savedSaveFilePath);

		if (bSavedFileExists ||
			bDefaultFileExists)
		{
			Print("Deleting scene's save files from %s\n", m_FileName.c_str());

			if (bDefaultFileExists)
			{
				DeleteFile(defaultSaveFilePath);
			}

			if (bSavedFileExists)
			{
				DeleteFile(savedSaveFilePath);
			}
		}

		m_bUsingSaveFile = false;
	}

	JSONObject BaseScene::SerializePointLight(PointLight& pointLight)
	{
		JSONObject object;

		std::string posStr = Vec3ToString(pointLight.position, 3);
		object.fields.emplace_back("pos", JSONValue(posStr));

		std::string colorStr = Vec3ToString(pointLight.color, 2);
		object.fields.emplace_back("color", JSONValue(colorStr));

		object.fields.emplace_back("enabled", JSONValue(pointLight.enabled != 0));
		object.fields.emplace_back("brightness", JSONValue(pointLight.brightness));

		return object;
	}

	JSONObject BaseScene::SerializeDirectionalLight(DirectionalLight& directionalLight)
	{
		JSONObject object;

		glm::vec3 dirLightDir = glm::eulerAngles(directionalLight.rotation);
		std::string dirStr = Vec3ToString(dirLightDir, 3);
		object.fields.emplace_back("rotation", JSONValue(dirStr));

		std::string posStr = Vec3ToString(directionalLight.position, 3);
		object.fields.emplace_back("pos", JSONValue(posStr));

		std::string colorStr = Vec3ToString(directionalLight.color, 2);
		object.fields.emplace_back("color", JSONValue(colorStr));

		object.fields.emplace_back("enabled", JSONValue(directionalLight.enabled != 0));
		object.fields.emplace_back("brightness", JSONValue(directionalLight.brightness));

		object.fields.emplace_back("cast shadows", JSONValue(directionalLight.bCastShadow));
		object.fields.emplace_back("shadow darkness", JSONValue(directionalLight.shadowDarkness));
		object.fields.emplace_back("shadow map near", JSONValue(directionalLight.shadowMapNearPlane));
		object.fields.emplace_back("shadow map far", JSONValue(directionalLight.shadowMapFarPlane));
		object.fields.emplace_back("shadow map zoom", JSONValue(directionalLight.shadowMapZoom));

		return object;
	}

	GameObject* BaseScene::AddRootObject(GameObject* gameObject)
	{
		if (!gameObject)
		{
			return nullptr;
		}

		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject == gameObject)
			{
				PrintWarn("Attempted to add same root object (%s) to scene more than once\n", rootObject->m_Name.c_str());
				return nullptr;
			}
		}

		gameObject->SetParent(nullptr);
		m_RootObjects.push_back(gameObject);
		UpdateRootObjectSiblingIndices();

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
				UpdateRootObjectSiblingIndices();
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
		m_RootObjects.clear();
	}

	std::vector<MaterialID> BaseScene::GetMaterialIDs()
	{
		return m_LoadedMaterials;
	}

	void BaseScene::AddMaterialID(MaterialID newMaterialID)
	{
		m_LoadedMaterials.push_back(newMaterialID);
	}

	void BaseScene::RemoveMaterialID(MaterialID materialID)
	{
		auto iter = m_LoadedMaterials.begin();
		while (iter != m_LoadedMaterials.end())
		{
			Material& material = g_Renderer->GetMaterial(*iter);
			MaterialID matID;
			if (g_Renderer->GetMaterialID(material.name, matID))
			{
				if (matID == materialID)
				{
					// TODO: Find all objects which use this material and give them new mats
					iter = m_LoadedMaterials.erase(iter);
				}
				else
				{
					++iter;
				}
			}
			else
			{
				++iter;
			}
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
		for (GameObject* rootObject : m_RootObjects)
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
		return RESOURCE_LOCATION  "scenes/default/" + m_FileName;
	}

	std::string BaseScene::GetRelativeFilePath() const
	{
		if (m_bUsingSaveFile)
		{
			return RESOURCE_LOCATION  "scenes/saved/" + m_FileName;
		}
		else
		{
			return RESOURCE_LOCATION  "scenes/default/" + m_FileName;
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
