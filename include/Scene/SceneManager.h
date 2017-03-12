#pragma once

#include <vector>
#include "Scene/BaseScene.h"

class SceneManager
{
public:
	SceneManager();
	virtual ~SceneManager();

	void UpdateAndRender(const GameContext& gameContext);

	void AddScene(BaseScene* newScene);
	void RemoveScene(BaseScene* scene);

	void SetCurrentScene(int sceneIndex);
	void SetCurrentScene(std::string sceneName);

	BaseScene* CurrentScene() const;

private:
	int m_CurrentSceneIndex;
	std::vector<BaseScene*> m_Scenes;

	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

};
