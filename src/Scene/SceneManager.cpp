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
	for (size_t i = 0; i < m_Scenes.size(); i++)
	{
		delete m_Scenes[i];
	}
	m_Scenes.clear();
}

void SceneManager::UpdateAndRender(const GameContext& gameContext)
{
	if (m_Scenes.empty())
	{
		Logger::LogError("No scenes have been added! Call SceneManager::AddScene at least once");
		return;
	}

	m_Scenes[m_CurrentSceneIndex]->UpdateAndRender(gameContext);
}

void SceneManager::AddScene(BaseScene* newScene)
{
	if (std::find(m_Scenes.begin(), m_Scenes.end(), newScene) == m_Scenes.end())
	{
		m_Scenes.push_back(newScene);
	}
	else
	{
		Logger::LogError("Did not add scene, it's already been added!");
	}
}

void SceneManager::RemoveScene(BaseScene* scene)
{
	if (std::find(m_Scenes.begin(), m_Scenes.end(), scene) != m_Scenes.end())
	{
		m_Scenes.erase(std::remove(m_Scenes.begin(), m_Scenes.end(), scene), m_Scenes.end());
	}
	else
	{
		Logger::LogError("Could not remove scene, it doesn't exist in the scene manager!");
	}
}

void SceneManager::SetCurrentScene(int sceneIndex)
{
	if (sceneIndex < 0 || sceneIndex >= (int)m_Scenes.size())
	{
		Logger::LogError("Could not set scene to index " + std::to_string(sceneIndex) + 
			", it doesn't exist in the scene manager!");
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
	Logger::LogError("Could not set scene to" + sceneName + ", it doesn't exist in the scene manager!");
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
	for (size_t i = 0; i < m_Scenes.size(); i++)
	{
		m_Scenes[i]->Destroy(gameContext);
	}
}
