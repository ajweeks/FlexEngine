#include "stdafx.hpp"

#include "Scene/SceneManager.hpp"

#include "Cameras/CameraManager.hpp"
#include "Editor.hpp"
#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Platform/Platform.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "StringBuilder.hpp"
#include "Systems/TrackManager.hpp"

namespace flex
{
	const char* SceneManager::s_NewObjectTypePopupStr = "New Object Type";
	const char* SceneManager::s_newSceneModalWindowID = "New scene";

	SceneManager::SceneManager() :
		m_DefaultDirStr(RelativePathToAbsolute(SCENE_DEFAULT_DIRECTORY))
	{
		AddFoundScenes();
	}

	void SceneManager::Destroy()
	{
		m_Scenes[m_CurrentSceneIndex]->Destroy();

		auto iter = m_Scenes.begin();
		while (iter != m_Scenes.end())
		{
			delete* iter;
			iter = m_Scenes.erase(iter);
		}
		m_Scenes.clear();
		m_CurrentSceneIndex = InvalidID;
	}

	bool SceneManager::SetCurrentScene(u32 sceneIndex, bool bPrintErrorOnFailure /* = true */)
	{
		if (bPrintErrorOnFailure && sceneIndex >= m_Scenes.size())
		{
			PrintError("Attempt to set scene to index %u failed, it does not exist in the SceneManager\n",
				sceneIndex);
			return false;
		}

		std::string sceneFileName = m_Scenes[sceneIndex]->GetFileName();
		Print("Loading scene at %s\n", sceneFileName.c_str());

		if (m_CurrentSceneIndex != InvalidID)
		{
			g_Editor->PreSceneChange();
			g_ResourceManager->PreSceneChange();
			g_Renderer->OnPreSceneChange();
		}

		// Any modifications will now be lost, so all prefabs will be clean again
		g_ResourceManager->SetAllPrefabsDirty(false);

		u32 previousSceneIndex = m_CurrentSceneIndex;

		if (m_CurrentSceneIndex != InvalidID)
		{
			g_CameraManager->Destroy();
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->Destroy();
			GetSystem<RoadManager>(SystemType::ROAD_MANAGER)->Destroy();

			m_Scenes[m_CurrentSceneIndex]->Destroy();
		}

		m_CurrentSceneIndex = sceneIndex;

		g_EngineInstance->CreateCameraInstances();

		InitializeCurrentScene(previousSceneIndex);
		PostInitializeCurrentScene();
		g_CameraManager->Initialize();

		g_EngineInstance->SetFramesToFakeDT(3);

		return true;
	}

	bool SceneManager::SetCurrentScene(BaseScene* scene, bool bPrintErrorOnFailure /* = true */)
	{
		for (size_t i = 0; i < m_Scenes.size(); ++i)
		{
			if (m_Scenes[i]->GetFileName().compare(scene->GetFileName()) == 0)
			{
				return SetCurrentScene((u32)i, bPrintErrorOnFailure);
			}
		}

		if (bPrintErrorOnFailure)
		{
			PrintError("Attempt to set current scene failed because it was not found in the scene manager: %s\n", scene->GetName().c_str());
		}

		return false;
	}

	bool SceneManager::SetCurrentScene(const std::string& sceneFileName, bool bPrintErrorOnFailure /* = true */)
	{
		std::string sceneNameClean = sceneFileName;
		size_t dotPos = sceneNameClean.rfind('.');
		if (dotPos == std::string::npos)
		{
			sceneNameClean = sceneNameClean + ".json";
		}
		for (size_t i = 0; i < m_Scenes.size(); ++i)
		{
			// TODO: Give scenes GUIDs to prevent name clashes
			if (m_Scenes[i]->GetFileName().compare(sceneNameClean) == 0)
			{
				return SetCurrentScene((u32)i, bPrintErrorOnFailure);
			}
		}

		if (bPrintErrorOnFailure)
		{
			PrintError("Attempt to set scene failed, it does not exist in the SceneManager: %s\n", sceneFileName.c_str());
		}

		return false;
	}

	void SceneManager::SetNextSceneActive()
	{
		u32 sceneCount = (u32)m_Scenes.size();

		u32 newCurrentSceneIndex = m_CurrentSceneIndex != InvalidID ? ((m_CurrentSceneIndex + 1) % sceneCount) : 0;
		SetCurrentScene(newCurrentSceneIndex);
	}

	void SceneManager::SetPreviousSceneActive()
	{
		const size_t sceneCount = m_Scenes.size();
		if (sceneCount == 1)
		{
			return;
		}

		// Loop around to previous index but stay positive cause things are unsigned
		u32 newCurrentSceneIndex = (u32)((m_CurrentSceneIndex + m_Scenes.size() - 1) % m_Scenes.size());
		SetCurrentScene(newCurrentSceneIndex);
	}

	void SceneManager::ReloadCurrentScene()
	{
		Print("Reloading current scene\n");

		SetCurrentScene(m_CurrentSceneIndex);

		g_Renderer->AddEditorString("Scene reloaded");
	}

	void SceneManager::CreateNewScene(const std::string& name, bool bSwitchImmediately)
	{
		const i32 newSceneIndex = (i32)m_Scenes.size();

		std::string fileName = name + ".json";

		BaseScene* newScene = new BaseScene(fileName);
		newScene->SetName(name);

		Print("Created new scene %s\n", name.c_str());

		m_Scenes.push_back(newScene);

		if (bSwitchImmediately)
		{
			SetCurrentScene(newSceneIndex);
		}

		newScene->SerializeToFile(true);
	}

	void SceneManager::DeleteScene(BaseScene* scene)
	{
		if (m_Scenes.size() == 1)
		{
			PrintWarn("Attempted to delete only remaining scene!\n");
			return;
		}

		if (!scene)
		{
			PrintWarn("Attempted to delete invalid scene!\n");
			return;
		}

		u32 oldSceneIndex = u32_max;
		for (i32 i = 0; i < (i32)m_Scenes.size(); ++i)
		{
			if (m_Scenes[i] == scene)
			{
				oldSceneIndex = i;
				break;
			}
		}

		if (oldSceneIndex == u32_max)
		{
			PrintError("Attempted to delete scene not present in scene manager: %s\n ", scene->GetName().c_str());
			return;
		}

		// After the resize
		u32 newSceneIndex = (u32)m_CurrentSceneIndex;
		bool bDeletingCurrentScene = (m_CurrentSceneIndex == oldSceneIndex);
		if (bDeletingCurrentScene)
		{
			newSceneIndex = (newSceneIndex + 1) % (u32)m_Scenes.size();
			SetCurrentScene(newSceneIndex);
		}
		else
		{
			if (oldSceneIndex < newSceneIndex)
			{
				newSceneIndex -= 1;
			}
		}

		Print("Deleting scene %s\n", scene->GetFileName().c_str());

		scene->DeleteSaveFiles();
		scene->Destroy();
		delete scene;
		m_Scenes.erase(m_Scenes.begin() + oldSceneIndex);
		if (bDeletingCurrentScene)
		{
			m_CurrentSceneIndex = (newSceneIndex == 0) ? 0 : (newSceneIndex - 1);
		}
	}

	void SceneManager::DrawImGuiObjects()
	{
		if (ImGui::TreeNode("Scenes"))
		{
			if (ImGui::Button("<"))
			{
				SetPreviousSceneActive();
			}

			ImGui::SameLine();

			BaseScene* currentScene = CurrentScene();

			// TODO: Display dirty status here
			const std::string currentSceneNameStr(currentScene->GetName());
			ImGui::Text("%s", currentSceneNameStr.c_str());

			if (ImGui::IsItemHovered())
			{
				std::string fileName = currentScene->GetShortRelativeFilePath();
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(fileName.c_str());
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			if (ImGui::Button(">"))
			{
				SetNextSceneActive();
			}

			currentScene->DrawImGuiObjects();

			ImGui::Separator();

			static ImGuiTextFilter sceneFilter;
			sceneFilter.Draw("##scene-filter");

			ImGui::SameLine();
			if (ImGui::Button("x"))
			{
				sceneFilter.Clear();
			}

			i32 sceneItemWidth = 240;
			if (ImGui::BeginChild("Scenes", ImVec2((real)sceneItemWidth, 120), true, ImGuiWindowFlags_NoResize))
			{
				for (i32 i = 0; i < (i32)m_Scenes.size(); ++i)
				{
					BaseScene* scene = m_Scenes[i];
					std::string sceneFileName = scene->GetFileName();
					if (sceneFilter.PassFilter(sceneFileName.c_str()))
					{
						bool bSceneSelected = (i == (i32)m_CurrentSceneIndex);
						if (ImGui::Selectable(sceneFileName.c_str(), &bSceneSelected, 0, ImVec2((real)sceneItemWidth, 0)))
						{
							if (i != (i32)m_CurrentSceneIndex)
							{
								SetCurrentScene(i);
							}
						}

						scene->DoSceneContextMenu();
					}
				}
			}
			ImGui::EndChild();

			if (ImGui::Button("New scene..."))
			{
				bOpenNewSceneWindow = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("Refresh scenes"))
			{
				AddFoundScenes();
				RemoveDeletedScenes();
			}

			ImGui::TreePop();
		}

		if (bOpenNewObjectTypePopup)
		{
			bOpenNewObjectTypePopup = false;
			ImGui::OpenPopup(s_NewObjectTypePopupStr);
			m_NewObjectTypeStrBuffer.clear();
			m_NewObjectTypeStrBuffer.resize(GameObject::MaxNameLength);
		}

		if (ImGui::BeginPopupModal(s_NewObjectTypePopupStr, NULL,
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoNavInputs))
		{
			bool bCreateType = false;
			if (ImGui::InputText("Type", (char*)m_NewObjectTypeStrBuffer.data(), GameObject::MaxNameLength, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				bCreateType = true;
			}

			ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();

			ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f);

			if (ImGui::Button("Create") || bCreateType)
			{
				// Remove excess trailing \0 chars
				m_NewObjectTypeStrBuffer = std::string(m_NewObjectTypeStrBuffer.c_str());

				if (!m_NewObjectTypeStrBuffer.empty())
				{
					g_ResourceManager->AddNewGameObjectType(m_NewObjectTypeStrBuffer.c_str());
					ImGui::CloseCurrentPopup();
				}
			}

			ImGui::EndPopup();
		}
	}

	void SceneManager::DrawImGuiModals()
	{
		bool bFocusNameTextBox = false;
		if (bOpenNewSceneWindow)
		{
			bOpenNewSceneWindow = false;
			bFocusNameTextBox = true;
			ImGui::OpenPopup(s_newSceneModalWindowID);
		}

		if (ImGui::BeginPopupModal(s_newSceneModalWindowID, NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			static std::string newSceneName = "scene_" + IntToString((i32)m_Scenes.size(), 2);

			const size_t maxStrLen = 256;
			newSceneName.resize(maxStrLen);
			if (bFocusNameTextBox)
			{
				ImGui::SetKeyboardFocusHere();
			}
			bool bCreate = ImGui::InputText("Scene name",
				(char*)newSceneName.data(),
				maxStrLen,
				ImGuiInputTextFlags_EnterReturnsTrue);

			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();

			ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f);

			bCreate |= ImGui::Button("Create");

			if (bCreate)
			{
				// Remove trailing '\0' characters
				newSceneName = std::string(newSceneName.c_str());
				newSceneName = MakeSceneNameUnique(newSceneName);
				CreateNewScene(newSceneName, true);
				g_CameraManager->SetCameraByName("debug", false);

				ImGui::CloseCurrentPopup();
			}

			if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	BaseScene* SceneManager::CurrentScene() const
	{
		return m_Scenes[m_CurrentSceneIndex];
	}

	bool SceneManager::HasSceneLoaded() const
	{
		return m_CurrentSceneIndex != InvalidID &&
			m_CurrentSceneIndex < (u32)m_Scenes.size() &&
			CurrentScene()->m_bInitialized;
	}

	void SceneManager::OpenNewSceneWindow()
	{
		bOpenNewSceneWindow = true;
	}

	bool SceneManager::DuplicateScene(BaseScene* scene, const std::string& newSceneFileName, const std::string& newSceneName)
	{
		bool bResult = false;

		std::string filePathFrom = RelativePathToAbsolute(scene->GetDefaultRelativeFilePath());
		std::string filePathTo = ExtractDirectoryString(filePathFrom) + newSceneFileName;

		if (FileExists(filePathTo))
		{
			PrintError("Attempting to duplicate scene onto already existing file name!\n");
		}
		else
		{
			if (Platform::CopyFile(filePathFrom, filePathTo))
			{
				BaseScene* newScene = new BaseScene(newSceneFileName);
				AddScene(newScene);
				SetCurrentScene(newScene);

				newScene->SetName(newSceneName);

				CurrentScene()->SerializeToFile(true);

				bResult = true;

				ImGui::CloseCurrentPopup();
			}
			else
			{
				PrintError("Failed to copy scene's file to %s\n", newSceneFileName.c_str());
			}
		}

		return bResult;
	}

	std::string SceneManager::MakeSceneNameUnique(const std::string& originalName)
	{
		std::string newSceneName = originalName;

		bool bSceneNameExists = false;
		for (BaseScene* scene : m_Scenes)
		{
			if (scene->GetName().compare(newSceneName) == 0)
			{
				bSceneNameExists = true;
				break;
			}
		}

		if (bSceneNameExists)
		{
			newSceneName = GetIncrementedPostFixedStr(newSceneName, "new scene");
		}

		return newSceneName;
	}

	bool SceneManager::SceneFileExists(const std::string& fileName) const
	{
		for (BaseScene* scene : m_Scenes)
		{
			if (scene->GetFileName().compare(fileName) == 0)
			{
				return true;
			}
		}

		return false;
	}

	void SceneManager::AddFoundScenes()
	{
		bool bFirstTimeThrough = m_Scenes.empty();

		std::vector<std::string> addedSceneFileNames;

		// Load the default for any scenes which don't have a corresponding save file
		std::vector<std::string> foundFileNames;
		if (Platform::FindFilesInDirectory(m_DefaultDirStr, foundFileNames, "json"))
		{
			for (std::string& fileName : foundFileNames)
			{
				fileName = StripLeadingDirectories(fileName);

				if (!SceneFileExists(fileName))
				{
					BaseScene* newScene = new BaseScene(fileName);
					m_Scenes.push_back(newScene);

					addedSceneFileNames.push_back(fileName);
				}
			}
		}

		if (!addedSceneFileNames.empty())
		{
			if (g_bEnableLogging_Loading)
			{
				Print("Added %u scenes to list:\n", (u32)addedSceneFileNames.size());
				for (std::string& fileName : addedSceneFileNames)
				{
					Print("%s, ", fileName.c_str());

				}
				Print("\n");
			}

			if (!bFirstTimeThrough)
			{
				i32 sceneCount = (i32)addedSceneFileNames.size();
				std::string messageStr = "Added " + IntToString(sceneCount) + " scene" + (sceneCount > 1 ? "s" : "");
				g_Renderer->AddEditorString(messageStr);
			}
		}
	}

	void SceneManager::AddScene(BaseScene* newScene)
	{
		bool bUnique = true;
		std::for_each(m_Scenes.begin(), m_Scenes.end(), [&bUnique, newScene](BaseScene* scene) mutable
		{
			if (scene->GetRelativeFilePath().compare(newScene->GetRelativeFilePath()) == 0)
			{
				bUnique = false;
			}
		});

		if (bUnique)
		{
			m_Scenes.push_back(newScene);
		}
		else
		{
			PrintError("Attempt to add already existing scene to SceneManager: %s\n", newScene->GetName().c_str());
		}
	}

	void SceneManager::RemoveDeletedScenes()
	{
		std::vector<std::string> removedSceneNames;

		auto sceneIter = m_Scenes.begin();
		u32 newSceneIndex = m_CurrentSceneIndex;
		u32 sceneIndex = 0;
		while (sceneIter != m_Scenes.end())
		{
			if (FileExists((*sceneIter)->GetRelativeFilePath()))
			{
				++sceneIter;
			}
			else
			{
				std::string fileName = (*sceneIter)->GetFileName();

				// Don't erase the current scene, warn the user the file is missing
				if (m_CurrentSceneIndex == sceneIndex)
				{
					PrintWarn("Current scene's JSON file is missing (%s)! Save now to keep your changes\n", fileName.c_str());
					++sceneIter;
				}
				else
				{
					Print("Removing scene from list due to JSON file missing: %s\n", fileName.c_str());

					removedSceneNames.push_back((*sceneIter)->GetName());
					delete* sceneIter;
					sceneIter = m_Scenes.erase(sceneIter);

					if (m_CurrentSceneIndex > sceneIndex)
					{
						--newSceneIndex;
					}
				}
			}

			++sceneIndex;
		}

		if (newSceneIndex != m_CurrentSceneIndex)
		{
			// Scene isn't actually changing, just the index since the array shrunk
			m_CurrentSceneIndex = newSceneIndex;
		}

		if (!removedSceneNames.empty())
		{
			i32 sceneCount = (i32)removedSceneNames.size();
			std::string messageStr = "Removed " + IntToString(sceneCount) + " scene" + (sceneCount > 1 ? "s" : "");
			g_Renderer->AddEditorString(messageStr);
		}
	}

	void SceneManager::InitializeCurrentScene(u32 previousSceneIndex)
	{
		CHECK(!m_Scenes.empty());

		if (m_CurrentSceneIndex == InvalidID)
		{
			m_CurrentSceneIndex = 0;
		}

		CurrentScene()->Initialize();

		if (previousSceneIndex != InvalidID)
		{
			g_EngineInstance->OnSceneChanged();
			g_ResourceManager->OnSceneChanged();
			g_Editor->OnSceneChanged();
			g_CameraManager->OnSceneChanged();
			g_Renderer->OnPostSceneChange();
			GetSystem<TrackManager>(SystemType::TRACK_MANAGER)->OnSceneChanged();
		}
	}

	void SceneManager::PostInitializeCurrentScene()
	{
		CHECK(!m_Scenes.empty());

		CurrentScene()->PostInitialize();

		if (g_bEnableLogging_Loading)
		{
			std::string currentSceneName = CurrentScene()->GetName();
			Print("Loaded scene %s\n", currentSceneName.c_str());
		}
	}
} // namespace flex
