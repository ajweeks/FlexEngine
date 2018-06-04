#pragma once

#include <string>
#include <vector>

#include "GameContext.hpp"

namespace flex
{
	class PhysicsWorld;
	class ReflectionProbe;
	class Player;
	class GameObject;
	struct JSONObject;
	struct JSONField;

	class BaseScene
	{
	public:
		BaseScene(const std::string& jsonFilePath);
		virtual ~BaseScene();

		virtual void Initialize(const GameContext& gameContext);
		virtual void PostInitialize(const GameContext& gameContext);
		virtual void Destroy(const GameContext& gameContext);
		virtual void Update(const GameContext& gameContext);

		std::string GetName() const;
		std::string GetJSONFilePath() const;

		PhysicsWorld* GetPhysicsWorld();

		/* 
		* Serializes all data from scene into JSON scene file.
		* Only writes data that has non-default values (e.g. an identity 
		* transform is not saved)
		*/
		void SerializeToFile(const GameContext& gameContext);

		std::vector<GameObject*>& GetRootObjects();
		void GetInteractibleObjects(std::vector<GameObject*>& interactibleObjects);

		GameObject* AddChild(GameObject* gameObject);
		void RemoveChild(GameObject* gameObject, bool deleteChild);
		void RemoveAllChildren(bool deleteChildren);

		/* Returns the first found game object with tag, or nullptr if none exist */
		GameObject* FirstObjectWithTag(const std::string& tag);

	protected:
		PhysicsWorld* m_PhysicsWorld = nullptr;

		GameObject* CreateEntityFromJSON(const GameContext& gameContext, const JSONObject& obj);
		void CreatePointLightFromJSON(const GameContext& gameContext, const JSONObject& obj, PointLight& pointLight);
		void CreateDirectionalLightFromJSON(const GameContext& gameContext, const JSONObject& obj, DirectionalLight& directionalLight);

		JSONObject SerializeObject(GameObject* gameObject, const GameContext& gameContext);
		JSONObject SerializeMaterial(const Material& material, const GameContext& gameContext);
		JSONObject SerializePointLight(PointLight& pointLight, const GameContext& gameContext);
		JSONObject SerializeDirectionalLight(DirectionalLight& directionalLight, const GameContext& gameContext);

		void ParseMaterialJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut);

		i32 GetMaterialArrayIndex(const Material& material, const GameContext& gameContext);

		std::string m_Name;
		std::string m_JSONFilePath;

		std::vector<GameObject*> m_Children;

		/*
		* Stores all unique initialized materials we've created
		* A "material array index" is used to index into this array
		*/
		std::vector<MaterialID> m_LoadedMaterials;

		ReflectionProbe* m_ReflectionProbe = nullptr;
		
		// TODO: Merge into one object type
		GameObject* m_Grid = nullptr;
		GameObject* m_WorldOrigin = nullptr;
		MaterialID m_GridMaterialID = InvalidMaterialID;
		MaterialID m_WorldAxisMaterialID = InvalidMaterialID;

		AudioSourceID dud_dud_dud_dud;
		AudioSourceID drmapan;
		AudioSourceID blip;
		//AudioSourceID TRG_Banks;

		Player* m_Player0 = nullptr;
		Player* m_Player1 = nullptr;

	private:
		/* Recursively searches through all game objects in search of one containing given tag */
		GameObject* FindObjectWithTag(const std::string& tag, GameObject* gameObject);

		BaseScene(const BaseScene&) = delete;
		BaseScene& operator=(const BaseScene&) = delete;

	};
} // namespace flex
