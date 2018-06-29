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
			if (scene->GetFilePath().compare(newScene->GetFilePath()) == 0)
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

		gameContext.engineInstance->PreSceneChange();

		CurrentScene()->Initialize(gameContext);

		gameContext.renderer->OnSceneChanged();
		gameContext.engineInstance->OnSceneChanged();
		gameContext.cameraManager->Initialize(gameContext);
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

	void SceneManager::SetCurrentScene(u32 sceneIndex, const GameContext& gameContext)
	{
		if (sceneIndex >= m_Scenes.size())
		{
			Logger::LogError("Attempt to set scene to index " + std::to_string(sceneIndex) +
				" failed, it does not exist in the SceneManager");
			return;
		}

		gameContext.engineInstance->PreSceneChange();

		if (m_CurrentSceneIndex != u32_max)
		{
			m_Scenes[m_CurrentSceneIndex]->Destroy(gameContext);
		}

		m_CurrentSceneIndex = sceneIndex;
	}

	void SceneManager::SetCurrentScene(BaseScene* scene, const GameContext& gameContext)
	{
		for (size_t i = 0; i < m_Scenes.size(); ++i)
		{
			if (m_Scenes[i]->GetName().compare(scene->GetName()) == 0)
			{
				SetCurrentScene(i, gameContext);
				return;
			}
		}

		Logger::LogError("Attempt to set current scene to " + scene->GetName() + " failed because it was not found in the scene manager!");
	}

	void SceneManager::SetCurrentScene(const std::string& sceneFileName, const GameContext& gameContext)
	{
		for (size_t i = 0; i < m_Scenes.size(); ++i)
		{
			if (m_Scenes[i]->GetFileName().compare(sceneFileName) == 0)
			{
				SetCurrentScene(i, gameContext);
				return;
			}
		}

		Logger::LogError("Attempt to set scene to " + sceneFileName + " failed, it does not exist in the SceneManager");
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
		// Find and load all saved scene files
		std::vector<std::string> foundFileNames;
		if (FindFilesInDirectory(m_SavedDirStr, foundFileNames, "json"))
		{
			for (auto iter = foundFileNames.begin(); iter != foundFileNames.end(); ++iter)
			{
				std::string fileName = *iter;
				StripLeadingDirectories(fileName);

				BaseScene* newScene = new BaseScene(fileName);
				m_Scenes.push_back(newScene);
			}
		}

		// Load the default for any scenes which don't have a corresponding save file
		foundFileNames.clear();
		if (FindFilesInDirectory(m_DefaultDirStr, foundFileNames, "json"))
		{
			for (auto iter = foundFileNames.begin(); iter != foundFileNames.end(); ++iter)
			{
				std::string filePath = *iter;
				
				bool sceneAlreadyAdded = false;
				for (auto sceneIter = m_Scenes.begin(); sceneIter != m_Scenes.end(); ++sceneIter)
				{
					std::string foundFileName = filePath;
					StripLeadingDirectories(foundFileName);

					std::string sceneFileName = (*sceneIter)->GetFilePath();
					StripLeadingDirectories(sceneFileName);

					if (sceneFileName.compare(foundFileName) == 0)
					{
						sceneAlreadyAdded = true;
						break;
					}
				}

				if (!sceneAlreadyAdded)
				{
					std::string fileName = filePath;
					StripLeadingDirectories(fileName);
					BaseScene* newScene = new BaseScene(fileName);
					m_Scenes.push_back(newScene);
				}
			}
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
} // namespace flex
