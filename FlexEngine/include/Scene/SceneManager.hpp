#pragma once

#include <vector>

#include "Scene/BaseScene.hpp"
#include "GameContext.hpp"

namespace flex
{
	class SceneManager
	{
	public:
		SceneManager();
		virtual ~SceneManager();

		void UpdateAndRender(const GameContext& gameContext);

		void AddScene(BaseScene* newScene);

		/* To be called after AddScene */
		void InitializeCurrentScene(const GameContext& gameContext);

		/* To be called after InitializeCurrentScene */
		void PostInitializeCurrentScene(const GameContext& gameContext);

		void RemoveScene(BaseScene* scene, const GameContext& gameContext);

		void SetCurrentScene(u32 sceneIndex, const GameContext& gameContext);
		void SetCurrentScene(BaseScene* scene, const GameContext& gameContext);
		void SetCurrentScene(const std::string& sceneName, const GameContext& gameContext);
		void SetNextSceneActive(const GameContext& gameContext);
		void SetPreviousSceneActive(const GameContext& gameContext);
		void ReloadCurrentScene(const GameContext& gameContext);

		// Adds all scenes found in scenes directory
		void AddFoundScenes();

		u32 CurrentSceneIndex() const;
		BaseScene* CurrentScene() const;
		u32 GetSceneCount() const;

		void DestroyAllScenes(const GameContext& gameContext);

	private:
		u32 m_CurrentSceneIndex = u32_max;
		std::vector<BaseScene*> m_Scenes;

		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

	};
} // namespace flex
