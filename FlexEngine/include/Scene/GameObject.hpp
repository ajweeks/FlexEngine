#pragma once

#include <vector>

#include "GameContext.hpp"
#include "Transform.hpp"

namespace flex
{
	class GameObject
	{
	public:
		GameObject(GameObject* parent = nullptr);
		virtual ~GameObject();

		void SetParent(GameObject* parent);
		void AddChild(GameObject* child);
		bool RemoveChild(GameObject* child);
		void RemoveAllChildren();

		virtual Transform& GetTransform();
		
		RenderID GetRenderID() const;
		void SetRenderID(RenderID renderID);

		virtual void Initialize(const GameContext& gameContext);
		virtual void PostInitialize(const GameContext& gameContext);

	protected:
		virtual void Update(const GameContext& gameContext);
		virtual void Destroy(const GameContext& gameContext);
		
		Transform m_Transform;
		// TODO: Make public?
		RenderID m_RenderID = InvalidRenderID;

	private:
		void RootInitialize(const GameContext& gameContext);
		void RootUpdate(const GameContext& gameContext);
		void RootDestroy(const GameContext& gameContext);

		friend class BaseScene;

		GameObject* m_Parent = nullptr;

		std::vector<GameObject*> m_Children;

	};
} // namespace flex
