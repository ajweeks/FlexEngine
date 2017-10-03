#include "stdafx.h"

#include "Scene/GameObject.h"

namespace flex
{
	GameObject::GameObject(GameObject* pParent) :
		m_pParent(pParent)
	{
	}

	GameObject::~GameObject()
	{
		for (size_t i = 0; i < m_Children.size(); ++i)
		{
			delete m_Children[i];
		}
	}

	void GameObject::SetTransform(const Transform& transform)
	{
		m_Transform = transform;
	}

	Transform& GameObject::GetTransform()
	{
		return m_Transform;
	}

	RenderID GameObject::GetRenderID() const
	{
		return m_RenderID;
	}

	void GameObject::RootInitialize(const GameContext& gameContext)
	{
		Initialize(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->RootInitialize(gameContext);
		}
	}

	void GameObject::RootUpdate(const GameContext& gameContext)
	{
		Update(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->RootUpdate(gameContext);
		}
	}

	void GameObject::RootDestroy(const GameContext& gameContext)
	{
		Destroy(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->RootDestroy(gameContext);
		}
	}
} // namespace flex
