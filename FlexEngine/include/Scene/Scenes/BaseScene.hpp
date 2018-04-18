#pragma once

#include <string>
#include <vector>

#include "GameContext.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	class PhysicsWorld;
	class ReflectionProbe;

	class BaseScene
	{
	public:
		BaseScene(const std::string& name = "");
		virtual ~BaseScene();

		void InitializeFromJSON(const std::string& jsonFilePath, const GameContext& gameContext);

		std::string GetName() const;

		PhysicsWorld* GetPhysicsWorld();

	protected:
		virtual void Initialize(const GameContext& gameContext);
		virtual void PostInitialize(const GameContext& gameContext);
		virtual void Destroy(const GameContext& gameContext);
		virtual void Update(const GameContext& gameContext);

		void AddChild(const GameContext& gameContext, GameObject* gameObject);
		void RemoveChild(GameObject* gameObject, bool deleteChild);
		void RemoveAllChildren(bool deleteChildren);

		PhysicsWorld* m_PhysicsWorld = nullptr;

	private:
		void RootInitialize(const GameContext& gameContext);
		void RootPostInitialize(const GameContext& gameContext);
		void RootUpdate(const GameContext& gameContext);
		void RootDestroy(const GameContext& gameContext);

		friend class SceneManager;

		std::string m_Name;

		std::vector<GameObject*> m_Children;

		ReflectionProbe* m_ReflectionProbe = nullptr;

		BaseScene(const BaseScene&) = delete;
		BaseScene& operator=(const BaseScene&) = delete;
	};
} // namespace flex
