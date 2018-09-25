#pragma once

#include <string>
#include <vector>

namespace flex
{
	class PhysicsWorld;
	class ReflectionProbe;
	class Player;
	class GameObject;
	struct JSONObject;
	struct JSONField;
	struct PointLight;
	struct DirectionalLight;
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
		void SerializeToFile(bool bSaveOverDefault = false);

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

	protected:
		friend class GameObject;

		// Recursively finds targetObject in currentObject's children
		// Returns true if targetObject was found and deleted
		bool DestroyGameObjectRecursive(GameObject* currentObject, GameObject* targetObject, bool bDeleteChildren);

		void CreatePointLightFromJSON(const JSONObject& obj, PointLight& pointLight);
		void CreateDirectionalLightFromJSON(const JSONObject& obj, DirectionalLight& directionalLight);

		JSONObject SerializePointLight(PointLight& pointLight);
		JSONObject SerializeDirectionalLight(DirectionalLight& directionalLight);

		i32 GetMaterialArrayIndex(const Material& material);

		MaterialID FindMaterialIDByName(const JSONObject& object);

		void UpdateRootObjectSiblingIndices();

		static const i32 m_FileVersion = 1;

		PhysicsWorld* m_PhysicsWorld = nullptr;

		std::string m_Name;
		std::string m_FileName;


		std::vector<GameObject*> m_RootObjects;

		bool m_bUsingSaveFile = false;

		bool m_bLoaded = false;

		/*
		* Stores all unique initialized materials we've created
		* A "material array index" is used to index into this array
		*/
		std::vector<MaterialID> m_LoadedMaterials;

		ReflectionProbe* m_ReflectionProbe = nullptr;

		Player* m_Player0 = nullptr;
		Player* m_Player1 = nullptr;

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
