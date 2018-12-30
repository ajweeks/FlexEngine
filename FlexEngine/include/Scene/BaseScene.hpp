#pragma once

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

	class BaseScene
	{
	public:
		// fileName e.g. "scene_01.json"
		BaseScene(const std::string& fileName);
		virtual ~BaseScene();

		virtual void Initialize();
		virtual void PostInitialize();
		virtual void Destroy();
		virtual void Update();

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
		void GetInteractibleObjects(std::vector<GameObject*>& interactibleObjects);

		GameObject* AddRootObject(GameObject* gameObject);
		void RemoveRootObject(GameObject* gameObject, bool deleteRootObject);
		void RemoveAllRootObjects(bool deleteRootObjects);

		std::vector<MaterialID> GetMaterialIDs();
		void AddMaterialID(MaterialID newMaterialID);
		void RemoveMaterialID(MaterialID materialID);

		/* Returns the first found game object with tag, or nullptr if none exist */
		GameObject* FirstObjectWithTag(const std::string& tag);

		Player* GetPlayer(i32 index);

		// Deletes and removes targetObject if exists in scene
		// Returns true if targetObject was found
		bool DestroyGameObject(GameObject* targetObject, bool bDeleteChildren);

		bool IsLoaded() const;

		static void ParseFoundMeshFiles();
		static void ParseFoundMaterialFiles();
		static void ParseFoundPrefabFiles();

		static bool SerializeMeshFile();
		static bool SerializeMaterialFile();
		static bool SerializePrefabFile();

		static std::vector<JSONObject> s_ParsedMaterials;
		static std::vector<JSONObject> s_ParsedMeshes;
		static std::vector<JSONObject> s_ParsedPrefabs;

		std::vector<GameObject*> GetAllObjects();

		TrackManager* GetTrackManager();

		// Returns 'prefix' with a number appended representing
		// how many other objects with that prefix are in the scene
		std::string GetUniqueObjectName(const std::string& prefix, i16 digits);

		void RemoveObjectAtEndOfFrame(GameObject* obj);
		void RemoveObjectsAtEndOfFrame(const std::vector<GameObject*>& objs);
		void AddObjectAtEndOFFrame(GameObject* obj);
		void AddObjectsAtEndOFFrame(const std::vector<GameObject*>& objs);

	protected:
		friend class GameObject;

		// Recursively finds targetObject in currentObject's children
		// Returns true if targetObject was found and deleted
		bool DestroyGameObjectRecursive(GameObject* currentObject, GameObject* targetObject, bool bDeleteChildren);

		i32 GetMaterialArrayIndex(const Material& material);

		MaterialID FindMaterialIDByName(const JSONObject& object);

		void UpdateRootObjectSiblingIndices();

		void CreateDefaultDirectionalLight();

		static const i32 m_FileVersion = 1;

		PhysicsWorld* m_PhysicsWorld = nullptr;

		std::string m_Name;
		std::string m_FileName;


		std::vector<GameObject*> m_RootObjects;

		//bool m_bUsingSaveFile = false;

		bool m_bLoaded = false;

		/*
		* Stores all unique initialized materials we've created
		* A "material array index" is used to index into this array
		*/
		std::vector<MaterialID> m_LoadedMaterials;

		ReflectionProbe* m_ReflectionProbe = nullptr;

		Player* m_Player0 = nullptr;
		Player* m_Player1 = nullptr;

		TrackManager m_TrackManager;

		std::vector<GameObject*> m_ObjectsToAddAtEndOfFrame;
		std::vector<GameObject*> m_ObjectsToRemoveAtEndOfFrame;

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
