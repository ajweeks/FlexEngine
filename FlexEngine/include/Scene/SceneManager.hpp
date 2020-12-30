#pragma once

namespace flex
{
	class BaseScene;

	class SceneManager
	{
	public:
		SceneManager();
		virtual ~SceneManager();

		void AddScene(BaseScene* newScene);

		/* To be called after AddScene */
		void InitializeCurrentScene();

		/* To be called after InitializeCurrentScene */
		void PostInitializeCurrentScene();

		void RemoveScene(BaseScene* scene);

		/* Destroys previous scene if exists, then sets current index (NOTE: Does *not* initialize new scene! */
		bool SetCurrentScene(u32 sceneIndex, bool bPrintErrorOnFailure = true);
		bool SetCurrentScene(BaseScene* scene, bool bPrintErrorOnFailure = true);
		bool SetCurrentScene(const std::string& sceneFileName, bool bPrintErrorOnFailure = true);
		void SetNextSceneActive();
		void SetPreviousSceneActive();
		void ReloadCurrentScene();

		// Adds all scenes found in scenes directory
		void AddFoundScenes();
		void RemoveDeletedScenes();

		void DeleteScene(BaseScene* scene);
		void CreateNewScene(const std::string& name, bool bSwitchImmediately);

		void DrawImGuiObjects();
		void DrawImGuiModals();

		u32 CurrentSceneIndex() const;
		BaseScene* CurrentScene() const;
		bool CurrentSceneExists() const;
		u32 GetSceneCount() const;

		i32 GetCurrentSceneIndex() const;
		BaseScene* GetSceneAtIndex(i32 index);

		bool DuplicateScene(BaseScene* scene, const std::string& newSceneFileName, const std::string& newSceneName);

		void DestroyAllScenes();

		void OpenNewSceneWindow();

	private:
		std::string MakeSceneNameUnique(const std::string& originalName);
		bool SceneFileExists(const std::string& fileName) const;

		u32 m_CurrentSceneIndex = InvalidID;
		u32 m_PreviousSceneIndex = InvalidID;
		std::vector<BaseScene*> m_Scenes;

		std::string m_SavedDirStr;
		std::string m_DefaultDirStr;

		bool m_bOpenNewSceneWindow = false;

		static const char* s_newSceneModalWindowID;

		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

	};
} // namespace flex
