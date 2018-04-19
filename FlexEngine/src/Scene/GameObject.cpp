#include "stdafx.hpp"

#include "Scene/GameObject.hpp"
#include "Helpers.hpp"

namespace flex
{
	GameObject::GameObject(GameObject* pParent) :
		m_Parent(pParent)
	{
		m_Transform.SetAsIdentity();

		if (pParent)
		{
			m_Transform.SetParentTransform(&pParent->m_Transform);
		}
	}

	GameObject::~GameObject()
	{
		m_Transform.RemoveAllChildTransforms();
		m_Transform.SetParentTransform(nullptr);

		for (size_t i = 0; i < m_Children.size(); ++i)
		{
			SafeDelete(m_Children[i]);
		}
		m_Children.clear();
	}

	void GameObject::SetParent(GameObject* parent)
	{
		m_Parent = parent;

		if (parent)
		{
			m_Transform.SetParentTransform(&parent->m_Transform);
		}

		m_Transform.Update();
	}

	void GameObject::AddChild(GameObject* child)
	{
		if (!child)
		{
			return;
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == child)
			{
				return; // Don't add the same child twice
			}
		}

		m_Children.push_back(child);
		m_Transform.AddChildTransform(&child->m_Transform);
		child->SetParent(this);
		child->m_Transform.SetParentTransform(&m_Transform);

		m_Transform.Update();
	}

	bool GameObject::RemoveChild(GameObject* child)
	{
		if (!child)
		{
			return false;
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == child)
			{
				(*iter)->m_Parent = nullptr;
				(*iter)->m_Transform.RemoveAllChildTransforms();
				m_Transform.RemoveChildTransform(&(*iter)->m_Transform);
				m_Children.erase(iter);
				return true;
			}
		}

		return false;
	}

	void GameObject::RemoveAllChildren()
	{
		m_Transform.RemoveAllChildTransforms();

		auto iter = m_Children.begin();
		while (iter != m_Children.end())
		{
			(*iter)->m_Parent = nullptr;
			(*iter)->m_Transform.RemoveAllChildTransforms();
			iter = m_Children.erase(iter);
		}
	}

	Transform& GameObject::GetTransform()
	{
		return m_Transform;
	}

	RenderID GameObject::GetRenderID() const
	{
		return m_RenderID;
	}

	void GameObject::Initialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void GameObject::PostInitialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void GameObject::Update(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void GameObject::Destroy(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void GameObject::SetRenderID(RenderID renderID)
	{
		m_RenderID = renderID;
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
