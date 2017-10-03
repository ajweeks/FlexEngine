#pragma once

#include <vector>

#include "GameContext.hpp"
#include "Transform.hpp"

namespace flex
{
	class GameObject
	{
	public:
		GameObject(GameObject* pParent = nullptr);
		virtual ~GameObject();

		void SetTransform(const Transform& transform);
		Transform& GetTransform();
		
		RenderID GetRenderID() const;

	protected:
		virtual void Initialize(const GameContext& gameContext) = 0;
		virtual void Update(const GameContext& gameContext) = 0;
		virtual void Destroy(const GameContext& gameContext) = 0;
		
		Transform m_Transform;
		RenderID m_RenderID;

	private:
		void RootInitialize(const GameContext& gameContext);
		void RootUpdate(const GameContext& gameContext);
		void RootDestroy(const GameContext& gameContext);

		friend class BaseScene;

		GameObject* m_pParent = nullptr;

		std::vector<GameObject*> m_Children;

	};
} // namespace flex
