#pragma once

#include <string>
#include <vector>

#include "GameContext.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	class PhysicsWorld;
	class ReflectionProbe;
	struct JSONObject;

	class BaseScene
	{
	public:
		BaseScene(const std::string& name, const std::string& jsonFilePath);
		virtual ~BaseScene();

		virtual void Initialize(const GameContext& gameContext);
		virtual void PostInitialize(const GameContext& gameContext);
		virtual void Destroy(const GameContext& gameContext);
		virtual void Update(const GameContext& gameContext);

		std::string GetName() const;

		PhysicsWorld* GetPhysicsWorld();

		/* 
		* Serializes all data from scene into JSON scene file.
		* Only writes data that has non-default values (e.g. an identity 
		* transform is not saved)
		*/
		void SerializeToFile(const GameContext& gameContext);

		std::vector<GameObject*>& GetRootObjects();

	protected:
		void AddChild(GameObject* gameObject);
		void RemoveChild(GameObject* gameObject, bool deleteChild);
		void RemoveAllChildren(bool deleteChildren);

		PhysicsWorld* m_PhysicsWorld = nullptr;

		GameObject* CreateEntityFromJSON(const GameContext& gameContext, const JSONObject& obj);
		void CreatePointLightFromJSON(const GameContext& gameContext, const JSONObject& obj, PointLight& pointLight);
		void CreateDirectionalLightFromJSON(const GameContext& gameContext, const JSONObject& obj, DirectionalLight& directionalLight);

		JSONObject SerializeObject(GameObject* gameObject, const GameContext& gameContext);
		JSONObject SerializePointLight(PointLight& pointLight, const GameContext& gameContext);
		JSONObject SerializeDirectionalLight(DirectionalLight& directionalLight, const GameContext& gameContext);

		void ParseMaterialJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut);

		std::string m_Name;
		std::string m_JSONFilePath;

		std::vector<GameObject*> m_Children;
		std::vector<PointLight> m_PointLights;
		DirectionalLight m_DirectionalLight;

		ReflectionProbe* m_ReflectionProbe = nullptr;
		
		// TODO: Merge into one object type
		MeshPrefab* m_Grid = nullptr;
		MeshPrefab* m_WorldOrigin = nullptr;
		MaterialID m_GridMaterialID = InvalidMaterialID;
		MaterialID m_WorldAxisMaterialID = InvalidMaterialID;

	private:
		BaseScene(const BaseScene&) = delete;
		BaseScene& operator=(const BaseScene&) = delete;

	};
} // namespace flex
