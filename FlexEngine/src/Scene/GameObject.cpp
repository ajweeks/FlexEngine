#include "stdafx.hpp"

#pragma warning(push, 0)
#include <LinearMath/btTransform.h>
#include <BulletCollision/BroadphaseCollision/btBroadphaseProxy.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#pragma warning(pop)

#include "Scene/GameObject.hpp"
#include "Helpers.hpp"
#include "Physics/RigidBody.hpp"

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
		if (m_RigidBody && m_CollisionShape)
		{
			btTransform startingTransform = btTransform::getIdentity();
			m_RigidBody->Initialize(m_CollisionShape, gameContext, startingTransform);
		}


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

		// NOTE: SafeDelete does not work on this type
		if (m_CollisionShape)
		{
			delete m_CollisionShape;
			m_CollisionShape = nullptr;
		}
		SafeDelete(m_RigidBody);
	}

	void GameObject::Update(const GameContext& gameContext)
	{
		if (m_RigidBody)
		{
			m_Transform.MatchRigidBody(m_RigidBody, true);
		}

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

	btCollisionShape* GameObject::SetCollisionShape(btCollisionShape* collisionShape)
	{
		m_CollisionShape = collisionShape;
		return collisionShape;
	}

	btCollisionShape* GameObject::GetCollisionShape() const
	{
		return m_CollisionShape;
	}

	RigidBody* GameObject::SetRigidBody(RigidBody* rigidBody)
	{
		m_RigidBody = rigidBody;
		return rigidBody;
	}

	RigidBody* GameObject::GetRigidBody() const
	{
		return m_RigidBody;
	}
} // namespace flex
