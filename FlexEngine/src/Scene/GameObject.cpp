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
			m_Serializable = false;
		}
	}

	GameObject::~GameObject()
	{
	}

	void GameObject::Initialize(const GameContext& gameContext)
	{
		const std::vector<Transform*>& children = m_Transform.GetChildren();
		for (auto iter = children.begin(); iter != children.end(); ++iter)
		{
			(*iter)->GetGameObject()->Initialize(gameContext);
		}
	}

	void GameObject::PostInitialize(const GameContext& gameContext)
	{
		if (m_RenderID != InvalidRenderID)
		{
			gameContext.renderer->PostInitializeRenderObject(gameContext, m_RenderID);
		}

		const std::vector<Transform*>& children = m_Transform.GetChildren();
		for (auto child : children)
		{
			child->GetGameObject()->PostInitialize(gameContext);
		}
	}

	void GameObject::Destroy(const GameContext& gameContext)
	{
		if (m_RenderID != InvalidRenderID)
		{
			gameContext.renderer->DestroyRenderObject(m_RenderID);
		}

		const std::vector<Transform*>& children = m_Transform.GetChildren();
		for (auto iter = children.begin(); iter != children.end(); ++iter)
		{
			(*iter)->GetGameObject()->Destroy(gameContext);
		}

		m_Transform.SetParentTransform(nullptr);
		m_Transform.RemoveAllChildTransforms();
	}

	void GameObject::Update(const GameContext& gameContext)
	{
		const std::vector<Transform*>& children = m_Transform.GetChildren();
		for (auto iter = children.begin(); iter != children.end(); ++iter)
		{
			(*iter)->GetGameObject()->Update(gameContext);
		}
	}

	void GameObject::SetParent(GameObject* parent)
	{
		m_Transform.SetParentTransform(parent->GetTransform());
		m_Transform.Update();
	}

	void GameObject::AddChild(GameObject* child)
	{
		if (!child)
		{
			return;
		}

		Transform* childTransform = child->GetTransform();

		const std::vector<Transform*>& children = m_Transform.GetChildren();
		for (auto iter = children.begin(); iter != children.end(); ++iter)
		{
			if (*iter == childTransform)
			{
				return; // Don't add the same child twice
			}
		}

		m_Transform.AddChildTransform(childTransform);
		child->SetParent(this);

		m_Transform.Update();
	}

	bool GameObject::RemoveChild(GameObject* child)
	{
		if (!child)
		{
			return false;
		}

		Transform* childTransform = child->GetTransform();

		const std::vector<Transform*>& children = m_Transform.GetChildren();
		for (auto iter = children.begin(); iter != children.end(); ++iter)
		{
			if (*iter == childTransform)
			{
				childTransform->SetParentTransform(nullptr);
				m_Transform.RemoveChildTransform(childTransform);
				return true;
			}
		}

		return false;
	}

	void GameObject::RemoveAllChildren()
	{
		m_Transform.RemoveAllChildTransforms();
	}

	Transform* GameObject::GetTransform()
	{
		return &m_Transform;
	}

	RenderID GameObject::GetRenderID() const
	{
		return m_RenderID;
	}

	bool GameObject::IsSerializable() const
	{
		return m_Serializable;
	}

	std::string GameObject::GetName() const
	{
		return m_Name;
	}

	void GameObject::SetRenderID(RenderID renderID)
	{
		m_RenderID = renderID;
	}
} // namespace flex
