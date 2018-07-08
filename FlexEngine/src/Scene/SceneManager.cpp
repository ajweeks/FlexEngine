	#include "stdafx.hpp"

#include "Scene/SceneManager.hpp"

#include <algorithm>

#include "Logger.hpp"
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

	void SceneManager::UpdateAndRender(const GameContext& gameContext)
	{
		if (m_Scenes.empty())
		{
			Logger::LogError("No scenes added to SceneManager");
			return;
		}

		m_Scenes[m_CurrentSceneIndex]->Update(gameContext);
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
			Logger::LogError("Attempt to add already existing scene to SceneManager: " + newScene->GetName());
		}
	}

	void SceneManager::InitializeCurrentScene(const GameContext& gameContext)
	{
		assert(!m_Scenes.empty());

		//gameContext.engineInstance->PreSceneChange();

		CurrentScene()->Initialize(gameContext);

		gameContext.renderer->OnSceneChanged();
		gameContext.engineInstance->OnSceneChanged();
		gameContext.cameraManager->OnSceneChanged(gameContext);
	}

	void SceneManager::PostInitializeCurrentScene(const GameContext& gameContext)
	{
		assert(!m_Scenes.empty());

		CurrentScene()->PostInitialize(gameContext);
	}

	void SceneManager::RemoveScene(BaseScene* scene, const GameContext& gameContext)
	{
		auto iter = std::find(m_Scenes.begin(), m_Scenes.end(), scene);
		if (iter != m_Scenes.end())
		{
			scene->Destroy(gameContext);
			SafeDelete(scene);
			m_Scenes.erase(iter);
		}
		else
		{
			Logger::LogError("Attempt to remove non-existent scene from SceneManager: " + scene->GetName());
		}
	}

	bool SceneManager::SetCurrentScene(u32 sceneIndex, const GameContext& gameContext, bool bPrintErrorOnFailure /* = true */)
	{
		if (bPrintErrorOnFailure && sceneIndex >= m_Scenes.size())
		{
			Logger::LogError("Attempt to set scene to index " + std::to_string(sceneIndex) +
				" failed, it does not exist in the SceneManager");
			return false;
		}

		gameContext.engineInstance->PreSceneChange();

		if (m_CurrentSceneIndex != u32_max)
		{
			m_Scenes[m_CurrentSceneIndex]->Destroy(gameContext);
		}

		m_CurrentSceneIndex = sceneIndex;

		return true;
	}

	bool SceneManager::SetCurrentScene(BaseScene* scene, const GameContext& gameContext, bool bPrintErrorOnFailure /* = true */)
	{
		for (size_t i = 0; i < m_Scenes.size(); ++i)
		{
			if (m_Scenes[i]->GetFileName().compare(scene->GetFileName()) == 0)
			{
				return SetCurrentScene(i, gameContext, bPrintErrorOnFailure);
			}
		}

		if (bPrintErrorOnFailure)
		{
			Logger::LogError("Attempt to set current scene to " + scene->GetName() + " failed because it was not found in the scene manager!");
		}

		return false;
	}

	bool SceneManager::SetCurrentScene(const std::string& sceneFileName, const GameContext& gameContext, bool bPrintErrorOnFailure /* = true */)
	{
		for (size_t i = 0; i < m_Scenes.size(); ++i)
		{
			if (m_Scenes[i]->GetFileName().compare(sceneFileName) == 0)
			{
				return SetCurrentScene(i, gameContext, bPrintErrorOnFailure);
			}
		}

		if (bPrintErrorOnFailure)
		{
			Logger::LogError("Attempt to set scene to " + sceneFileName + " failed, it does not exist in the SceneManager");
		}

		return false;
	}

	void SceneManager::SetNextSceneActiveAndInit(const GameContext& gameContext)
	{
		const size_t sceneCount = m_Scenes.size();
		if (sceneCount == 1)
		{
			return;
		}
		
		u32 newCurrentSceneIndex = (m_CurrentSceneIndex + 1) % m_Scenes.size();
		SetCurrentScene(newCurrentSceneIndex, gameContext);

		InitializeCurrentScene(gameContext);
		PostInitializeCurrentScene(gameContext);
	}

	void SceneManager::SetPreviousSceneActiveAndInit(const GameContext& gameContext)
	{
		const size_t sceneCount = m_Scenes.size();
		if (sceneCount == 1)
		{
			return;
		}

		// Loop around to previous index but stay positive cause things are unsigned
		u32 newCurrentSceneIndex = (m_CurrentSceneIndex + m_Scenes.size() - 1) % m_Scenes.size();
		SetCurrentScene(newCurrentSceneIndex, gameContext);

		InitializeCurrentScene(gameContext);
		PostInitializeCurrentScene(gameContext);
	}

	void SceneManager::ReloadCurrentScene(const GameContext& gameContext)
	{
		SetCurrentScene(m_CurrentSceneIndex, gameContext);

		InitializeCurrentScene(gameContext);
		PostInitializeCurrentScene(gameContext);
	}

	void SceneManager::AddFoundScenes()
	{
		std::vector<std::string> addedSceneFileNames;

		// Find and load all saved scene files
		std::vector<std::string> foundFileNames;
		if (FindFilesInDirectory(m_SavedDirStr, foundFileNames, "json"))
		{
			for (auto iter = foundFileNames.begin(); iter != foundFileNames.end(); ++iter)
			{
				std::string fileName = *iter;
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
			for (auto iter = foundFileNames.begin(); iter != foundFileNames.end(); ++iter)
			{
				std::string fileName = *iter;
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
			Logger::LogInfo("Added " + IntToString(addedSceneFileNames.size()) + " scenes to list:");
			for (std::string& fileName : addedSceneFileNames)
			{
				Logger::LogInfo(fileName + " ", false);

			}
			Logger::LogNewLine();
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
					Logger::LogWarning("Current scene's JSON file is missing (" + fileName + ")! Save now to keep your changes");
					++sceneIter;
				}
				else
				{
					Logger::LogInfo("Removing scene from list due to JSON file missing: " + fileName);

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

	void SceneManager::DeleteScene(const GameContext& gameContext, BaseScene* scene)
	{
		if (m_Scenes.size() == 1)
		{
			Logger::LogWarning("Attempted to delete only remaining scene!");
			return;
		}

		if (!scene)
		{
			Logger::LogWarning("Attempted to delete invalid scene!");
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
			Logger::LogError("Attempted to delete scene not present in scene manager! " + scene->GetName());
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
			
			gameContext.engineInstance->PreSceneChange();
			scene->Destroy(gameContext);
		}
		else
		{
			if (oldSceneIndex < newSceneIndex)
			{
				m_CurrentSceneIndex -= 1;
			}
		}

		Logger::LogInfo("Deleting scene " + scene->GetFileName());

		scene->DeleteSaveFiles();
		SafeDelete(scene);
		m_Scenes.erase(m_Scenes.begin() + oldSceneIndex);


		if (bDeletingCurrentScene)
		{
			InitializeCurrentScene(gameContext);
			PostInitializeCurrentScene(gameContext);
		}
	}

	void SceneManager::CreateNewScene(const GameContext& gameContext, const std::string& name, bool bSwitchImmediately)
	{
		const i32 newSceneIndex = m_Scenes.size();

		std::string fileName = name + ".json";

		BaseScene* newScene = new BaseScene(fileName);
		newScene->SetName(name);

		Logger::LogInfo("Created new scene " + name);

		m_Scenes.push_back(newScene);

		if (bSwitchImmediately)
		{
			SetCurrentScene(newSceneIndex, gameContext);
			InitializeCurrentScene(gameContext);
			PostInitializeCurrentScene(gameContext);
		}

		newScene->SerializeToFile(gameContext, true);
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

	void SceneManager::DestroyAllScenes(const GameContext& gameContext)
	{
		m_Scenes[m_CurrentSceneIndex]->Destroy(gameContext);

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
		for (auto sceneIter = m_Scenes.begin(); sceneIter != m_Scenes.end(); ++sceneIter)
		{
			std::string existingSceneFileName = (*sceneIter)->GetFileName();

			if (existingSceneFileName.compare(fileName) == 0)
			{
				return true;
			}
		}

		return false;
	}
} // namespace flex
