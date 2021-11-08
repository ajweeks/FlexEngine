#pragma once

namespace flex
{
	class BaseScene;

	class SceneManager final
	{
	public:
		SceneManager();
		~SceneManager() = default;

		SceneManager(const SceneManager&&) = delete;
		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

		void Destroy();

		// Destroys previous scene if exists, then sets current index
		bool SetCurrentScene(u32 sceneIndex, bool bPrintErrorOnFailure = true);
		bool SetCurrentScene(BaseScene* scene, bool bPrintErrorOnFailure = true);
		bool SetCurrentScene(const std::string& sceneFileName, bool bPrintErrorOnFailure = true);
		void SetNextSceneActive();
		void SetPreviousSceneActive();
		void ReloadCurrentScene();

		void CreateNewScene(const std::string& name, bool bSwitchImmediately);
		void DeleteScene(BaseScene* scene);

		void DrawImGuiObjects();
		void DrawImGuiModals();

		BaseScene* CurrentScene() const;
		bool HasSceneLoaded() const; // False during initial load

		bool DuplicateScene(BaseScene* scene, const std::string& newSceneFileName, const std::string& newSceneName);

		void OpenNewSceneWindow();

		void ReadGameObjectTypesFile();
		void WriteGameObjectTypesFile();

		static std::map<StringID, std::string> GameObjectTypeStringIDPairs;

		bool bOpenNewSceneWindow = false;
		bool bOpenNewObjectTypePopup = false;

	private:
		std::string MakeSceneNameUnique(const std::string& originalName);
		bool SceneFileExists(const std::string& fileName) const;

		// Adds all scenes found in scenes directory
		void AddFoundScenes();

		void AddScene(BaseScene* newScene);
		void RemoveDeletedScenes();

		/* To be called after AddScene */
		void InitializeCurrentScene(u32 previousSceneIndex);

		/* To be called after InitializeCurrentScene */
		void PostInitializeCurrentScene();

		u32 m_CurrentSceneIndex = InvalidID;
		std::vector<BaseScene*> m_Scenes;

		std::string m_DefaultDirStr;

		static const char* s_NewObjectTypePopupStr;
		std::string m_NewObjectTypeStrBuffer;

		static const char* s_newSceneModalWindowID;

	};
} // namespace flex
