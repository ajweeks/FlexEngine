#pragma once

#include "Graphics/RendererTypes.hpp"
#include "Managers/CartManager.hpp"
#include "Track/TrackManager.hpp"

namespace flex
{
	class PhysicsWorld;
	class ReflectionProbe;
	class Player;
	class GameObject;
	class PointLight;
	class DirectionalLight;
	struct JSONObject;
	struct JSONField;
	struct Material;
	class ICallbackGameObject;

	class BaseScene
	{
	public:
		// fileName e.g. "scene_01.json"
		explicit BaseScene(const std::string& fileName);
		virtual ~BaseScene();

		virtual void Initialize();
		virtual void PostInitialize();
		virtual void Destroy();
		virtual void Update();
		virtual void LateUpdate();

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
		GameObject* AddChildObject(GameObject* parent, GameObject* gameObject);
		void RemoveRootObject(GameObject* gameObject, bool bDestroy);
		void RemoveRootObjectImmediate(GameObject* gameObject, bool bDestroy);
		void RemoveAllRootObjects(bool bDestroy);
		void RemoveAllRootObjectsImmediate(bool bDestroy);
		bool RemoveObject(GameObject* gameObject, bool bDestroy);
		bool RemoveObjectImmediate(GameObject* gameObject, bool bDestroy);
		void RemoveObjects(const std::vector<GameObject*>& gameObjects, bool bDestroy);
		void RemoveObjectsImmediate(const std::vector<GameObject*>& gameObjects, bool bDestroy);
		std::vector<GameObject*>::const_iterator RemoveObjectImmediate(std::vector<GameObject*>::const_iterator objectIter, bool bDestroy);


		std::vector<MaterialID> GetMaterialIDs();
		void AddMaterialID(MaterialID newMaterialID);
		void RemoveMaterialID(MaterialID materialID);

		/* Returns the first found game object with tag, or nullptr if none exist */
		GameObject* FirstObjectWithTag(const std::string& tag);

		Player* GetPlayer(i32 index);

		bool IsLoaded() const;

		static void ParseFoundMeshFiles();
		static void ParseFoundMaterialFiles();
		static void ParseFoundPrefabFiles();

		static std::vector<JSONObject> s_ParsedMaterials;
		static std::vector<JSONObject> s_ParsedMeshes;
		static std::vector<JSONObject> s_ParsedPrefabs;

		bool SerializeMeshFile() const;
		bool SerializeMaterialFile() const;
		bool SerializePrefabFile() const;

		std::vector<GameObject*> GetAllObjects();

		template<class T>
		std::vector<T*> GetObjectsOfType()
		{
			std::vector<GameObject*> objs = GetAllObjects();
			std::vector<T*> result;

			for (GameObject* obj : objs)
			{
				if (dynamic_cast<T*>(obj) != nullptr)
				{
					result.push_back((T*)obj);
				}
			}

			return result;
		}

		TrackManager* GetTrackManager();
		CartManager* GetCartManager();

		// Returns 'prefix' with a number appended representing
		// how many other objects with that prefix are in the scene
		std::string GetUniqueObjectName(const std::string& prefix, i16 digits);

		i32 GetSceneFileVersion() const;

		bool HasPlayers() const;

		const SkyboxData& GetSkyboxData() const;

	protected:
		friend GameObject;
		friend CartManager;
		friend SceneManager;

		// Recursively finds targetObject in currentObject's children
		// Returns true if targetObject was found and deleted
		bool DestroyGameObjectRecursive(GameObject* currentObject, GameObject* targetObject, bool bDestroyChildren);

		i32 GetMaterialArrayIndex(const Material& material);

		std::vector<MaterialID> RetrieveMaterialIDsFromJSON(const JSONObject& object, i32 fileVersion);

		void UpdateRootObjectSiblingIndices();

		static const i32 LATEST_SCENE_FILE_VERSION = 3;
		i32 m_SceneFileVersion = 1;

		static const i32 LATEST_MATERIALS_FILE_VERSION = 1;
		i32 m_MaterialsFileVersion = 1;

		static const i32 LATEST_MESHES_FILE_VERSION = 1;
		i32 m_MeshesFileVersion = 1;

		PhysicsWorld* m_PhysicsWorld = nullptr;

		std::string m_Name;
		std::string m_FileName;

		std::vector<GameObject*> m_RootObjects;

		bool m_bInitialized = false;
		bool m_bLoaded = false;
		bool m_bSpawnPlayer = false;

		/*
		* Stores all unique initialized materials we've created
		* A "material array index" is used to index into this array
		*/
		std::vector<MaterialID> m_LoadedMaterials;

		ReflectionProbe* m_ReflectionProbe = nullptr;

		SkyboxData m_SkyboxData;

		Player* m_Player0 = nullptr;
		Player* m_Player1 = nullptr;

		TrackManager m_TrackManager;
		CartManager m_CartManager;

		std::vector<ICallbackGameObject*> m_OnGameObjectDestroyedCallbacks;

		std::vector<GameObject*> m_PendingAddObjects;
		std::vector<Pair<GameObject*, GameObject*>> m_PendingAddChildObjects;
		std::vector<GameObject*> m_PendingDeleteObjects;
		std::vector<GameObject*> m_PendingDestroyObjects;

	private:
		/*
		* Recursively searches through all game objects and returns first
		* one containing given tag, or nullptr if none exist
		*/
		GameObject* FindObjectWithTag(const std::string& tag, GameObject* gameObject);

		BaseScene(const BaseScene&) = delete;
		BaseScene& operator=(const BaseScene&) = delete;

	};
} // namespace flex
