#pragma once

#include <string>
#include <vector>

#include "FreeCamera.hpp"
#include "GameContext.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	class BaseScene
	{
	public:
		BaseScene(std::string name);
		virtual ~BaseScene();

		std::string GetName() const;

	protected:
		virtual void Initialize(const GameContext& gameContext) = 0;
		virtual void PostInitialize(const GameContext& gameContext) = 0;
		virtual void Destroy(const GameContext& gameContext) = 0;
		virtual void Update(const GameContext& gameContext) = 0;

		void AddChild(const GameContext& gameContext, GameObject* gameObject);
		void RemoveChild(GameObject* gameObject, bool deleteChild);
		void RemoveAllChildren(bool deleteChildren);

	private:
		void RootInitialize(const GameContext& gameContext);
		void RootPostInitialize(const GameContext& gameContext);
		void RootUpdate(const GameContext& gameContext);
		void RootDestroy(const GameContext& gameContext);

		friend class SceneManager;

		std::string m_Name;

		std::vector<GameObject*> m_Children;

		BaseScene(const BaseScene&) = delete;
		BaseScene& operator=(const BaseScene&) = delete;
	};
} // namespace flex
