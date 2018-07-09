	#include "stdafx.hpp"

#include "Scene/SceneManager.hpp"

#include <algorithm>

#include "FlexEngine.hpp"
#include "Cameras/CameraManager.hpp"

namespace flex
{
	SceneManager::SceneManager() :
		m_CurrentSceneIndex(0)
	{
		m_SavedDirStr = RelativePathToAbsolute(RESOURCE_LOCATION + "scenes/saved");
		m_DefaultDirStr = RelativePathToAbsolute(RESOURCE_LOCATION + "scenes/default");

		if (!DirectoryExists(m_SavedDirStr))
		{
			CreateDirectoryRecursive(m_SavedDirStr);
		}
	}

	SceneManager::~SceneManager()
	{
	}

	void SceneManager::UpdateAndRender()
	{
		if (m_Scenes.empty())
		{
			PrintError("No scenes ded to SceneManager\n");
			return;
		}

		m_Scenes[m_CurrentSceneIndex]->Update();
	}

	void SceneManager::AddScene(BaseScene* newScene)
	{
		bool unique = true;
		std::for_each(m_Scenes.begin(), m_Scenes.end(), [&unique, newScene](BaseScene* scene) mutable
		{
			if (scene->GetRelativeFilePath().compare(newScene->GetRelativeFilePath()) == 0)
			{
				unique = false;
			}
		});

		if (unique)
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

		CurrentScene()->Initialize();

		g_Renderer->OnSceneChanged();
		g_EngineInstance->OnSceneChanged();
		g_CameraManager->OnSceneChanged();
	}

	void SceneManager::PostInitializeCurrentScene()
	{
		assert(!m_Scenes.empty());

		CurrentScene()->PostInitialize();
	}

	void SceneManager::RemoveScene(BaseScene* scene)
	{
		auto iter = std::find(m_Scenes.begin(), m_Scenes.end(), scene);
		if (iter != m_Scenes.end())
		{
			scene->Destroy();
			SafeDelete(scene);
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
			PrintError("Attempt to set scene to index %i failed, it does not exist in the SceneManager\n",
					   sceneIndex);
			return false;
		}

		g_EngineInstance->PreSceneChange();

		if (m_CurrentSceneIndex != u32_max)
		{
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
				return SetCurrentScene(i,bPrintErrorOnFailure);
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
				return SetCurrentScene(i,bPrintErrorOnFailure);
			}
		}

		if (bPrintErrorOnFailure)
		{
			PrintError("Attempt to set scene failed, it does not exist in the SceneManager: %s\n", sceneFileName.c_str());
		}

		return false;
	}

	void SceneManager::SetNextSceneActiveAndInit()
	{
		const size_t sceneCount = m_Scenes.size();
		if (sceneCount == 1)
		{
			return;
		}
		
		u32 newCurrentSceneIndex = (m_CurrentSceneIndex + 1) % m_Scenes.size();
		SetCurrentScene(newCurrentSceneIndex);

		InitializeCurrentScene();
		PostInitializeCurrentScene();
	}

	void SceneManager::SetPreviousSceneActiveAndInit()
	{
		const size_t sceneCount = m_Scenes.size();
		if (sceneCount == 1)
		{
			return;
		}

		// Loop around to previous index but stay positive cause things are unsigned
		u32 newCurrentSceneIndex = (m_CurrentSceneIndex + m_Scenes.size() - 1) % m_Scenes.size();
		SetCurrentScene(newCurrentSceneIndex);

		InitializeCurrentScene();
		PostInitializeCurrentScene();
	}

	void SceneManager::ReloadCurrentScene()
	{
		SetCurrentScene(m_CurrentSceneIndex);

		InitializeCurrentScene();
		PostInitializeCurrentScene();
	}

	void SceneManager::AddFoundScenes()
	{
		std::vector<std::string> addedSceneFileNames;

		// Find and load all saved scene files
		std::vector<std::string> foundFileNames;
		if (FindFilesInDirectory(m_SavedDirStr, foundFileNames, "json"))
		{
			for (std::string& fileName : foundFileNames)
			{
				StripLeadingDirectories(fileName);

				if (!SceneExists(fileName))
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
				StripLeadingDirectories(fileName);
				
				if (!SceneExists(fileName))
				{
					BaseScene* newScene = new BaseScene(fileName);
					m_Scenes.push_back(newScene);

					addedSceneFileNames.push_back(fileName);
				}
			}
		}

		if (!addedSceneFileNames.empty())
		{
			Print("Added %i scenes to list:\n", addedSceneFileNames.size());
			for (std::string& fileName : addedSceneFileNames)
			{
				Print("%s ", fileName.c_str());

			}
			Print("\n");
		}
	}

	void SceneManager::RemoveDeletedScenes()
	{
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
			PrintError("Attempted to delete scene not present in scene manager: %s\n ", scene->GetName());
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
			
			g_EngineInstance->PreSceneChange();
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
		SafeDelete(scene);
		m_Scenes.erase(m_Scenes.begin() + oldSceneIndex);


		if (bDeletingCurrentScene)
		{
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
			SafeDelete(*iter);
			iter = m_Scenes.erase(iter);
		}
		m_Scenes.clear();
	}

	bool SceneManager::SceneExists(const std::string& fileName) const
	{
		for (BaseScene* scene : m_Scenes)
		{
			std::string existingSceneFileName = scene->GetFileName();

			if (existingSceneFileName.compare(fileName) == 0)
			{
				return true;
			}
		}

		return false;
	}
} // namespace flex
