#include "stdafx.hpp"

#include "Scene/SceneManager.hpp"

#include "Cameras/CameraManager.hpp"
#include "Editor.hpp"
#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Scene/BaseScene.hpp"

namespace flex
{
	SceneManager::SceneManager() :
		m_SavedDirStr(RelativePathToAbsolute(RESOURCE_LOCATION "scenes/saved")),
		m_DefaultDirStr(RelativePathToAbsolute(RESOURCE_LOCATION "scenes/default"))
	{
		if (!DirectoryExists(m_SavedDirStr))
		{
			CreateDirectoryRecursive(m_SavedDirStr);
		}
	}

	SceneManager::~SceneManager()
	{
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

	void SceneManager::InitializeCurrentScene()
	{
		assert(!m_Scenes.empty());

		//g_EngineInstance->PreSceneChange();

		if (m_CurrentSceneIndex == InvalidID)
		{
			m_CurrentSceneIndex = 0;
		}

		CurrentScene()->Initialize();

		if (m_PreviousSceneIndex != InvalidID)
		{
			g_Renderer->OnPreSceneChange();
			g_EngineInstance->OnSceneChanged();
			g_Editor->OnSceneChanged();
			g_CameraManager->OnSceneChanged();
			g_Renderer->OnPostSceneChange();
		}
	}

	void SceneManager::PostInitializeCurrentScene()
	{
		assert(!m_Scenes.empty());

		CurrentScene()->PostInitialize();

		if (g_bEnableLogging_Loading)
		{
			std::string currentSceneName = CurrentScene()->GetName();
			Print("Loaded scene %s\n", currentSceneName.c_str());
		}
	}

	void SceneManager::RemoveScene(BaseScene* scene)
	{
		auto iter = std::find(m_Scenes.begin(), m_Scenes.end(), scene);
		if (iter != m_Scenes.end())
		{
			scene->Destroy();
			delete scene;
			m_Scenes.erase(iter);
		}
		else
		{
			PrintError("Attempt to remove non-existent scene from SceneManager: %s\n", scene->GetName().c_str());
		}
	}

	bool SceneManager::SetCurrentScene(u32 sceneIndex, bool bPrintErrorOnFailure /* = true */)
	{
		if (bPrintErrorOnFailure && sceneIndex >= m_Scenes.size())
		{
			PrintError("Attempt to set scene to index %u failed, it does not exist in the SceneManager\n",
				sceneIndex);
			return false;
		}

		g_Editor->PreSceneChange();

		m_PreviousSceneIndex = m_CurrentSceneIndex;

		if (m_CurrentSceneIndex != InvalidID)
		{
			//if (m_Scenes[m_CurrentSceneIndex]->GetPhysicsWorld())
			//{
			//	std::string editorStr = "Switching to ";
			//	if (m_Scenes[sceneIndex]->GetName().empty())
			//	{
			//		editorStr += m_Scenes[sceneIndex]->GetFileName();
			//	}
			//	else
			//	{
			//		editorStr += m_Scenes[sceneIndex]->GetName();
			//	}
			//	g_Renderer->AddEditorString(editorStr);
			//	// TODO: LATER: HACK: Kinda hacky, but it works... maybe instead of this put a one-frame delay in?
			//	g_Renderer->Draw();
			//}

			m_Scenes[m_CurrentSceneIndex]->Destroy();
		}

		m_CurrentSceneIndex = sceneIndex;

		return true;
	}

	bool SceneManager::SetCurrentScene(BaseScene* scene, bool bPrintErrorOnFailure /* = true */)
	{
		for (size_t i = 0; i < m_Scenes.size(); ++i)
		{
			if (m_Scenes[i]->GetFileName().compare(scene->GetFileName()) == 0)
			{
				return SetCurrentScene(i, bPrintErrorOnFailure);
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
		for (size_t i = 0; i < m_Scenes.size(); ++i)
		{
			if (m_Scenes[i]->GetFileName().compare(sceneFileName) == 0)
			{
				return SetCurrentScene(i, bPrintErrorOnFailure);
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
		const size_t sceneCount = m_Scenes.size();
		if (sceneCount == 1)
		{
			m_CurrentSceneIndex = 0;
			return;
		}

		if (m_CurrentSceneIndex == InvalidID)
		{
			m_CurrentSceneIndex = 0;
		}

		u32 newCurrentSceneIndex = (m_CurrentSceneIndex + 1) % m_Scenes.size();
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
		u32 newCurrentSceneIndex = (m_CurrentSceneIndex + m_Scenes.size() - 1) % m_Scenes.size();
		SetCurrentScene(newCurrentSceneIndex);
	}

	void SceneManager::ReloadCurrentScene()
	{
		Print("Reloading current scene\n");

		SetCurrentScene(m_CurrentSceneIndex);

		InitializeCurrentScene();
		PostInitializeCurrentScene();
		g_Renderer->AddEditorString("Scene reloaded");
	}

	void SceneManager::AddFoundScenes()
	{
		bool bFirstTimeThrough = m_Scenes.empty();

		std::vector<std::string> addedSceneFileNames;

		// Find and load all saved scene files
		std::vector<std::string> foundFileNames;
		if (FindFilesInDirectory(m_SavedDirStr, foundFileNames, "json"))
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

		// Load the default for any scenes which don't have a corresponding save file
		foundFileNames.clear();
		if (FindFilesInDirectory(m_DefaultDirStr, foundFileNames, "json"))
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
				Print("Added %u scenes to list:\n", addedSceneFileNames.size());
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
					delete *sceneIter;
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

		i32 oldSceneIndex = -1;
		for (i32 i = 0; i < (i32)m_Scenes.size(); ++i)
		{
			if (m_Scenes[i] == scene)
			{
				oldSceneIndex = i;
				break;
			}
		}

		if (oldSceneIndex == -1)
		{
			PrintError("Attempted to delete scene not present in scene manager: %s\n ", scene->GetName().c_str());
			return;
		}

		// After the resize
		i32 newSceneIndex = (u32)m_CurrentSceneIndex;
		bool bDeletingCurrentScene = (m_CurrentSceneIndex == (u32)oldSceneIndex);
		if (bDeletingCurrentScene)
		{
			if (m_Scenes.size() == 2)
			{
				m_CurrentSceneIndex = 0;
			}

			g_Editor->PreSceneChange();
			scene->Destroy();
		}
		else
		{
			if (oldSceneIndex < newSceneIndex)
			{
				m_CurrentSceneIndex -= 1;
			}
		}

		Print("Deleting scene %s\n", scene->GetFileName().c_str());

		scene->DeleteSaveFiles();
		delete scene;
		m_Scenes.erase(m_Scenes.begin() + oldSceneIndex);


		if (bDeletingCurrentScene)
		{
			if (m_CurrentSceneIndex == m_Scenes.size())
			{
				m_CurrentSceneIndex--;
			}

			InitializeCurrentScene();
			PostInitializeCurrentScene();
		}
	}

	void SceneManager::CreateNewScene(const std::string& name, bool bSwitchImmediately)
	{
		const i32 newSceneIndex = m_Scenes.size();

		std::string fileName = name + ".json";

		BaseScene* newScene = new BaseScene(fileName);
		newScene->SetName(name);

		Print("Created new scene %s\n", name.c_str());

		m_Scenes.push_back(newScene);

		if (bSwitchImmediately)
		{
			SetCurrentScene(newSceneIndex);
			InitializeCurrentScene();
			PostInitializeCurrentScene();
		}

		newScene->SerializeToFile(true);
	}

	void SceneManager::DrawImGuiObjects()
	{
		if (ImGui::TreeNode("Scenes"))
		{
			if (ImGui::Button("<"))
			{
				SetPreviousSceneActive();
				InitializeCurrentScene();
				PostInitializeCurrentScene();
			}

			ImGui::SameLine();

			BaseScene* currentScene = CurrentScene();

			const std::string currentSceneNameStr(currentScene->GetName() + (currentScene->IsUsingSaveFile() ? " (saved)" : " (default)"));
			ImGui::Text("%s", currentSceneNameStr.c_str());

			DoSceneContextMenu(currentScene);

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
				InitializeCurrentScene();
				PostInitializeCurrentScene();
			}

			ImGui::Checkbox("Spawn player", &currentScene->m_bSpawnPlayer);

			i32 sceneItemWidth = 240;
			if (ImGui::BeginChild("Scenes", ImVec2((real)sceneItemWidth, 120), true, ImGuiWindowFlags_NoResize))
			{
				for (i32 i = 0; i < (i32)m_Scenes.size(); ++i)
				{
					bool bSceneSelected = (i == (i32)m_CurrentSceneIndex);
					BaseScene* scene = GetSceneAtIndex(i);
					std::string sceneFileName = scene->GetFileName();
					if (ImGui::Selectable(sceneFileName.c_str(), &bSceneSelected, 0, ImVec2((real)sceneItemWidth, 0)))
					{
						if (i != (i32)m_CurrentSceneIndex)
						{
							if (SetCurrentScene(i))
							{
								InitializeCurrentScene();
								PostInitializeCurrentScene();
							}
						}
					}

					DoSceneContextMenu(scene);
				}
			}
			ImGui::EndChild();

			if (ImGui::Button("New scene..."))
			{
				ImGui::OpenPopup("New scene");
			}

			if (ImGui::BeginPopupModal("New scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				static std::string newSceneName = "scene_" + IntToString(GetSceneCount(), 2);

				const size_t maxStrLen = 256;
				newSceneName.resize(maxStrLen);
				bool bCreate = ImGui::InputText("Scene name",
					(char*)newSceneName.data(),
					maxStrLen,
					ImGuiInputTextFlags_EnterReturnsTrue);

				bCreate |= ImGui::Button("Create");

				if (bCreate)
				{
					// Remove trailing '\0' characters
					newSceneName = std::string(newSceneName.c_str());
					newSceneName = MakeSceneNameUnique(newSceneName);
					CreateNewScene(newSceneName, true);

					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Refresh scenes"))
			{
				AddFoundScenes();
				RemoveDeletedScenes();
			}

			ImGui::TreePop();
		}
	}

	void SceneManager::DoSceneContextMenu(BaseScene* scene)
	{
		bool bClicked = ImGui::IsMouseReleased(1) &&
			ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

		BaseScene* currentScene = CurrentScene();

		std::string contextMenuID = "scene context menu " + scene->GetFileName();
		if (ImGui::BeginPopupContextItem(contextMenuID.c_str()))
		{
			{
				const i32 sceneNameMaxCharCount = 256;

				// We don't know the names of scene's that haven't been loaded
				if (scene->IsLoaded())
				{
					static char newSceneName[sceneNameMaxCharCount];
					if (bClicked)
					{
						strcpy_s(newSceneName, scene->GetName().c_str());
					}

					bool bRenameScene = ImGui::InputText("##rename-scene",
						newSceneName,
						sceneNameMaxCharCount,
						ImGuiInputTextFlags_EnterReturnsTrue);

					ImGui::SameLine();

					bRenameScene |= ImGui::Button("Rename scene");

					if (bRenameScene)
					{
						scene->SetName(newSceneName);
						// Don't close popup here since we will likely want to save that change
					}
				}

				static char newSceneFileName[sceneNameMaxCharCount];
				if (bClicked)
				{
					strcpy_s(newSceneFileName, scene->GetFileName().c_str());
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
					std::string fileDir = ExtractDirectoryString(RelativePathToAbsolute(scene->GetDefaultRelativeFilePath()));
					std::string newSceneFilePath = fileDir + newSceneFileNameStr;
					bool bNameEmpty = newSceneFileNameStr.empty();
					bool bCorrectFileType = EndsWith(newSceneFileNameStr, ".json");
					bool bFileExists = FileExists(newSceneFilePath);
					bool bSceneNameValid = (!bNameEmpty && bCorrectFileType && !bFileExists);

					if (bSceneNameValid)
					{
						if (scene->SetFileName(newSceneFileNameStr, true))
						{
							ImGui::CloseCurrentPopup();
						}
					}
					else
					{
						PrintError("Attempted name scene with invalid name: %s\n", newSceneFileNameStr.c_str());
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

			// Only allow current scene to be saved
			if (currentScene == scene)
			{
				if (ImGui::Button("Save"))
				{
					scene->SerializeToFile(false);

					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);

				if (ImGui::Button("Save over default"))
				{
					scene->SerializeToFile(true);

					ImGui::CloseCurrentPopup();
				}

				if (scene->IsUsingSaveFile())
				{
					ImGui::SameLine();

					if (ImGui::Button("Hard reload (deletes save file!)"))
					{
						DeleteFile(scene->GetRelativeFilePath());
						ReloadCurrentScene();

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

				std::string newSceneNameStr = scene->GetName();
				newSceneNameStr += " Copy";
				strcpy_s(newSceneName, newSceneNameStr.c_str());

				std::string newSceneFileNameStr = StripFileType(scene->GetFileName());

				bool bValidName = false;
				do
				{
					i16 numNumericalChars = 0;
					i32 numEndingWith = GetNumberEndingWith(newSceneFileNameStr, numNumericalChars);
					if (numNumericalChars > 0)
					{
						u32 charsBeforeNum = (newSceneFileNameStr.length() - numNumericalChars);
						newSceneFileNameStr = newSceneFileNameStr.substr(0, charsBeforeNum) +
							IntToString(numEndingWith + 1, numNumericalChars);
					}
					else
					{
						newSceneFileNameStr += "_01";
					}

					std::string filePathFrom = RelativePathToAbsolute(scene->GetDefaultRelativeFilePath());
					std::string fullNewFilePath = ExtractDirectoryString(filePathFrom);
					fullNewFilePath += newSceneFileNameStr + ".json";
					bValidName = !FileExists(fullNewFilePath);
				} while (!bValidName);

				newSceneFileNameStr += ".json";

				strcpy_s(newSceneFileName, newSceneFileNameStr.c_str());
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
					std::string filePathFrom = RelativePathToAbsolute(scene->GetDefaultRelativeFilePath());
					std::string filePathTo = ExtractDirectoryString(filePathFrom) + newSceneFileName;

					if (FileExists(filePathTo))
					{
						PrintError("Attempting to duplicate scene onto already existing file name!\n");
					}
					else
					{
						if (CopyFile(filePathFrom, filePathTo))
						{
							BaseScene* newScene = new BaseScene(newSceneFileName);
							AddScene(newScene);
							SetCurrentScene(newScene);

							InitializeCurrentScene();
							PostInitializeCurrentScene();
							newScene->SetName(newSceneName);

							CurrentScene()->SerializeToFile(true);

							bCloseContextMenu = true;

							ImGui::CloseCurrentPopup();
						}
						else
						{
							PrintError("Failed to copy scene's file to %s\n", newSceneFileName);
						}
					}
				}

				ImGui::SameLine();

				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Open in explorer"))
			{
				const std::string directory = RelativePathToAbsolute(ExtractDirectoryString(currentScene->GetRelativeFilePath()));
				OpenExplorer(directory);
			}

			ImGui::SameLine();

			const char* deleteScenePopupID = "Delete scene";

			ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);

			if (ImGui::Button("Delete scene..."))
			{
				ImGui::OpenPopup(deleteScenePopupID);
			}

			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();

			if (ImGui::BeginPopupModal(deleteScenePopupID, NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				static std::string sceneName = CurrentScene()->GetName();

				ImGui::PushStyleColor(ImGuiCol_Text, g_WarningTextColor);
				std::string textStr = "Are you sure you want to permanently delete " + sceneName + "? (both the default & saved files)";
				ImGui::Text("%s", textStr.c_str());
				ImGui::PopStyleColor();

				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);
				if (ImGui::Button("Delete"))
				{
					DeleteScene(scene);

					ImGui::CloseCurrentPopup();

					bCloseContextMenu = true;
				}
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();

				ImGui::SameLine();

				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			if (bCloseContextMenu)
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	u32 SceneManager::CurrentSceneIndex() const
	{
		return m_CurrentSceneIndex;
	}

	BaseScene* SceneManager::CurrentScene() const
	{
		return m_Scenes[m_CurrentSceneIndex];
	}

	u32 SceneManager::GetSceneCount() const
	{
		return m_Scenes.size();
	}

	i32 SceneManager::GetCurrentSceneIndex() const
	{
		return m_CurrentSceneIndex;
	}

	BaseScene* SceneManager::GetSceneAtIndex(i32 index)
	{
		assert(index >= 0);
		assert(index < (i32)m_Scenes.size());

		return m_Scenes[index];
	}

	void SceneManager::DestroyAllScenes()
	{
		m_Scenes[m_CurrentSceneIndex]->Destroy();

		auto iter = m_Scenes.begin();
		while (iter != m_Scenes.end())
		{
			delete *iter;
			iter = m_Scenes.erase(iter);
		}
		m_Scenes.clear();
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
} // namespace flex
