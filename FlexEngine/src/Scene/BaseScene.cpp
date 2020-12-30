#include "stdafx.hpp"

#include "Scene/BaseScene.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

#include <glm/gtx/norm.hpp> // for distance2
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Callbacks/GameObjectCallbacks.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Editor.hpp"
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
#include "Platform/Platform.hpp"
#include "ResourceManager.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "StringBuilder.hpp"
#include "Track/BezierCurve3D.hpp"

namespace flex
{
	std::map<StringID, std::string> BaseScene::GameObjectTypeStringIDPairs;

	BaseScene::BaseScene(const std::string& fileName) :
		m_FileName(fileName),
		m_TrackManager(this),
		m_CartManager(this)
	{
		// Default day sky
		m_SkyboxData = {};
		m_SkyboxData.top = glm::pow(glm::vec4(0.22f, 0.58f, 0.88f, 0.0f), glm::vec4(2.2f));
		m_SkyboxData.mid = glm::pow(glm::vec4(0.66f, 0.86f, 0.95f, 0.0f), glm::vec4(2.2f));
		m_SkyboxData.btm = glm::pow(glm::vec4(0.75f, 0.91f, 0.99f, 0.0f), glm::vec4(2.2f));
	}

	BaseScene::~BaseScene()
	{
	}

	void BaseScene::Initialize()
	{
		if (!m_bInitialized)
		{
			if (m_PhysicsWorld)
			{
				PrintWarn("Scene is being initialized more than once!\n");
			}

			// Physics world
			m_PhysicsWorld = new PhysicsWorld();
			m_PhysicsWorld->Initialize();

			m_PhysicsWorld->GetWorld()->setGravity({ 0.0f, -9.81f, 0.0f });

			m_CartManager.Initialize();

			const std::string filePath = SCENE_DEFAULT_DIRECTORY + m_FileName;

			if (FileExists(filePath))
			{
				LoadFromFile(filePath);
			}
			else
			{
				CreateBlank(filePath);
			}

			if (m_bSpawnPlayer)
			{
				m_Player0 = new Player(0, m_PlayerGUIDs[0]);
				m_Player0->GetTransform()->SetWorldPosition(glm::vec3(0.0f, 2.0f, 0.0f));
				AddRootObjectImmediate(m_Player0);

				//m_Player1 = new Player(1, m_PlayerGUIDs[1]);
				//AddRootObjectImmediate(m_Player1);
			}

			for (GameObject* rootObject : m_RootObjects)
			{
				rootObject->Initialize();
			}

			UpdateRootObjectSiblingIndices();

			m_bInitialized = true;

			m_bLoaded = true;
		}

		ReadGameObjectTypesFile();

		// All updating to new file version should be complete by this point
		m_SceneFileVersion = LATEST_SCENE_FILE_VERSION;
		m_MaterialsFileVersion = LATEST_MATERIALS_FILE_VERSION;
		m_MeshesFileVersion = LATEST_MESHES_FILE_VERSION;
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
		m_bInitialized = false;

		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject)
			{
				rootObject->Destroy();
				delete rootObject;
			}
		}
		m_RootObjects.clear();
		m_GameObjectLUT.clear();

		m_TrackManager.Destroy();
		m_CartManager.Destroy();

		m_PendingDestroyObjects.clear();
		m_PendingRemoveObjects.clear();
		m_PendingAddObjects.clear();
		m_PendingAddChildObjects.clear();

		m_OnGameObjectDestroyedCallbacks.clear();

		g_Renderer->SetSkyboxMesh(nullptr);
		g_Renderer->RemoveDirectionalLight();
		g_Renderer->RemoveAllPointLights();

		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Destroy();
			delete m_PhysicsWorld;
			m_PhysicsWorld = nullptr;
		}

		m_Player0 = nullptr;
		m_Player1 = nullptr;
	}

	void BaseScene::Update()
	{
		PROFILE_BEGIN("Update Scene");

		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Update(g_DeltaTime);
		}

		m_CartManager.Update();
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_Z))
		{
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_X))
		{
			AudioManager::PauseSource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}

		{
			PROFILE_AUTO("Tick scene objects");
			for (GameObject* rootObject : m_RootObjects)
			{
				if (rootObject)
				{
					rootObject->Update();
				}
			}
		}

		m_TrackManager.DrawDebug();

		PROFILE_END("Update Scene");
	}

	void BaseScene::LateUpdate()
	{
		if (!m_PendingRemoveObjects.empty())
		{
			for (const GameObjectID& objectID : m_PendingRemoveObjects)
			{
				RemoveObjectImmediate(objectID, false);
			}
			m_PendingRemoveObjects.clear();
			UpdateRootObjectSiblingIndices();
			g_Renderer->RenderObjectStateChanged();
		}

		if (!m_PendingDestroyObjects.empty())
		{
			for (const GameObjectID& objectID : m_PendingDestroyObjects)
			{
				RemoveObjectImmediate(objectID, true);
			}
			m_PendingDestroyObjects.clear();
			UpdateRootObjectSiblingIndices();
			g_Renderer->RenderObjectStateChanged();
		}

		if (!m_PendingAddObjects.empty())
		{
			for (GameObject* object : m_PendingAddObjects)
			{
				AddRootObjectImmediate(object);
			}
			m_PendingAddObjects.clear();
			UpdateRootObjectSiblingIndices();
			g_Renderer->RenderObjectStateChanged();
		}

		if (!m_PendingAddChildObjects.empty())
		{
			for (const Pair<GameObject*, GameObject*>& objectPair : m_PendingAddChildObjects)
			{
				objectPair.first->AddChildImmediate(objectPair.second);
				RegisterGameObject(objectPair.second);
			}
			m_PendingAddChildObjects.clear();
			UpdateRootObjectSiblingIndices();
			g_Renderer->RenderObjectStateChanged();
		}
	}

	bool BaseScene::LoadFromFile(const std::string& filePath)
	{
		if (!FileExists(filePath))
		{
			return false;
		}

		if (g_bEnableLogging_Loading)
		{
			Print("Loading scene from %s\n", filePath.c_str());
		}

		JSONObject sceneRootObject;
		if (!JSONParser::ParseFromFile(filePath, sceneRootObject))
		{
			PrintError("Failed to parse scene file: %s\n\terror: %s\n", filePath.c_str(), JSONParser::GetErrorString());
			return false;
		}

		constexpr bool printSceneContentsToConsole = false;
		if (printSceneContentsToConsole)
		{
			Print("Parsed scene file:\n");
			PrintLong(sceneRootObject.Print(0).c_str());
		}

		if (!sceneRootObject.SetIntChecked("version", m_SceneFileVersion))
		{
			m_SceneFileVersion = 1;
			PrintError("Scene version missing from scene file. Assuming version 1\n");
		}

		sceneRootObject.SetStringChecked("name", m_Name);

		// TODO: Serialize player's game object ID
		sceneRootObject.SetBoolChecked("spawn player", m_bSpawnPlayer);

		if (!sceneRootObject.SetGUIDChecked("player 0 guid", m_PlayerGUIDs[0]))
		{
			m_PlayerGUIDs[0] = InvalidGameObjectID;
		}
		if (!sceneRootObject.SetGUIDChecked("player 1 guid", m_PlayerGUIDs[1]))
		{
			m_PlayerGUIDs[1] = InvalidGameObjectID;
		}

		JSONObject skyboxDataObj;
		if (sceneRootObject.SetObjectChecked("skybox data", skyboxDataObj))
		{
			// TODO: Add SetGammaColourChecked
			if (skyboxDataObj.SetVec4Checked("top colour", m_SkyboxData.top))
			{
				m_SkyboxData.top = glm::pow(m_SkyboxData.top, glm::vec4(2.2f));
			}
			if (skyboxDataObj.SetVec4Checked("mid colour", m_SkyboxData.mid))
			{
				m_SkyboxData.mid = glm::pow(m_SkyboxData.mid, glm::vec4(2.2f));
			}
			if (skyboxDataObj.SetVec4Checked("btm colour", m_SkyboxData.btm))
			{
				m_SkyboxData.btm = glm::pow(m_SkyboxData.btm, glm::vec4(2.2f));
			}
		}

		JSONObject cameraObj;
		if (sceneRootObject.SetObjectChecked("camera", cameraObj))
		{
			std::string camType;
			if (cameraObj.SetStringChecked("type", camType))
			{
				if (camType.compare("terminal") == 0)
				{
					g_CameraManager->PushCameraByName(camType, true, false);

				}
				else
				{
					g_CameraManager->SetCameraByName(camType, true);
				}
			}

			BaseCamera* cam = g_CameraManager->CurrentCamera();

			real zNear, zFar, fov, aperture, shutterSpeed, lightSensitivity, exposure, moveSpeed;
			if (cameraObj.SetFloatChecked("near plane", zNear)) cam->zNear = zNear;
			if (cameraObj.SetFloatChecked("far plane", zFar)) cam->zFar = zFar;
			if (cameraObj.SetFloatChecked("fov", fov)) cam->FOV = fov;
			if (cameraObj.SetFloatChecked("aperture", aperture)) cam->aperture = aperture;
			if (cameraObj.SetFloatChecked("shutter speed", shutterSpeed)) cam->shutterSpeed = shutterSpeed;
			if (cameraObj.SetFloatChecked("light sensitivity", lightSensitivity)) cam->lightSensitivity = lightSensitivity;
			if (cameraObj.SetFloatChecked("exposure", exposure)) cam->exposure = exposure;
			if (cameraObj.SetFloatChecked("move speed", moveSpeed)) cam->moveSpeed = moveSpeed;

			if (cameraObj.HasField("aperture"))
			{
				cam->aperture = cameraObj.GetFloat("aperture");
				cam->shutterSpeed = cameraObj.GetFloat("shutter speed");
				cam->lightSensitivity = cameraObj.GetFloat("light sensitivity");
			}

			JSONObject cameraTransform;
			if (cameraObj.SetObjectChecked("transform", cameraTransform))
			{
				glm::vec3 camPos = ParseVec3(cameraTransform.GetString("position"));
				if (IsNanOrInf(camPos))
				{
					PrintError("Camera pos was saved out as nan or inf, resetting to 0\n");
					camPos = VEC3_ZERO;
				}
				cam->position = camPos;

				real camPitch = cameraTransform.GetFloat("pitch");
				if (IsNanOrInf(camPitch))
				{
					PrintError("Camera pitch was saved out as nan or inf, resetting to 0\n");
					camPitch = 0.0f;
				}
				cam->pitch = camPitch;

				real camYaw = cameraTransform.GetFloat("yaw");
				if (IsNanOrInf(camYaw))
				{
					PrintError("Camera yaw was saved out as nan or inf, resetting to 0\n");
					camYaw = 0.0f;
				}
				cam->yaw = camYaw;
			}

			cam->CalculateExposure();
		}

		// This holds all the objects in the scene which do not have a parent
		const std::vector<JSONObject>& rootObjects = sceneRootObject.GetObjectArray("objects");
		for (const JSONObject& rootObjectJSON : rootObjects)
		{
			GameObject* rootObj = GameObject::CreateObjectFromJSON(rootObjectJSON, this, m_SceneFileVersion);
			if (rootObj != nullptr)
			{
				rootObj->GetTransform()->UpdateParentTransform();
				AddRootObjectImmediate(rootObj);
			}
			else
			{
				std::string firstFieldName = rootObjectJSON.fields.empty() ? std::string() : rootObjectJSON.fields[0].Print(0);
				PrintError("Failed to create game object from JSON object with first field %s\n", firstFieldName.c_str());
			}
		}

		if (sceneRootObject.HasField("track manager"))
		{
			const JSONObject& trackManagerObj = sceneRootObject.GetObject("track manager");

			m_TrackManager.InitializeFromJSON(trackManagerObj);
		}

		return true;
	}

	void BaseScene::CreateBlank(const std::string& filePath)
	{
		if (g_bEnableLogging_Loading)
		{
			Print("Creating blank scene at: %s\n", filePath.c_str());
		}

		// Skybox
		{
			MaterialID skyboxMatID = InvalidMaterialID;
			g_Renderer->FindOrCreateMaterialByName("skybox 01", skyboxMatID);
			assert(skyboxMatID != InvalidMaterialID);

			Skybox* skybox = new Skybox("Skybox");
			skybox->ProcedurallyInitialize(skyboxMatID);

			AddRootObjectImmediate(skybox);
		}

#if 0
		// Reflection probe
		{
			MaterialID sphereMatID = InvalidMaterialID;
			g_Renderer->FindOrCreateMaterialByName("pbr chrome", sphereMatID);
			assert(sphereMatID != InvalidMaterialID);

			ReflectionProbe* reflectionProbe = new ReflectionProbe("Reflection Probe 01");

			JSONObject emptyObj = {};
			reflectionProbe->ParseJSON(emptyObj, this, sphereMatID);
			reflectionProbe->GetTransform()->SetLocalPosition(glm::vec3(0.0f, 8.0f, 0.0f));

			AddRootObjectImmediate(reflectionProbe);
		}
#endif

		// Default directional light
		DirectionalLight* dirLight = new DirectionalLight();
		g_SceneManager->CurrentScene()->AddRootObjectImmediate(dirLight);
		dirLight->SetRot(glm::quat(glm::vec3(130.0f, -65.0f, 120.0f)));
		dirLight->SetPos(glm::vec3(0.0f, 15.0f, 0.0f));
		dirLight->data.brightness = 3.0f;
		dirLight->Initialize();
		dirLight->PostInitialize();
	}

	void BaseScene::DrawImGuiObjects()
	{
		ImGui::Checkbox("Spawn player", &m_bSpawnPlayer);

		ImGuiExt::ColorEdit3Gamma("Top", &m_SkyboxData.top.x);
		ImGuiExt::ColorEdit3Gamma("Mid", &m_SkyboxData.mid.x);
		ImGuiExt::ColorEdit3Gamma("Bottom", &m_SkyboxData.btm.x);

		DoSceneContextMenu();
	}

	void BaseScene::DoSceneContextMenu()
	{
		bool bClicked = ImGui::IsMouseReleased(1) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

		std::string contextMenuID = "scene context menu " + m_FileName;
		if (ImGui::BeginPopupContextItem(contextMenuID.c_str()))
		{
			{
				const i32 sceneNameMaxCharCount = 256;

				// We don't know the names of scene's that haven't been loaded
				if (m_bLoaded)
				{
					static char newSceneName[sceneNameMaxCharCount];
					if (bClicked)
					{
						strcpy(newSceneName, m_Name.c_str());
					}

					bool bRenameScene = ImGui::InputText("##rename-scene",
						newSceneName,
						sceneNameMaxCharCount,
						ImGuiInputTextFlags_EnterReturnsTrue);

					ImGui::SameLine();

					bRenameScene |= ImGui::Button("Rename scene");

					if (bRenameScene)
					{
						SetName(newSceneName);
						// Don't close popup here since we will likely want to save that change
					}
				}

				static char newSceneFileName[sceneNameMaxCharCount];
				if (bClicked)
				{
					strcpy(newSceneFileName, m_FileName.c_str());
				}

				bool bRenameSceneFileName = ImGui::InputText("##rename-scene-file-name",
					newSceneFileName,
					sceneNameMaxCharCount,
					ImGuiInputTextFlags_EnterReturnsTrue);

				ImGui::SameLine();

				bRenameSceneFileName |= ImGui::Button("Rename file");

				if (bRenameSceneFileName)
				{
					std::string newSceneFileNameStr(newSceneFileName);
					if (newSceneFileNameStr.compare(m_FileName) != 0)
					{
						std::string fileDir = ExtractDirectoryString(RelativePathToAbsolute(GetDefaultRelativeFilePath()));
						std::string newSceneFilePath = fileDir + newSceneFileNameStr;
						bool bNameEmpty = newSceneFileNameStr.empty();
						bool bCorrectFileType = EndsWith(newSceneFileNameStr, ".json");
						bool bFileExists = FileExists(newSceneFilePath);
						bool bSceneNameValid = (!bNameEmpty && bCorrectFileType && !bFileExists);

						if (bSceneNameValid)
						{
							if (SetFileName(newSceneFileNameStr, true))
							{
								ImGui::CloseCurrentPopup();
							}
						}
						else
						{
							PrintError("Attempted to set scene name to invalid name: %s\n", newSceneFileNameStr.c_str());
							if (bNameEmpty)
							{
								PrintError("(file name is empty!)\n");
							}
							else if (!bCorrectFileType)
							{
								PrintError("(must end with \".json\"!)\n");
							}
							else if (bFileExists)
							{
								PrintError("(file already exists!)\n");
							}
						}
					}
				}
			}

			// Only allow current scene to be saved
			if (g_SceneManager->CurrentScene() == this)
			{
				if (ImGui::Button("Save"))
				{
					SerializeToFile(false);

					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);

				if (ImGui::Button("Save over default"))
				{
					SerializeToFile(true);

					ImGui::CloseCurrentPopup();
				}

				if (IsUsingSaveFile())
				{
					ImGui::SameLine();

					if (ImGui::Button("Hard reload (deletes save file!)"))
					{
						Platform::DeleteFile(GetRelativeFilePath());
						g_SceneManager->ReloadCurrentScene();

						ImGui::CloseCurrentPopup();
					}
				}

				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
			}

			static const char* duplicateScenePopupLabel = "Duplicate scene";
			const i32 sceneNameMaxCharCount = 256;
			static char newSceneName[sceneNameMaxCharCount];
			static char newSceneFileName[sceneNameMaxCharCount];
			if (ImGui::Button("Duplicate..."))
			{
				ImGui::OpenPopup(duplicateScenePopupLabel);

				std::string newSceneNameStr = m_Name;
				newSceneNameStr += " Copy";
				strcpy(newSceneName, newSceneNameStr.c_str());

				std::string newSceneFileNameStr = StripFileType(m_FileName);

				bool bValidName = false;
				do
				{
					i16 numNumericalChars = 0;
					i32 numEndingWith = GetNumberEndingWith(newSceneFileNameStr, numNumericalChars);
					if (numNumericalChars > 0)
					{
						u32 charsBeforeNum = (u32)(newSceneFileNameStr.length() - numNumericalChars);
						newSceneFileNameStr = newSceneFileNameStr.substr(0, charsBeforeNum) +
							IntToString(numEndingWith + 1, numNumericalChars);
					}
					else
					{
						newSceneFileNameStr += "_01";
					}

					std::string filePathFrom = RelativePathToAbsolute(GetDefaultRelativeFilePath());
					std::string fullNewFilePath = ExtractDirectoryString(filePathFrom);
					fullNewFilePath += newSceneFileNameStr + ".json";
					bValidName = !FileExists(fullNewFilePath);
				} while (!bValidName);

				newSceneFileNameStr += ".json";

				strcpy(newSceneFileName, newSceneFileNameStr.c_str());
			}

			bool bCloseContextMenu = false;
			if (ImGui::BeginPopupModal(duplicateScenePopupLabel,
				NULL,
				ImGuiWindowFlags_AlwaysAutoResize))
			{

				bool bDuplicateScene = ImGui::InputText("Name##duplicate-scene-name",
					newSceneName,
					sceneNameMaxCharCount,
					ImGuiInputTextFlags_EnterReturnsTrue);

				bDuplicateScene |= ImGui::InputText("File name##duplicate-scene-file-path",
					newSceneFileName,
					sceneNameMaxCharCount,
					ImGuiInputTextFlags_EnterReturnsTrue);

				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				bDuplicateScene |= ImGui::Button("Duplicate");

				bool bValidInput = true;

				if (strlen(newSceneName) == 0 ||
					strlen(newSceneFileName) == 0 ||
					!EndsWith(newSceneFileName, ".json"))
				{
					bValidInput = false;
				}

				if (bDuplicateScene && bValidInput)
				{
					if (g_SceneManager->DuplicateScene(this, newSceneFileName, newSceneName))
					{
						bCloseContextMenu = true;
					}
				}

				if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Open in explorer"))
			{
				const std::string directory = RelativePathToAbsolute(ExtractDirectoryString(GetRelativeFilePath()));
				Platform::OpenExplorer(directory);
			}

			ImGui::SameLine();

			const char* deleteScenePopupID = "Delete scene";

			ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);

			if (ImGui::Button("Delete scene..."))
			{
				ImGui::OpenPopup(deleteScenePopupID);
			}

			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();

			if (ImGui::BeginPopupModal(deleteScenePopupID, NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, g_WarningTextColour);
				std::string textStr = "Are you sure you want to permanently delete " + m_Name + "? (both the default & saved files)";
				ImGui::Text("%s", textStr.c_str());
				ImGui::PopStyleColor();

				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);
				if (ImGui::Button("Delete"))
				{
					g_SceneManager->DeleteScene(this);

					ImGui::CloseCurrentPopup();

					bCloseContextMenu = true;
				}
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();

				if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			if (bCloseContextMenu)
			{
				ImGui::CloseCurrentPopup();
			}

			if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	bool BaseScene::IsLoaded() const
	{
		return m_bLoaded;
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

	std::vector<GameObjectID> BaseScene::GetAllObjectIDs()
	{
		std::vector<GameObjectID> result;

		for (GameObject* obj : m_RootObjects)
		{
			obj->AddSelfIDAndChildrenToVec(result);
		}

		return result;
	}

	TrackManager* BaseScene::GetTrackManager()
	{
		return &m_TrackManager;
	}

	CartManager* BaseScene::GetCartManager()
	{
		return &m_CartManager;
	}

	std::string BaseScene::GetUniqueObjectName(const std::string& existingName)
	{
		const std::vector<GameObject*> allObjects = GetAllObjects();

		i16 digits;
		i32 existingIndex = GetNumberEndingWith(existingName, digits);

		std::string prefix = existingName.substr(0, existingName.length() - digits);

		i32 newIndex = existingIndex + 1;
		bool bNameTaken = true;
		std::string name;
		while (bNameTaken)
		{
			name.clear();
			bNameTaken = false;
			std::string suffix = IntToString(newIndex, digits);
			name = prefix + suffix;
			for (const GameObject* gameObject : allObjects)
			{
				if (gameObject->GetName().compare(name) == 0)
				{
					bNameTaken = true;
					++newIndex;
					break;
				}
			}
			for (const GameObject* gameObject : m_PendingAddObjects)
			{
				if (gameObject->GetName() == name)
				{
					bNameTaken = true;
					++newIndex;
					break;
				}
			}
			for (const Pair<GameObject*, GameObject*>& gameObjectPair : m_PendingAddChildObjects)
			{
				if (gameObjectPair.second->GetName() == name)
				{
					bNameTaken = true;
					++newIndex;
					break;
				}
			}
		}

		return name;
	}

	std::string BaseScene::GetUniqueObjectName(const std::string& prefix, i16 digits)
	{
		const std::vector<GameObject*> allObjects = GetAllObjects();

		i32 newIndex = 0;
		bool bNameTaken = true;
		while (bNameTaken)
		{
			bNameTaken = false;
			std::string suffix = IntToString(newIndex, digits);
			std::string name = prefix + suffix;
			for (const GameObject* gameObject : allObjects)
			{
				if (gameObject->GetName() == name)
				{
					bNameTaken = true;
					++newIndex;
					break;
				}
			}
			for (const GameObject* gameObject : m_PendingAddObjects)
			{
				if (gameObject->GetName() == name)
				{
					bNameTaken = true;
					++newIndex;
					break;
				}
			}
			for (const Pair<GameObject*, GameObject*>& gameObjectPair : m_PendingAddChildObjects)
			{
				if (gameObjectPair.second->GetName() == name)
				{
					bNameTaken = true;
					++newIndex;
					break;
				}
			}
		}

		return prefix + IntToString(newIndex, digits);
	}

	i32 BaseScene::GetSceneFileVersion() const
	{
		return m_SceneFileVersion;
	}

	bool BaseScene::HasPlayers() const
	{
		return m_bSpawnPlayer;
	}

	const SkyboxData& BaseScene::GetSkyboxData() const
	{
		return m_SkyboxData;
	}

	void BaseScene::DrawImGuiForSelectedObjects()
	{
		ImGui::NewLine();

		ImGui::BeginChild("SelectedObject", ImVec2(0.0f, 500.0f), true);

		if (g_Editor->HasSelectedObject())
		{
			// TODO: Draw common fields for all selected objects?
			GameObject* selectedObject = GetGameObject(g_Editor->GetFirstSelectedObjectID());
			if (selectedObject)
			{
				selectedObject->DrawImGuiObjects();
			}
		}

		ImGui::EndChild();
	}

	void BaseScene::DrawImGuiForRenderObjectsList()
	{
		ImGui::NewLine();

		ImGui::Text("Game Objects");

		// Dropping objects onto this text makes them root objects
		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Editor::GameObjectPayloadCStr);

			if (payload && payload->Data)
			{
				i32 draggedObjectCount = payload->DataSize / sizeof(GameObjectID);

				std::vector<GameObjectID> draggedGameObjectsIDs;
				draggedGameObjectsIDs.reserve(draggedObjectCount);
				for (i32 i = 0; i < draggedObjectCount; ++i)
				{
					draggedGameObjectsIDs.push_back(*((GameObjectID*)payload->Data + i));
				}

				if (!draggedGameObjectsIDs.empty())
				{
					for (const GameObjectID& draggedGameObjectID : draggedGameObjectsIDs)
					{
						GameObject* draggedGameObject = GetGameObject(draggedGameObjectID);
						GameObject* parent = draggedGameObject->GetParent();
						GameObjectID parentID = parent != nullptr ? parent->ID : InvalidGameObjectID;
						bool bParentInSelection = parentID.IsValid() ? (Find(draggedGameObjectsIDs, parentID) != draggedGameObjectsIDs.end()) : false;
						// Make all non-root objects whose parents aren't being moved root objects (but leave sub-hierarchy as is)
						if (!bParentInSelection && parent)
						{
							parent->RemoveChildImmediate(draggedGameObject->ID, false);
							g_SceneManager->CurrentScene()->AddRootObject(draggedGameObject);
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();
		for (GameObject* rootObject : rootObjects)
		{
			if (DrawImGuiGameObjectNameAndChildren(rootObject))
			{
				break;
			}
		}

		DoCreateGameObjectButton("Add object...", "Add object");

		const bool bShowAddPointLightBtn = g_Renderer->GetNumPointLights() < MAX_POINT_LIGHT_COUNT;
		if (bShowAddPointLightBtn)
		{
			if (ImGui::Button("Add point light"))
			{
				BaseScene* scene = g_SceneManager->CurrentScene();
				PointLight* newPointLight = new PointLight(scene);
				scene->AddRootObject(newPointLight);
				newPointLight->Initialize();
				newPointLight->PostInitialize();

				g_Editor->SetSelectedObject(newPointLight->ID);
			}
		}

		const bool bShowAddDirLightBtn = g_Renderer->GetDirectionalLight() == nullptr;
		if (bShowAddDirLightBtn)
		{
			if (bShowAddPointLightBtn)
			{
				ImGui::SameLine();
			}

			if (ImGui::Button("Add directional light"))
			{
				BaseScene* scene = g_SceneManager->CurrentScene();
				DirectionalLight* newDiright = new DirectionalLight();
				scene->AddRootObject(newDiright);
				newDiright->Initialize();
				newDiright->PostInitialize();

				g_Editor->SetSelectedObject(newDiright->ID);
			}
		}
	}

	void BaseScene::DoCreateGameObjectButton(const char* buttonName, const char* popupName)
	{
		static const char* defaultNewNameBase = "New_Object_";

		static std::string newObjectName;

		if (ImGui::Button(buttonName))
		{
			ImGui::OpenPopup(popupName);

			m_NewObjectTypeIDPair.first = SID("object");
			m_NewObjectTypeIDPair.second = GameObjectTypeStringIDPairs[SID("object")];

			i32 highestNoNameObj = -1;
			i16 maxNumChars = 2;
			const std::vector<GameObject*> allObjects = g_SceneManager->CurrentScene()->GetAllObjects();
			for (GameObject* gameObject : allObjects)
			{
				if (StartsWith(gameObject->GetName(), defaultNewNameBase))
				{
					i16 numChars;
					i32 num = GetNumberEndingWith(gameObject->GetName(), numChars);
					if (num != -1)
					{
						highestNoNameObj = glm::max(highestNoNameObj, num);
						maxNumChars = glm::max(maxNumChars, maxNumChars);
					}
				}
			}
			newObjectName = defaultNewNameBase + IntToString(highestNoNameObj + 1, maxNumChars);
		}

		if (ImGui::BeginPopupModal(popupName, NULL,
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings))
		{
			const size_t maxStrLen = 256;
			newObjectName.resize(maxStrLen);

			bool bCreate = ImGui::InputText("##new-object-name",
				(char*)newObjectName.data(),
				maxStrLen,
				ImGuiInputTextFlags_EnterReturnsTrue);

			// SetItemDefaultFocus doesn't work here for some reason
			if (ImGui::IsWindowAppearing())
			{
				ImGui::SetKeyboardFocusHere();
			}

			if (ImGui::BeginCombo("Type", m_NewObjectTypeIDPair.second.c_str()))
			{
				for (const auto& typeIDPair : GameObjectTypeStringIDPairs)
				{
					bool bSelected = (typeIDPair.first == m_NewObjectTypeIDPair.first);
					if (ImGui::Selectable(typeIDPair.second.c_str(), &bSelected))
					{
						m_NewObjectTypeIDPair.first = typeIDPair.first;
						m_NewObjectTypeIDPair.second = typeIDPair.second;
					}
				}

				ImGui::EndCombo();
			}

			static std::string newObjectType;

			const char* newTypePopupStr = "New Object Type";
			if (ImGui::Button("New Type"))
			{
				ImGui::OpenPopup(newTypePopupStr);
				newObjectType.clear();
				newObjectType.resize(maxStrLen);
			}

			if (ImGui::BeginPopupModal(newTypePopupStr, NULL,
				ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoNavInputs))
			{
				bool bCreateType = false;
				if (ImGui::InputText("Type", (char*)newObjectType.data(), maxStrLen, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					bCreateType = true;
				}

				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button("Create") || bCreateType)
				{
					// Remove excess trailing \0 chars
					newObjectType = std::string(newObjectType.c_str());

					if (!newObjectType.empty())
					{
						StringID newTypeID = Hash(newObjectType.c_str());
						GameObjectTypeStringIDPairs.emplace(newTypeID, newObjectType);

						WriteGameObjectTypesFile();

						ImGui::CloseCurrentPopup();
					}
				}

				ImGui::EndPopup();
			}

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			bCreate |= ImGui::Button("Create");

			bool bInvalidName = std::string(newObjectName.c_str()).empty();

			if (bCreate && !bInvalidName)
			{
				// Remove excess trailing \0 chars
				newObjectName = std::string(newObjectName.c_str());

				if (!newObjectName.empty())
				{
					GameObject* parent = nullptr;

					if (g_Editor->HasSelectedObject())
					{
						parent = GetGameObject(g_Editor->GetFirstSelectedObjectID());
					}

					CreateNewObject(newObjectName, parent);

					ImGui::CloseCurrentPopup();
				}
			}

			if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	void BaseScene::CreateNewObject(const std::string& newObjectName, GameObject* parent /* = nullptr */)
	{
		GameObject* newGameObject = GameObject::CreateObjectOfType(this, m_NewObjectTypeIDPair.first, newObjectName);

		// Special case handling
		switch (m_NewObjectTypeIDPair.first)
		{
		case SID("object"):
		{
			Mesh* mesh = newGameObject->SetMesh(new Mesh(newGameObject));
			mesh->LoadFromFile(MESH_DIRECTORY "cube.glb", g_Renderer->GetPlaceholderMaterialID());
		} break;
		case SID("socket"):
		{
			std::string socketName = g_SceneManager->CurrentScene()->GetUniqueObjectName("socket_", 3);

			u32 socketIndex = 0;
			if (parent != nullptr)
			{
				socketIndex = (u32)parent->sockets.size();
			}

			g_PluggablesSystem->AddSocket((Socket*)newGameObject, socketIndex);
		} break;
		};

		newGameObject->Initialize();
		newGameObject->PostInitialize();

		if (parent != nullptr)
		{
			g_SceneManager->CurrentScene()->AddChildObjectImmediate(parent, newGameObject);
		}
		else
		{
			g_SceneManager->CurrentScene()->AddRootObjectImmediate(newGameObject);
		}

		g_Editor->SetSelectedObject(newGameObject->ID);
	}

	bool BaseScene::DrawImGuiGameObjectNameAndChildren(GameObject* gameObject)
	{
		if (!gameObject->IsVisibleInSceneExplorer())
		{
			return false;
		}

		ImGui::PushID(gameObject);

		bool result = DrawImGuiGameObjectNameAndChildrenInternal(gameObject);

		ImGui::PopID();

		return result;
	}

	bool BaseScene::DrawImGuiGameObjectNameAndChildrenInternal(GameObject* gameObject)
	{
		// ImGui::PushID will have been called so ImGui calls in this function don't need to be qualified to be unique

		bool bParentChildTreeDirty = false;

		std::string objectName = gameObject->GetName();

		bool bForceTreeNodeOpen = false;

		const std::vector<GameObject*>& gameObjectChildren = gameObject->GetChildren();
		bool bHasChildren = !gameObjectChildren.empty();
		if (bHasChildren)
		{
			bool bChildVisibleInSceneExplorer = false;
			// Make sure at least one child is visible in scene explorer
			for (GameObject* child : gameObjectChildren)
			{
				if (child->IsVisibleInSceneExplorer(true))
				{
					bChildVisibleInSceneExplorer = true;
				}
				if (child->SelfOrChildIsSelected())
				{
					bForceTreeNodeOpen = true;
				}
			}

			if (!bChildVisibleInSceneExplorer)
			{
				bHasChildren = false;
			}
		}
		bool bSelected = g_Editor->IsObjectSelected(gameObject->ID);

		bool bVisible = gameObject->IsVisible();
		if (ImGui::Checkbox("##visible", &bVisible))
		{
			gameObject->SetVisible(bVisible);
		}
		ImGui::SameLine();

		ImGuiTreeNodeFlags node_flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_OpenOnDoubleClick |
			(bSelected ? ImGuiTreeNodeFlags_Selected : 0);

		if (!bHasChildren)
		{
			node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}

		if (bForceTreeNodeOpen)
		{
			ImGui::SetNextTreeNodeOpen(true);
		}

		bool bNodeOpen = ImGui::TreeNodeEx((void*)gameObject, node_flags, "%s", objectName.c_str());

		bool bGameObjectDeletedOrDuplicated = gameObject->DoImGuiContextMenu(false);
		if (bGameObjectDeletedOrDuplicated || gameObject == nullptr)
		{
			bParentChildTreeDirty = true;
		}
		else
		{
			// TODO: Remove from renderer class
			if (ImGui::IsMouseReleased(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_None))
			{
				if (g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_CONTROL))
				{
					g_Editor->ToggleSelectedObject(gameObject->ID);
				}
				else if (g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT))
				{
					const std::vector<GameObjectID>& selectedObjectIDs = g_Editor->GetSelectedObjectIDs();
					if (selectedObjectIDs.empty() ||
						(selectedObjectIDs.size() == 1 && selectedObjectIDs[0] == gameObject->ID))
					{
						g_Editor->ToggleSelectedObject(gameObject->ID);
					}
					else
					{
						std::vector<GameObject*> objectsToSelect;

						GameObject* objectA = GetGameObject(selectedObjectIDs[selectedObjectIDs.size() - 1]);
						GameObject* objectB = gameObject;

						objectA->AddSelfAndChildrenToVec(objectsToSelect);
						objectB->AddSelfAndChildrenToVec(objectsToSelect);

						if (objectA->GetParent() == objectB->GetParent() &&
							objectA != objectB)
						{
							// Ensure A comes before B
							if (objectA->GetSiblingIndex() > objectB->GetSiblingIndex())
							{
								std::swap(objectA, objectB);
							}

							const std::vector<GameObject*>& objectALaterSiblings = objectA->GetLaterSiblings();
							auto objectBIter = Find(objectALaterSiblings, objectB);
							assert(objectBIter != objectALaterSiblings.end());
							for (auto iter = objectALaterSiblings.begin(); iter != objectBIter; ++iter)
							{
								(*iter)->AddSelfAndChildrenToVec(objectsToSelect);
							}
						}

						for (GameObject* objectToSelect : objectsToSelect)
						{
							g_Editor->AddSelectedObject(objectToSelect->ID);
						}
					}
				}
				else
				{
					g_Editor->SetSelectedObject(gameObject->ID);
				}
			}

			if (ImGui::IsItemActive())
			{
				if (ImGui::BeginDragDropSource())
				{
					void* data = nullptr;
					size_t size = 0;

					const std::vector<GameObjectID>& selectedObjectIDs = g_Editor->GetSelectedObjectIDs();
					auto iter = Find(selectedObjectIDs, gameObject->ID);
					bool bItemInSelection = iter != selectedObjectIDs.end();
					std::string dragDropText;

					std::vector<GameObjectID> draggedGameObjectIDs;
					if (bItemInSelection)
					{
						for (const GameObjectID& selectedObjectID : selectedObjectIDs)
						{
							// Don't allow children to not be part of dragged selection
							GameObject* selectedObject = GetGameObject(selectedObjectID);
							selectedObject->AddSelfIDAndChildrenToVec(draggedGameObjectIDs);
						}

						// Ensure any children which weren't selected are now in selection
						for (const GameObjectID& draggedGameObjectID : draggedGameObjectIDs)
						{
							g_Editor->AddSelectedObject(draggedGameObjectID);
						}

						data = draggedGameObjectIDs.data();
						size = draggedGameObjectIDs.size() * sizeof(GameObjectID);

						if (draggedGameObjectIDs.size() == 1)
						{
							dragDropText = GetGameObject(draggedGameObjectIDs[0])->GetName();
						}
						else
						{
							dragDropText = IntToString((u32)draggedGameObjectIDs.size()) + " objects";
						}
					}
					else
					{
						g_Editor->SetSelectedObject(gameObject->ID);

						data = (void*)&gameObject->ID;
						size = sizeof(GameObjectID);
						dragDropText = gameObject->GetName();
					}

					ImGui::SetDragDropPayload(Editor::GameObjectPayloadCStr, data, size);

					ImGui::Text("%s", dragDropText.c_str());

					ImGui::EndDragDropSource();
				}
			}

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Editor::GameObjectPayloadCStr);

				if (payload && payload->Data)
				{
					i32 draggedObjectCount = payload->DataSize / sizeof(GameObjectID);

					std::vector<GameObjectID> draggedGameObjectIDs;
					draggedGameObjectIDs.reserve(draggedObjectCount);
					for (i32 i = 0; i < draggedObjectCount; ++i)
					{
						draggedGameObjectIDs.push_back(*((GameObjectID*)payload->Data + i));
					}

					if (!draggedGameObjectIDs.empty())
					{
						bool bContainsChild = false;

						for (const GameObjectID& draggedGameObjectID : draggedGameObjectIDs)
						{
							GameObject* draggedGameObject = GetGameObject(draggedGameObjectID);
							if (draggedGameObject == gameObject)
							{
								bContainsChild = true;
								break;
							}

							if (draggedGameObject->HasChild(gameObject, true))
							{
								bContainsChild = true;
								break;
							}
						}

						// If we're a child of the dragged object then don't allow (causes infinite recursion)
						if (!bContainsChild)
						{
							for (const GameObjectID& draggedGameObjectID : draggedGameObjectIDs)
							{
								GameObject* draggedGameObject = GetGameObject(draggedGameObjectID);
								GameObject* parent = draggedGameObject->GetParent();
								if (parent != nullptr)
								{
									// Parent isn't also being dragged
									if (Find(draggedGameObjectIDs, parent->ID) == draggedGameObjectIDs.end())
									{
										draggedGameObject->DetachFromParent();
										gameObject->AddChildImmediate(draggedGameObject);
										bParentChildTreeDirty = true;
									}
								}
								else
								{
									g_SceneManager->CurrentScene()->RemoveObjectImmediate(draggedGameObject, false);
									gameObject->AddChildImmediate(draggedGameObject);
									bParentChildTreeDirty = true;
								}
							}
						}
					}
				}

				ImGui::EndDragDropTarget();
			}
		}

		if (bNodeOpen && bHasChildren)
		{
			if (!bParentChildTreeDirty && gameObject)
			{
				ImGui::Indent();
				// Don't cache results since children can change during this recursive call
				for (GameObject* child : gameObject->GetChildren())
				{
					if (DrawImGuiGameObjectNameAndChildren(child))
					{
						// If parent-child tree changed then early out

						ImGui::Unindent();
						ImGui::TreePop();

						return true;
					}
				}
				ImGui::Unindent();
			}

			ImGui::TreePop();
		}

		return bParentChildTreeDirty;
	}

	GameObject* BaseScene::GetGameObject(const GameObjectID& gameObjectID)
	{
		auto iter = m_GameObjectLUT.find(gameObjectID);
		if (iter == m_GameObjectLUT.end())
		{
			return nullptr;
		}
		return iter->second;
	}

	const char* BaseScene::GameObjectTypeIDToString(StringID typeID)
	{
		auto iter = GameObjectTypeStringIDPairs.find(typeID);
		if (iter == GameObjectTypeStringIDPairs.end())
		{
			return "";
		}
		return iter->second.c_str();
	}

	void BaseScene::UpdateRootObjectSiblingIndices()
	{
		for (i32 i = 0; i < (i32)m_RootObjects.size(); ++i)
		{
			m_RootObjects[i]->UpdateSiblingIndices(i);
		}
	}

	void BaseScene::SerializeToFile(bool bSaveOverDefault /* = false */) const
	{
		bool success = true;

		success = g_ResourceManager->SerializeMaterialFile() && success;
		//success &= BaseScene::SerializePrefabFile();

		PROFILE_AUTO("Serialize scene");

		JSONObject rootSceneObject = {};

		rootSceneObject.fields.emplace_back("version", JSONValue(m_SceneFileVersion));
		rootSceneObject.fields.emplace_back("name", JSONValue(m_Name));
		rootSceneObject.fields.emplace_back("spawn player", JSONValue(m_bSpawnPlayer));
		if (m_Player0 != nullptr)
		{
			rootSceneObject.fields.emplace_back("player 0 guid", JSONValue(m_Player0->ID));
		}
		if (m_Player1 != nullptr)
		{
			rootSceneObject.fields.emplace_back("player 1 guid", JSONValue(m_Player1->ID));
		}

		JSONObject skyboxDataObj = {};
		skyboxDataObj.fields.emplace_back("top colour", JSONValue(VecToString(glm::pow(m_SkyboxData.top, glm::vec4(1.0f / 2.2f)))));
		skyboxDataObj.fields.emplace_back("mid colour", JSONValue(VecToString(glm::pow(m_SkyboxData.mid, glm::vec4(1.0f / 2.2f)))));
		skyboxDataObj.fields.emplace_back("btm colour", JSONValue(VecToString(glm::pow(m_SkyboxData.btm, glm::vec4(1.0f / 2.2f)))));
		rootSceneObject.fields.emplace_back("skybox data", JSONValue(skyboxDataObj));

		{
			JSONObject cameraObj = {};

			BaseCamera* cam = g_CameraManager->CurrentCamera();

			// TODO: Serialize all non-procedural cameras & last used

			cameraObj.fields.emplace_back("type", JSONValue(cam->GetName().c_str()));

			cameraObj.fields.emplace_back("near plane", JSONValue(cam->zNear));
			cameraObj.fields.emplace_back("far plane", JSONValue(cam->zFar));
			cameraObj.fields.emplace_back("fov", JSONValue(cam->FOV));
			cameraObj.fields.emplace_back("aperture", JSONValue(cam->aperture));
			cameraObj.fields.emplace_back("shutter speed", JSONValue(cam->shutterSpeed));
			cameraObj.fields.emplace_back("light sensitivity", JSONValue(cam->lightSensitivity));
			cameraObj.fields.emplace_back("exposure", JSONValue(cam->exposure));
			cameraObj.fields.emplace_back("move speed", JSONValue(cam->moveSpeed));

			std::string posStr = VecToString(cam->position, 3);
			real pitch = cam->pitch;
			real yaw = cam->yaw;
			JSONObject cameraTransform = {};
			cameraTransform.fields.emplace_back("position", JSONValue(posStr));
			cameraTransform.fields.emplace_back("pitch", JSONValue(pitch));
			cameraTransform.fields.emplace_back("yaw", JSONValue(yaw));
			cameraObj.fields.emplace_back("transform", JSONValue(cameraTransform));

			rootSceneObject.fields.emplace_back("camera", JSONValue(cameraObj));
		}

		std::vector<JSONObject> objectsArray;
		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject->IsSerializable())
			{
				objectsArray.push_back(rootObject->Serialize(this));
			}
		}
		rootSceneObject.fields.emplace_back("objects", JSONValue(objectsArray));

		if (!m_TrackManager.m_Tracks.empty())
		{
			rootSceneObject.fields.emplace_back("track manager", JSONValue(m_TrackManager.Serialize()));
		}

		Print("Serializing scene to %s\n", m_FileName.c_str());

		std::string fileContents = rootSceneObject.Print(0);

		const std::string defaultSaveFilePath = SCENE_DEFAULT_DIRECTORY + m_FileName;
		const std::string savedSaveFilePath = SCENE_SAVED_DIRECTORY + m_FileName;
		std::string saveFilePath;
		if (bSaveOverDefault)
		{
			saveFilePath = defaultSaveFilePath;
		}
		else
		{
			saveFilePath = savedSaveFilePath;
		}

		saveFilePath = RelativePathToAbsolute(saveFilePath);

		if (bSaveOverDefault)
		{
			//m_bUsingSaveFile = false;

			if (FileExists(savedSaveFilePath))
			{
				Platform::DeleteFile(savedSaveFilePath);
			}
		}


		success &= WriteFile(saveFilePath, fileContents, false);

		if (success)
		{
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::blip));
			g_Renderer->AddEditorString("Saved " + m_Name);

			if (!bSaveOverDefault)
			{
				//m_bUsingSaveFile = true;
			}
		}
		else
		{
			PrintError("Failed to open file for writing at \"%s\", Can't serialize scene\n", m_FileName.c_str());
			AudioManager::PlaySource(FlexEngine::GetAudioSourceID(FlexEngine::SoundEffect::dud_dud_dud_dud));
		}
	}

	void BaseScene::DeleteSaveFiles()
	{
		const std::string defaultSaveFilePath = SCENE_DEFAULT_DIRECTORY + m_FileName;
		const std::string savedSaveFilePath = SCENE_SAVED_DIRECTORY + m_FileName;

		bool bDefaultFileExists = FileExists(defaultSaveFilePath);
		bool bSavedFileExists = FileExists(savedSaveFilePath);

		if (bSavedFileExists ||
			bDefaultFileExists)
		{
			Print("Deleting scene's save files from %s\n", m_FileName.c_str());

			if (bDefaultFileExists)
			{
				Platform::DeleteFile(defaultSaveFilePath);
			}

			if (bSavedFileExists)
			{
				Platform::DeleteFile(savedSaveFilePath);
			}
		}

		//m_bUsingSaveFile = false;
	}

	GameObject* BaseScene::AddRootObject(GameObject* gameObject)
	{
		if (gameObject == nullptr)
		{
			return nullptr;
		}

		m_PendingAddObjects.push_back(gameObject);

		return gameObject;
	}

	GameObject* BaseScene::AddRootObjectImmediate(GameObject* gameObject)
	{
		if (gameObject == nullptr)
		{
			return nullptr;
		}

		for (GameObject* rootObject : m_RootObjects)
		{
			if (rootObject->ID == gameObject->ID)
			{
				PrintWarn("Attempted to add same root object (%s) to scene more than once\n", rootObject->m_Name.c_str());
				return nullptr;
			}
		}

		RegisterGameObject(gameObject);

		gameObject->SetParent(nullptr);

		m_RootObjects.push_back(gameObject);

		UpdateRootObjectSiblingIndices();
		g_Renderer->RenderObjectStateChanged();

		return gameObject;
	}

	void BaseScene::RegisterGameObject(GameObject* gameObject)
	{
		auto iter = m_GameObjectLUT.find(gameObject->ID);
		if (iter != m_GameObjectLUT.end())
		{
			assert(iter->second == gameObject);
		}
		m_GameObjectLUT[gameObject->ID] = gameObject;

		for (GameObject* child : gameObject->m_Children)
		{
			RegisterGameObject(child);
		}
	}

	void BaseScene::UnregisterGameObject(const GameObjectID& gameObjectID)
	{
		auto iter = m_GameObjectLUT.find(gameObjectID);
		if (iter == m_GameObjectLUT.end())
		{
			// Silently fail (this case is hit with e.g. editor gizmo objects that aren't ever registered)

			//std::string idStr = gameObjectID.ToString();
			//PrintError("Attempted to unregister non-registered object with ID %s\n", idStr.c_str());
		}
		else
		{
			m_GameObjectLUT.erase(iter);
		}
	}

	GameObject* BaseScene::AddChildObject(GameObject* parent, GameObject* child)
	{
		if (parent == nullptr || child == nullptr)
		{
			return child;
		}

		if (parent->ID == child->ID)
		{
			PrintError("Attempted to add self as child on %s\n", child->GetName().c_str());
			return child;
		}

		m_PendingAddChildObjects.emplace_back(parent, child);

		return child;
	}

	GameObject* BaseScene::AddChildObjectImmediate(GameObject* parent, GameObject* child)
	{
		if (parent == nullptr || child == nullptr)
		{
			return child;
		}

		if (parent->ID == child->ID)
		{
			PrintError("Attempted to add self as child on %s\n", child->GetName().c_str());
			return child;
		}


		parent->AddChildImmediate(child);
		RegisterGameObject(child);

		return child;
	}

	void BaseScene::RemoveAllObjects()
	{
		for (GameObject* rootObject : m_RootObjects)
		{
			m_PendingDestroyObjects.push_back(rootObject->ID);
		}
	}

	void BaseScene::RemoveAllObjectsImmediate()
	{
		auto iter = m_RootObjects.begin();
		while (iter != m_RootObjects.end())
		{
			GameObject* gameObject = *iter;

			// Recurses down child hierarchy
			gameObject->Destroy();
			delete gameObject;

			iter = m_RootObjects.erase(iter);
		}

		m_GameObjectLUT.clear();
		m_RootObjects.clear();

		g_Renderer->RenderObjectStateChanged();
	}

	void BaseScene::RemoveObject(const GameObjectID& gameObjectID, bool bDestroy)
	{
		if (bDestroy)
		{
			// TODO: Use set to handle uniqueness checking
			if (!Contains(m_PendingDestroyObjects, gameObjectID))
			{
				m_PendingDestroyObjects.push_back(gameObjectID);
			}
		}
		else
		{
			if (!Contains(m_PendingRemoveObjects, gameObjectID))
			{
				m_PendingRemoveObjects.push_back(gameObjectID);
			}
		}
	}

	void BaseScene::RemoveObject(GameObject* gameObject, bool bDestroy)
	{
		RemoveObject(gameObject->ID, bDestroy);
	}

	void BaseScene::RemoveObjectImmediate(const GameObjectID& gameObjectID, bool bDestroy)
	{
		GameObject* gameObject = GetGameObject(gameObjectID);
		if (gameObject == nullptr)
		{
			std::string idStr = gameObjectID.ToString();
			PrintError("Invalid game object ID: %s\n", idStr.c_str());
			return;
		}

		const bool bRootObject = (gameObject->GetParent() == nullptr);

		if (bRootObject)
		{
			auto iter = m_RootObjects.begin();
			while (iter != m_RootObjects.end())
			{
				if ((*iter)->ID == gameObjectID)
				{
					m_RootObjects.erase(iter);
					break;
				}
				++iter;
			}
		}

		if (bDestroy)
		{
			RemoveObjectImmediateRecursive(gameObjectID, bDestroy);
		}
		else
		{
			if (!bRootObject)
			{
				gameObject->GetParent()->RemoveChildImmediate(gameObjectID, bDestroy);
			}
		}
	}

	void BaseScene::RemoveObjectImmediate(GameObject* gameObject, bool bDestroy)
	{
		RemoveObjectImmediate(gameObject->ID, bDestroy);
	}

	void BaseScene::RemoveObjectImmediateRecursive(const GameObjectID& gameObjectID, bool bDestroy)
	{
		GameObject* gameObject = GetGameObject(gameObjectID);
		if (gameObject == nullptr)
		{
			std::string idStr = gameObjectID.ToString();
			PrintError("Invalid game object ID: %s\n", idStr.c_str());
			return;
		}

		const bool bRootObject = (gameObject->GetParent() == nullptr);

		if (bDestroy)
		{
			while (!gameObject->m_Children.empty())
			{
				// gameObject->m_Children will shrink by one for each call since bDestroy will ensure children are detached from us
				GameObject* child = gameObject->m_Children[gameObject->m_Children.size() - 1];
				RemoveObjectImmediateRecursive(child->ID, bDestroy);
			}
		}
		else
		{
			auto childIter = gameObject->m_Children.begin();
			while (childIter != gameObject->m_Children.end())
			{
				RemoveObjectImmediateRecursive((*childIter)->ID, bDestroy);
				childIter = gameObject->m_Children.erase(childIter);
			}
		}

		if (bDestroy)
		{
			gameObject->Destroy();
			delete gameObject;
		}
		else
		{
			gameObject->DetachFromParent();
		}
	}

	void BaseScene::RemoveObjects(const std::vector<GameObjectID>& gameObjects, bool bDestroy)
	{
		if (bDestroy)
		{
			m_PendingDestroyObjects.insert(m_PendingDestroyObjects.end(), gameObjects.begin(), gameObjects.end());
		}
		else
		{
			m_PendingRemoveObjects.insert(m_PendingRemoveObjects.end(), gameObjects.begin(), gameObjects.end());
		}
	}

	void BaseScene::RemoveObjects(const std::vector<GameObject*>& gameObjects, bool bDestroy)
	{
		if (bDestroy)
		{
			m_PendingDestroyObjects.reserve(m_PendingDestroyObjects.size() + gameObjects.size());
			for (GameObject* gameObject : gameObjects)
			{
				if (!Contains(m_PendingDestroyObjects, gameObject->ID))
				{
					m_PendingDestroyObjects.push_back(gameObject->ID);
				}
			}
		}
		else
		{
			m_PendingRemoveObjects.reserve(m_PendingRemoveObjects.size() + gameObjects.size());
			for (GameObject* gameObject : gameObjects)
			{
				if (!Contains(m_PendingRemoveObjects, gameObject->ID))
				{
					m_PendingRemoveObjects.push_back(gameObject->ID);
				}
			}
		}
	}

	void BaseScene::RemoveObjectsImmediate(const std::vector<GameObjectID>& gameObjects, bool bDestroy)
	{
		for (const GameObjectID& gameObjectID : gameObjects)
		{
			RemoveObjectImmediate(gameObjectID, bDestroy);
		}
	}

	void BaseScene::RemoveObjectsImmediate(const std::vector<GameObject*>& gameObjects, bool bDestroy)
	{
		for (GameObject* gameObject : gameObjects)
		{
			RemoveObjectImmediate(gameObject->ID, bDestroy);
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

	void BaseScene::ReadGameObjectTypesFile()
	{
		GameObjectTypeStringIDPairs = {};
		std::string fileContents;
		if (ReadFile(GAME_OBJECT_TYPES_LOCATION, fileContents, false))
		{
			std::vector<std::string> lines = Split(fileContents, '\n');
			for (const std::string& line : lines)
			{
				if (!line.empty())
				{
					const char* lineCStr = line.c_str();
					StringID typeID = Hash(lineCStr);
					if (GameObjectTypeStringIDPairs.find(typeID) != GameObjectTypeStringIDPairs.end())
					{
						PrintError("Game Object Type hash collision on %s!\n", lineCStr);
					}
					GameObjectTypeStringIDPairs.emplace(typeID, line);
				}
			}
		}
		else
		{
			PrintError("Failed to read game object types file from %s!\n", GAME_OBJECT_TYPES_LOCATION);
		}
	}

	void BaseScene::WriteGameObjectTypesFile()
	{
		StringBuilder fileContents;

		for (auto iter = GameObjectTypeStringIDPairs.begin(); iter != GameObjectTypeStringIDPairs.end(); ++iter)
		{
			fileContents.AppendLine(iter->second);
		}

		if (!WriteFile(GAME_OBJECT_TYPES_LOCATION, fileContents.ToString(), false))
		{
			PrintError("Failed to write game object types file to %s\n", GAME_OBJECT_TYPES_LOCATION);
		}
	}

	std::vector<GameObject*>& BaseScene::GetRootObjects()
	{
		return m_RootObjects;
	}

	void BaseScene::GetInteractableObjects(std::vector<GameObject*>& interactableObjects)
	{
		std::vector<GameObject*> allObjects = GetAllObjects();
		for (GameObject* object : allObjects)
		{
			if (object->m_bInteractable)
			{
				interactableObjects.push_back(object);
			}
		}
	}

	void BaseScene::BindOnGameObjectDestroyedCallback(ICallbackGameObject* callback)
	{
		if (std::find(m_OnGameObjectDestroyedCallbacks.begin(), m_OnGameObjectDestroyedCallbacks.end(), callback) != m_OnGameObjectDestroyedCallbacks.end())
		{
			PrintWarn("Attempted to bind on game object destroyed callback multiple times!\n");
			return;
		}

		m_OnGameObjectDestroyedCallbacks.push_back(callback);
	}

	void BaseScene::UnbindOnGameObjectDestroyedCallback(ICallbackGameObject* callback)
	{
		auto iter = std::find(m_OnGameObjectDestroyedCallbacks.begin(), m_OnGameObjectDestroyedCallbacks.end(), callback);
		if (iter == m_OnGameObjectDestroyedCallbacks.end())
		{
			PrintWarn("Attempted to unbind game object destroyed callback that isn't present in callback list!\n");
			return;
		}

		m_OnGameObjectDestroyedCallbacks.erase(iter);
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
		return SCENE_DEFAULT_DIRECTORY + m_FileName;
	}

	std::string BaseScene::GetRelativeFilePath() const
	{
		//if (m_bUsingSaveFile)
		//{
		//	return SCENE_SAVED_DIRECTORY + m_FileName;
		//}
		//else
		//{
		return SCENE_DEFAULT_DIRECTORY + m_FileName;
		//}
	}

	std::string BaseScene::GetShortRelativeFilePath() const
	{
		//if (m_bUsingSaveFile)
		//{
		//	return SCENE_SAVED_DIRECTORY + m_FileName;
		//}
		//else
		//{
		return SCENE_DEFAULT_DIRECTORY + m_FileName;
		//}
	}

	bool BaseScene::SetFileName(const std::string& fileName, bool bDeletePreviousFiles)
	{
		bool success = false;

		if (fileName == m_FileName)
		{
			return true;
		}

		std::string absDefaultFilePathFrom = RelativePathToAbsolute(GetDefaultRelativeFilePath());
		std::string absDefaultFilePathTo = ExtractDirectoryString(absDefaultFilePathFrom) + fileName;
		if (Platform::CopyFile(absDefaultFilePathFrom, absDefaultFilePathTo))
		{
			//if (m_bUsingSaveFile)
			{
				std::string absSavedFilePathFrom = RelativePathToAbsolute(GetRelativeFilePath());
				std::string absSavedFilePathTo = ExtractDirectoryString(absSavedFilePathFrom) + fileName;
				if (Platform::CopyFile(absSavedFilePathFrom, absSavedFilePathTo))
				{
					success = true;
				}
			}
			//else
			//{
			//	success = true;
			//}

			// Don't delete files unless copies worked
			if (success)
			{
				if (bDeletePreviousFiles)
				{
					std::string pAbsDefaultFilePath = RelativePathToAbsolute(GetDefaultRelativeFilePath());
					Platform::DeleteFile(pAbsDefaultFilePath, false);

					//if (m_bUsingSaveFile)
					//{
					//	std::string pAbsSavedFilePath = RelativePathToAbsolute(GetRelativeFilePath());
					//	Platform::DeleteFile(pAbsSavedFilePath, false);
					//}
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
		//return m_bUsingSaveFile;
		return false;
	}

	PhysicsWorld* BaseScene::GetPhysicsWorld()
	{
		return m_PhysicsWorld;
	}

} // namespace flex
