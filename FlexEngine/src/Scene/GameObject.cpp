#include "stdafx.hpp"

#include "Scene/GameObject.hpp"
#include "Helpers.hpp"

namespace flex
{
	GameObject::GameObject(const std::string& name, SerializableType serializableType) :
		m_Name(name),
		m_SerializableType(serializableType)
	{
		m_Transform.SetAsIdentity();
		m_Transform.SetGameObject(this);

		if (m_SerializableType == SerializableType::NONE)
		{
			m_bSerializable = false;
		}
	}

	GameObject::~GameObject()
	{
	}

	void GameObject::Initialize(const GameContext& gameContext)
	{
		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Initialize(gameContext);
		}
	}

	void GameObject::PostInitialize(const GameContext& gameContext)
	{
		if (m_RenderID != InvalidRenderID)
		{
			gameContext.renderer->PostInitializeRenderObject(gameContext, m_RenderID);
		}

		for (auto child : m_Children)
		{
			child->PostInitialize(gameContext);
		}
	}

	void GameObject::Destroy(const GameContext& gameContext)
	{
		if (m_RenderID != InvalidRenderID)
		{
			gameContext.renderer->DestroyRenderObject(m_RenderID);
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Destroy(gameContext);
			SafeDelete(*iter);
		}
		m_Children.clear();
	}

	void GameObject::Update(const GameContext& gameContext)
	{
		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Update(gameContext);
		}
	}

	GameObject* GameObject::GetParent()
	{
		return m_Parent;
	}

	void GameObject::SetParent(GameObject* parent)
	{
		if (parent == this)
		{
			Logger::LogError("Attempted to set parent as self! (" + m_Name + ")");
			return;
		}

		m_Parent = parent;

		m_Transform.Update();
	}

	GameObject* GameObject::AddChild(GameObject* child)
	{
		if (!child)
		{
			return nullptr;
		}

		if (child == this)
		{
			Logger::LogError("Attempted to add self as child! (" + m_Name + ")");
			return nullptr;
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == child)
			{
				// Don't add the same child twice
				return nullptr;
			}
		}

		m_Children.push_back(child);
		child->SetParent(this);

		m_Transform.Update();

		return child;
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
				child->SetParent(nullptr);
				m_Children.erase(iter);
				return true;
			}
		}

		return false;
	}

	void GameObject::RemoveAllChildren()
	{
		auto iter = m_Children.begin();
		while (iter != m_Children.end())
		{
			iter = m_Children.erase(iter);
		}
		m_Children.clear();
	}

	const std::vector<GameObject*>& GameObject::GetChildren() const
	{
		return m_Children;
	}

	Transform* GameObject::GetTransform()
	{
		return &m_Transform;
	}

	RenderID GameObject::GetRenderID() const
	{
		return m_RenderID;
	}

	std::string GameObject::GetName() const
	{
		return m_Name;
	}

	void GameObject::SetRenderID(RenderID renderID)
	{
		m_RenderID = renderID;
	}

	bool GameObject::IsSerializable() const
	{
		return m_bSerializable;
	}

	void GameObject::SetSerializable(bool serializable)
	{
		m_bSerializable = serializable;
	}

	bool GameObject::IsStatic() const
	{
		return m_bStatic;
	}

	void GameObject::SetStatic(bool newStatic)
	{
		m_bStatic = newStatic;
	}

	bool GameObject::IsVisible() const
	{
		return m_bVisible;
	}

	void GameObject::SetVisible(bool visible)
	{
		m_bVisible = visible;
	}

	bool GameObject::IsVisibleInSceneExplorer() const
	{
		return m_bVisibleInSceneExplorer;
	}

	void GameObject::SetVisibleInSceneExplorer(bool visibleInSceneExplorer)
	{
		m_bVisibleInSceneExplorer = visibleInSceneExplorer;
	}
} // namespace flex
