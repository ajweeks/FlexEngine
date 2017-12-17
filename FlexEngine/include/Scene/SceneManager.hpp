#pragma once

#include <vector>

#include "Scene/Scenes/BaseScene.hpp"
#include "GameContext.hpp"

namespace flex
{
	class SceneManager
	{
	public:
		SceneManager();
		virtual ~SceneManager();

		void UpdateAndRender(const GameContext& gameContext);

		void AddScene(BaseScene* newScene, const GameContext& gameContext);
		void RemoveScene(BaseScene* scene, const GameContext& gameContext);

		void SetCurrentScene(BaseScene* scene);
		void SetCurrentScene(u32 sceneIndex);
		void SetCurrentScene(std::string sceneName);
		void SetNextSceneActive();
		void SetPreviousSceneActive();

		u32 CurrentSceneIndex() const;
		BaseScene* CurrentScene() const;
		u32 GetSceneCount() const;

		void DestroyAllScenes(const GameContext& gameContext);

	private:
		u32 m_CurrentSceneIndex;
		std::vector<BaseScene*> m_Scenes;

		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

	};
} // namespace flex
