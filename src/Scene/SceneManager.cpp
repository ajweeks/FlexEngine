#include "stdafx.h"

#include "Scene/SceneManager.h"
#include "Scene/BaseScene.h"
#include "Logger.h"
#include "GameContext.h"

#include <algorithm>

SceneManager::SceneManager() :
	m_CurrentSceneIndex(0)
{
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

	m_Scenes[m_CurrentSceneIndex]->RootUpdate(gameContext);
}

void SceneManager::AddScene(BaseScene* newScene, const GameContext& gameContext)
{
	if (std::find(m_Scenes.begin(), m_Scenes.end(), newScene) == m_Scenes.end())
	{
		m_Scenes.push_back(newScene);
		newScene->RootInitialize(gameContext);
	}
	else
	{
		Logger::LogError("Attempt to add already existing scene to SceneManager: " + newScene->m_Name);
	}
}

void SceneManager::RemoveScene(BaseScene* scene, const GameContext& gameContext)
{
	auto iter = std::find(m_Scenes.begin(), m_Scenes.end(), scene);
	if (iter != m_Scenes.end())
	{
		scene->RootDestroy(gameContext);
		SafeDelete(scene);
		m_Scenes.erase(iter);
	}
	else
	{
		Logger::LogError("Attempt to remove non-existent scene from SceneManager: " + scene->m_Name);
	}
}

void SceneManager::SetCurrentScene(int sceneIndex)
{
	if (sceneIndex < 0 || sceneIndex >= (int)m_Scenes.size())
	{
		Logger::LogError("Attempt to set scene to index " + std::to_string(sceneIndex) + 
			" failed, it does not exist in the SceneManager");
		return;
	}

	m_CurrentSceneIndex = sceneIndex;
}

void SceneManager::SetCurrentScene(std::string sceneName)
{
	for (size_t i = 0; i < m_Scenes.size(); i++)
	{
		if (m_Scenes[i]->GetName().compare(sceneName) == 0)
		{
			m_CurrentSceneIndex = i;
			return;
		}
	}

	Logger::LogError("Attempt to set scene to " + sceneName + " failed, it does not exist in the SceneManager");
}

BaseScene* SceneManager::CurrentScene() const
{
	if (m_CurrentSceneIndex < 0 || m_CurrentSceneIndex >= (int)m_Scenes.size())
	{
		return nullptr;
	}

	return m_Scenes[m_CurrentSceneIndex];
}

void SceneManager::DestroyAllScenes(const GameContext& gameContext)
{
	auto iter = m_Scenes.begin();
	while (iter != m_Scenes.end())
	{
		(*iter)->RootDestroy(gameContext);
		SafeDelete(*iter);
		iter = m_Scenes.erase(iter);
	}
	m_Scenes.clear();
}
