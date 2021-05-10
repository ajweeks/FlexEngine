#pragma once

#include "Graphics/RendererTypes.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	class PhysicsWorld;
	class ReflectionProbe;
	class Player;
	class PointLight;
	class DirectionalLight;
	struct JSONObject;
	struct JSONField;
	struct Material;
	class ICallbackGameObject;

	class BaseScene final
	{
	public:
		// fileName e.g. "scene_01.json"
		explicit BaseScene(const std::string& fileName);
		~BaseScene();

		void Initialize();
		void PostInitialize();
		void Destroy();
		void Update();
		void LateUpdate();

		void OnPrefabChanged(const PrefabID& prefabID);

		bool LoadFromFile(const std::string& filePath);
		void CreateBlank(const std::string& filePath);

		void DrawImGuiObjects();
		void DoSceneContextMenu();

		void BindOnGameObjectDestroyedCallback(ICallbackGameObject* callback);
		void UnbindOnGameObjectDestroyedCallback(ICallbackGameObject* callback);

		void SetName(const std::string& name);
		std::string GetName() const;
		std::string GetDefaultRelativeFilePath() const;
		std::string GetRelativeFilePath() const;
		std::string GetShortRelativeFilePath() const;
		// Creates a new file at the specified location and copies this scene's data in to it
		// Always copies saved file if exists, old files can optionally be deleted
		bool SetFileName(const std::string& fileName, bool bDeletePreviousFiles);
		std::string GetFileName() const;

		bool IsUsingSaveFile() const;

		PhysicsWorld* GetPhysicsWorld();

		/*
		* Serializes all data from scene into JSON scene file.
		* Only writes data that has non-default values (e.g. an identity
		* transform is not saved)
		*/
		void SerializeToFile(bool bSaveOverDefault = false) const;

		void DeleteSaveFiles();

		std::vector<GameObject*>& GetRootObjects();
		void GetInteractableObjects(std::vector<GameObject*>& interactableObjects);

		GameObject* AddRootObject(GameObject* gameObject);
		GameObject* AddRootObjectImmediate(GameObject* gameObject);
		GameObject* AddChildObject(GameObject* parent, GameObject* child);
		GameObject* AddChildObjectImmediate(GameObject* parent, GameObject* child);
		void RemoveAllObjects(); // Removes and destroys all objects in scene at end of frame
		void RemoveAllObjectsImmediate();  // Removes and destroys all objects in scene
		void RemoveObject(const GameObjectID& gameObjectID, bool bDestroy);
		void RemoveObject(GameObject* gameObject, bool bDestroy);
		void RemoveObjectImmediate(const GameObjectID& gameObjectID, bool bDestroy);
		void RemoveObjectImmediate(GameObject* gameObject, bool bDestroy);
		void RemoveObjects(const std::vector<GameObjectID>& gameObjects, bool bDestroy);
		void RemoveObjects(const std::vector<GameObject*>& gameObjects, bool bDestroy);
		void RemoveObjectsImmediate(const std::vector<GameObjectID>& gameObjects, bool bDestroy);
		void RemoveObjectsImmediate(const std::vector<GameObject*>& gameObjects, bool bDestroy);

		GameObject* InstantiatePrefab(const PrefabID& prefabID, GameObject* parent = nullptr);
		GameObject* ReplacePrefab(const PrefabID& prefabID, GameObject* previousInstance);

		GameObjectID FirstObjectWithTag(const std::string& tag);

		Player* GetPlayer(i32 index);

		bool IsLoaded() const;

		std::vector<GameObject*> GetAllObjects();
		std::vector<GameObjectID> GetAllObjectIDs();

		template<class T>
		std::vector<T*> GetObjectsOfType(StringID typeID)
		{
			std::vector<GameObject*> gameObjects = GetAllObjects();
			std::vector<T*> result;

			for (GameObject* gameObject : gameObjects)
			{
				if (gameObject->GetTypeID() == typeID)
				{
					result.push_back((T*)gameObject);
				}
			}

			return result;
		}

		std::string GetUniqueObjectName(const std::string& existingName);
		// Returns 'prefix' with a number appended representing
		// how many other objects with that prefix are in the scene
		std::string GetUniqueObjectName(const std::string& prefix, i16 digits);

		i32 GetSceneFileVersion() const;

		bool HasPlayers() const;

		const SkyboxData& GetSkyboxData() const;

		void DrawImGuiForSelectedObjects();
		void DrawImGuiForRenderObjectsList();

		// If the object gets deleted this frame *gameObjectRef gets set to nullptr
		void DoCreateGameObjectButton(const char* buttonName, const char* popupName);
		bool DrawImGuiGameObjectNameAndChildren(GameObject* gameObject);
		// Returns true if the parent-child tree changed during this call
		bool DrawImGuiGameObjectNameAndChildrenInternal(GameObject* gameObject);

		GameObject* GetGameObject(const GameObjectID& gameObjectID) const;

		bool DrawImGuiGameObjectIDField(const char* label, GameObjectID& ID, bool bReadOnly = false);

		void SetTimeOfDay(real time);
		real GetTimeOfDay() const;
		void SetSecondsPerDay(real secPerDay);
		real GetSecondsPerDay() const;
		void SetTimeOfDayPaused(bool bPaused);
		bool GetTimeOfDayPaused() const;

		real GetPlayerMinHeight() const;
		glm::vec3 GetPlayerSpawnPoint() const;

		static const char* GameObjectTypeIDToString(StringID typeID);

		static std::map<StringID, std::string> GameObjectTypeStringIDPairs;

		static const i32 LATEST_SCENE_FILE_VERSION = 6;
		static const i32 LATEST_MATERIALS_FILE_VERSION = 1;
		static const i32 LATEST_MESHES_FILE_VERSION = 1;
		static const i32 LATETST_PREFAB_FILE_VERSION = 3;

	protected:
		friend GameObject;
		friend SceneManager;

		void RemoveObjectImmediateRecursive(const GameObjectID& gameObjectID, bool bDestroy);

		void UpdateRootObjectSiblingIndices();
		void RegisterGameObject(GameObject* gameObject);
		void UnregisterGameObject(const GameObjectID& gameObjectID);

		void CreateNewGameObject(const std::string& newObjectName, GameObject* parent = nullptr);

		i32 m_SceneFileVersion = 1;
		i32 m_MaterialsFileVersion = 1;
		i32 m_MeshesFileVersion = 1;

		std::string m_Name;
		std::string m_FileName;

		PhysicsWorld* m_PhysicsWorld = nullptr;

		std::map<GameObjectID, GameObject*> m_GameObjectLUT;
		std::vector<GameObject*> m_RootObjects;

		bool m_bInitialized = false;
		bool m_bLoaded = false;
		bool m_bSpawnPlayer = false;
		GameObjectID m_PlayerGUIDs[2];

		ReflectionProbe* m_ReflectionProbe = nullptr;

		bool m_bPauseTimeOfDay = false;
		real m_TimeOfDay; // [0, 1) - 0 = noon, 0.5 = midnight
		real m_SecondsPerDay = 6000.0f;

		SkyboxData m_SkyboxDatas[4];
		SkyboxData m_SkyboxData;

		// Kill zone for player
		real m_PlayerMinHeight = -500.0f;
		glm::vec3 m_PlayerSpawnPoint;

		Player* m_Player0 = nullptr;
		Player* m_Player1 = nullptr;

		std::vector<ICallbackGameObject*> m_OnGameObjectDestroyedCallbacks;

		std::vector<GameObject*> m_PendingAddObjects; // Objects to add as root objects at LateUpdate
		std::vector<Pair<GameObject*, GameObject*>> m_PendingAddChildObjects; // Objects to add to parents at LateUpdate
		std::vector<GameObjectID> m_PendingRemoveObjects; // Objects to remove but not destroy at LateUpdate this frame
		std::vector<GameObjectID> m_PendingDestroyObjects; // Objects to destroy at LateUpdate this frame

	private:
		/*
		* Recursively searches through all game objects and returns first
		* one containing given tag, or nullptr if none exist
		*/
		GameObjectID FindObjectWithTag(const std::string& tag, GameObject* gameObject);

		void OnPrefabChangedInternal(const PrefabID& prefabID, GameObject* prefabTemplate, GameObject* rootObject);

		void ReadGameObjectTypesFile();
		void WriteGameObjectTypesFile();

		BaseScene(const BaseScene&) = delete;
		BaseScene& operator=(const BaseScene&) = delete;

		Pair<StringID, std::string> m_NewObjectTypeIDPair;

	};
} // namespace flex
