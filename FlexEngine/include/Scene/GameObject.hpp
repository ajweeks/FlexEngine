#pragma once

#include <vector>

#include "GameContext.hpp"
#include "Transform.hpp"

namespace flex
{
	class GameObject
	{
	public:
		GameObject(const std::string& name, SerializableType serializableType);
		virtual ~GameObject();

		virtual void Initialize(const GameContext& gameContext);
		virtual void PostInitialize(const GameContext& gameContext);
		virtual void Destroy(const GameContext& gameContext);
		virtual void Update(const GameContext& gameContext);

		void SetParent(GameObject* parent);
		void AddChild(GameObject* child);
		bool RemoveChild(GameObject* child);
		void RemoveAllChildren();

		virtual Transform* GetTransform();
		
		RenderID GetRenderID() const;
		void SetRenderID(RenderID renderID);

		bool IsSerializable() const;

		std::string GetName() const;

	protected:
		Transform m_Transform;
		// TODO: Make public?
		RenderID m_RenderID = InvalidRenderID;

		std::string m_Name;
		SerializableType m_SerializableType = SerializableType::NONE;
		bool m_Serializable = true;

		friend class BaseClass;
		friend class BaseScene;

		GameObject* m_Parent = nullptr;
		std::vector<GameObject*> m_Children;

	};
} // namespace flex
