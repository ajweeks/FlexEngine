#include "stdafx.hpp"

#pragma warning(push, 0)
#include <LinearMath/btTransform.h>
#include <LinearMath/btIDebugDraw.h>
#include <BulletCollision/BroadphaseCollision/btBroadphaseProxy.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletDynamics/ConstraintSolver/btFixedConstraint.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#pragma warning(pop)

#include "Audio/AudioManager.hpp"
#include "GameContext.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	RandomizedAudioSource GameObject::s_SqueakySounds;
	AudioSourceID GameObject::s_BunkSound;

	GameObject::GameObject(const std::string& name, GameObjectType type) :
		m_Name(name),
		m_Type(type)
	{
		m_Transform.SetAsIdentity();
		m_Transform.SetGameObject(this);

		if (!s_SqueakySounds.IsInitialized())
		{
			s_SqueakySounds.Initialize(RESOURCE_LOCATION + "audio/squeak00.wav", 5);

			s_BunkSound = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/bunk.wav");
		}
	}

	GameObject::~GameObject()
	{
	}

	void GameObject::Initialize(const GameContext& gameContext)
	{
		switch (m_Type)
		{
		case GameObjectType::VALVE:
		{
		} break;
		case GameObjectType::RISING_BLOCK:
		{
			m_RisingBlockMembers.startingPos = m_Transform.GetWorldPosition();
		} break;
		}

		if (m_RigidBody)
		{
			if (!m_CollisionShape)
			{
				Logger::LogError("Game object contains rigid body but no collision shape! Must call SetCollisionShape before Initialize");
			}
			else
			{
				m_RigidBody->Initialize(m_CollisionShape, gameContext, &m_Transform);
			}
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Initialize(gameContext);
		}
	}

	void GameObject::PostInitialize(const GameContext& gameContext)
	{
		RigidBody* rb = GetRigidBody();

		switch (m_Type)
		{
		case GameObjectType::REFLECTION_PROBE:
		{
			gameContext.renderer->SetReflectionProbeMaterial(m_ReflectionProbeMembers.captureMatID);
		} break;
		case GameObjectType::VALVE:
		{
			rb->SetPhysicsFlags((u32)PhysicsFlag::TRIGGER);
			auto rbInternal = rb->GetRigidBodyInternal();
			rbInternal->setAngularFactor(btVector3(0, 1, 0));
			// Mark as trigger
			rbInternal->setCollisionFlags(
				rbInternal->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
			rbInternal->setGravity(btVector3(0, 0, 0));
		} break;
		case GameObjectType::RISING_BLOCK:
		{
			auto rbInternal = rb->GetRigidBodyInternal();
			rbInternal->setGravity(btVector3(0, 0, 0));

			//btTransform transform = m_RigidBody->GetRigidBodyInternal()->getWorldTransform();
			//btFixedConstraint* constraint = new btFixedConstraint(
			//	*m_RigidBody->GetRigidBodyInternal(),
			//	*m_RigidBody->GetRigidBodyInternal(),
			//	transform,
			//	transform);
			////constraint->setAngularLowerLimit(btVector3(0, 0, 0));
			////constraint->setAngularUpperLimit(btVector3(0, 0, 0));
			//m_RigidBody->AddConstraint(constraint);
		} break;
		}

		if (m_RenderID != InvalidRenderID)
		{
			gameContext.renderer->PostInitializeRenderObject(gameContext, m_RenderID);
		}

		if (rb)
		{
			rb->GetRigidBodyInternal()->setUserPointer(this);
		}

		for (auto child : m_Children)
		{
			child->PostInitialize(gameContext);
		}
	}

	void GameObject::Destroy(const GameContext& gameContext)
	{
		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Destroy(gameContext);
			SafeDelete(*iter);
		}
		m_Children.clear();

		if (m_MeshComponent)
		{
			m_MeshComponent->Destroy();
			SafeDelete(m_MeshComponent);
		}

		if (m_RenderID != InvalidRenderID)
		{
			gameContext.renderer->DestroyRenderObject(m_RenderID);
			m_RenderID = InvalidRenderID;
		}

		if (m_RigidBody)
		{
			m_RigidBody->Destroy(gameContext);
			SafeDelete(m_RigidBody);
		}

		// NOTE: SafeDelete does not work on this type
		if (m_CollisionShape)
		{
			delete m_CollisionShape;
			m_CollisionShape = nullptr;
		}
	}

	void GameObject::Update(const GameContext& gameContext)
	{
		if (m_ObjectInteractingWith)
		{
			// TODO: Write real fancy-lookin outline shader instead of drawing a lil cross
			btIDebugDraw* debugDrawer = gameContext.renderer->GetDebugDrawer();
			auto pos = Vec3ToBtVec3(m_Transform.GetWorldPosition());
			debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.1f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.1f, 0.1f));
		}
		else if (m_bInteractable)
		{
			btIDebugDraw* debugDrawer = gameContext.renderer->GetDebugDrawer();
			auto pos = Vec3ToBtVec3(m_Transform.GetWorldPosition());
			debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.95f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.95f, 0.1f));
		}

		switch (m_Type)
		{
		case GameObjectType::VALVE:
		{
			// True when our rotation is changed by another object (rising block)
			bool bRotatedByOtherObject = false;
			real currentAbsAvgRotationSpeed = 0.0f;
			if (m_ObjectInteractingWith)
			{
				i32 playerIndex = ((Player*)m_ObjectInteractingWith)->GetIndex();

				const InputManager::GamepadState& gamepadState = gameContext.inputManager->GetGamepadState(playerIndex);
				m_ValveMembers.rotationSpeed = (-gamepadState.averageRotationSpeeds.currentAverage) * m_ValveMembers.rotationSpeedScale;
				currentAbsAvgRotationSpeed = glm::abs(gamepadState.averageRotationSpeeds.currentAverage);
			}
			else
			{
				m_ValveMembers.rotationSpeed = (m_ValveMembers.rotation - m_ValveMembers.pRotation) / gameContext.deltaTime;
				// Not entirely true but needed to trigger sound
				currentAbsAvgRotationSpeed = glm::abs(m_ValveMembers.rotationSpeed);
				bRotatedByOtherObject = (glm::abs(m_ValveMembers.rotation - m_ValveMembers.pRotation) > 0);
			}

			if ((m_ValveMembers.rotationSpeed < 0.0f &&
				m_ValveMembers.rotation <= m_ValveMembers.minRotation) ||
				(m_ValveMembers.rotationSpeed > 0.0f &&
				m_ValveMembers.rotation >= m_ValveMembers.maxRotation))
			{
				m_ValveMembers.rotationSpeed = 0.0f;
				m_ValveMembers.pRotationSpeed = 0.0f;
			}
			else
			{
				if (m_ValveMembers.rotationSpeed == 0.0f)
				{
					m_ValveMembers.pRotationSpeed *= m_ValveMembers.invSlowDownRate;
				}
				else
				{
					m_ValveMembers.pRotationSpeed = m_ValveMembers.rotationSpeed;
				}
			}

			if (!bRotatedByOtherObject)
			{
				real rotationSpeed = gameContext.deltaTime * m_ValveMembers.pRotationSpeed;
				m_ValveMembers.rotation += rotationSpeed;
			}
			real overshoot = 0.0f;
			if (m_ValveMembers.rotation > m_ValveMembers.maxRotation)
			{
				overshoot = m_ValveMembers.rotation - m_ValveMembers.maxRotation;
				m_ValveMembers.rotation = m_ValveMembers.maxRotation;
			}
			else if (m_ValveMembers.rotation < m_ValveMembers.minRotation)
			{
				overshoot = m_ValveMembers.minRotation - m_ValveMembers.rotation;
				m_ValveMembers.rotation = m_ValveMembers.minRotation;
			}

			m_ValveMembers.pRotation = m_ValveMembers.rotation;

			if (overshoot != 0.0f &&
				currentAbsAvgRotationSpeed > 0.01f)
			{
				real gain = glm::abs(overshoot) * 8.0f;
				gain = glm::clamp(gain, 0.0f, 1.0f);
				AudioManager::SetSourceGain(s_BunkSound, gain);
				AudioManager::PlaySource(s_BunkSound, true);
				//Logger::LogInfo(std::to_string(overshoot) + ", " + std::to_string(gain));
				m_ValveMembers.rotationSpeed = 0.0f;
				m_ValveMembers.pRotationSpeed = 0.0f;
			}

			m_RigidBody->GetRigidBodyInternal()->activate(true);
			m_Transform.SetLocalRotation(glm::quat(glm::vec3(0, m_ValveMembers.rotation, 0)));
			m_RigidBody->UpdateParentTransform();

			if (glm::abs(m_ValveMembers.rotationSpeed) > 0.2f)
			{
				bool updateGain = !s_SqueakySounds.IsPlaying();
				
				s_SqueakySounds.Play(false);

				if (updateGain)
				{
					s_SqueakySounds.SetGain(glm::abs(m_ValveMembers.rotationSpeed) * 2.0f - 0.2f);
				}
			}
		} break;
		case GameObjectType::RISING_BLOCK:
		{
			real minDist = m_RisingBlockMembers.valve->m_ValveMembers.minRotation;
			real maxDist = m_RisingBlockMembers.valve->m_ValveMembers.maxRotation;
			//real totalDist = (maxDist - minDist);
			real dist = m_RisingBlockMembers.valve->m_ValveMembers.rotation;

			real playerControlledValveRotationSpeed = 0.0f;
			if (m_RisingBlockMembers.valve->bBeingInteractedWith)
			{
				i32 playerIndex = ((Player*)m_RisingBlockMembers.valve->m_ObjectInteractingWith)->GetIndex();
				const InputManager::GamepadState& gamepadState = gameContext.inputManager->GetGamepadState(playerIndex);
				playerControlledValveRotationSpeed = (-gamepadState.averageRotationSpeeds.currentAverage) *
					m_RisingBlockMembers.valve->m_ValveMembers.rotationSpeedScale;
			}

			if (m_RisingBlockMembers.bAffectedByGravity &&
				m_RisingBlockMembers.valve->m_ValveMembers.rotation >= 
					m_RisingBlockMembers.valve->m_ValveMembers.minRotation + 0.1f)
			{
				// Apply gravity by rotating valve
				real fallSpeed = 6.0f;
				real distMult = 1.0f - glm::clamp(playerControlledValveRotationSpeed / 2.0f, 0.0f, 1.0f);
				real dDist = (fallSpeed * gameContext.deltaTime * distMult);
				dist -= Lerp(m_RisingBlockMembers.pdDistBlockMoved, dDist, 0.1f);
				m_RisingBlockMembers.pdDistBlockMoved = dDist;

				// NOTE: Don't clamp out of bounds rotation here, valve object 
				// will handle it and play correct "overshoot" sound
				//dist = glm::clamp(dist, minDist, maxDist);

				m_RisingBlockMembers.valve->m_ValveMembers.rotation = dist;
			}

			glm::vec3 newPos = m_RisingBlockMembers.startingPos +
				dist * m_RisingBlockMembers.moveAxis;
			
			if (m_RigidBody)
			{
				m_RigidBody->GetRigidBodyInternal()->activate(true);
				btTransform transform;
				m_RigidBody->GetRigidBodyInternal()->getMotionState()->getWorldTransform(transform);
				transform.setOrigin(Vec3ToBtVec3(newPos));
				transform.setRotation(btQuaternion::getIdentity());
				m_RigidBody->GetRigidBodyInternal()->setWorldTransform(transform);
			}

			btVector3 startPos = Vec3ToBtVec3(m_RisingBlockMembers.startingPos);
			gameContext.renderer->GetDebugDrawer()->drawLine(
				startPos,
				startPos + Vec3ToBtVec3(m_RisingBlockMembers.moveAxis * maxDist),
				btVector3(1, 1, 1));
			if (minDist < 0.0f)
			{
				gameContext.renderer->GetDebugDrawer()->drawLine(
					startPos,
					startPos + Vec3ToBtVec3(m_RisingBlockMembers.moveAxis * minDist),
					btVector3(0.99f, 0.6f, 0.6f));
			}

			gameContext.renderer->GetDebugDrawer()->drawLine(
				startPos,
				startPos + Vec3ToBtVec3(m_RisingBlockMembers.moveAxis * dist),
				btVector3(0.3f, 0.3f, 0.5f));
		} break;
		}

		if (m_RigidBody)
		{
			if (m_RigidBody->IsKinematic())
			{
				m_RigidBody->MatchParentTransform();
			}
			else
			{
				// Rigid body will take these changes into account
				m_RigidBody->UpdateParentTransform();
			}
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Update(gameContext);
		}
	}

	bool GameObject::SetInteractingWith(GameObject* gameObject)
	{
		m_ObjectInteractingWith = gameObject;

		bBeingInteractedWith = (gameObject != nullptr);

		return true;
	}

	GameObject* GameObject::GetObjectInteractingWith()
	{
		return m_ObjectInteractingWith;
	}

	GameObject* GameObject::GetParent()
	{
		return m_Parent;
	}

	void GameObject::DetachFromParent()
	{
		if (m_Parent)
		{
			m_Parent->RemoveChild(this);
		}
	}

	void GameObject::SetParent(GameObject* parent)
	{
		if (parent == this)
		{
			Logger::LogError("Attempted to set parent as self! (" + m_Name + ")");
			return;
		}

		m_Parent = parent;

		m_Transform.UpdateParentTransform();
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

		Transform* childTransform = child->GetTransform();
		glm::mat4 childWorldTransform = childTransform->GetWorldTransform();

		if (child == m_Parent)
		{
			DetachFromParent();
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

		childTransform->SetWorldTransform(childWorldTransform);

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
				glm::mat4 childWorldTransform = child->GetTransform()->GetWorldTransform();

				child->SetParent(nullptr);

				child->GetTransform()->SetWorldTransform(childWorldTransform);

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

	bool GameObject::HasChild(GameObject* child, bool bRecurse)
	{
		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == child)
			{
				return true;
			}

			if (bRecurse && 
				*iter && 
				(*iter)->HasChild(child, bRecurse))
			{
				return true;
			}
		}
		return false;
	}

	Transform* GameObject::GetTransform()
	{
		return &m_Transform;
	}

	void GameObject::AddTag(const std::string& tag)
	{
		if (std::find(m_Tags.begin(), m_Tags.end(), tag) == m_Tags.end())
		{
			m_Tags.push_back(tag);
		}
	}

	bool GameObject::HasTag(const std::string& tag)
	{
		auto result = std::find(m_Tags.begin(), m_Tags.end(), tag);
		return (result != m_Tags.end());
	}

	RenderID GameObject::GetRenderID() const
	{
		return m_RenderID;
	}

	std::string GameObject::GetName() const
	{
		return m_Name;
	}

	void GameObject::SetName(const std::string& newName)
	{
		m_Name = newName;
	}

	void GameObject::SetRenderID(RenderID renderID)
	{
		m_RenderID = renderID;
	}

	bool GameObject::IsSerializable() const
	{
		return m_bSerializable;
	}

	void GameObject::SetSerializable(bool bSerializable)
	{
		m_bSerializable = bSerializable;
	}

	bool GameObject::IsStatic() const
	{
		return m_bStatic;
	}

	void GameObject::SetStatic(bool bStatic)
	{
		m_bStatic = bStatic;
	}

	bool GameObject::IsVisible() const
	{
		return m_bVisible;
	}

	void GameObject::SetVisible(bool bVisible, bool effectChildren)
	{
		if (m_bVisible != bVisible)
		{
			m_bVisible = bVisible;

			if (effectChildren)
			{
				for (GameObject* child : m_Children)
				{
					child->SetVisible(bVisible, effectChildren);
				}
			}
		}
	}

	bool GameObject::IsVisibleInSceneExplorer() const
	{
		return m_bVisibleInSceneExplorer;
	}

	void GameObject::SetVisibleInSceneExplorer(bool bVisibleInSceneExplorer)
	{
		if (m_bVisibleInSceneExplorer != bVisibleInSceneExplorer)
		{
			m_bVisibleInSceneExplorer = bVisibleInSceneExplorer;
		}
	}

	btCollisionShape* GameObject::SetCollisionShape(btCollisionShape* collisionShape)
	{
		if (m_CollisionShape)
		{
			delete m_CollisionShape;
			m_CollisionShape = nullptr;
		}

		m_CollisionShape = collisionShape;
		return collisionShape;
	}

	btCollisionShape* GameObject::GetCollisionShape() const
	{
		return m_CollisionShape;
	}

	RigidBody* GameObject::SetRigidBody(RigidBody* rigidBody)
	{
		SafeDelete(m_RigidBody);

		m_RigidBody = rigidBody;
		return rigidBody;
	}

	RigidBody* GameObject::GetRigidBody() const
	{
		return m_RigidBody;
	}

	MeshComponent* GameObject::GetMeshComponent()
	{
		return m_MeshComponent;
	}

	MeshComponent* GameObject::SetMeshComponent(MeshComponent* meshComponent)
	{
		m_MeshComponent = meshComponent;
		return meshComponent;
	}

	void GameObject::OnOverlapBegin(GameObject* other)
	{
		overlappingObjects.push_back(other);

		if (m_Type != GameObjectType::PLAYER)
		{
			if (other->HasTag("Player0") ||
				other->HasTag("Player1"))
			{
				m_bInteractable = true;
			}
		}
	}

	void GameObject::OnOverlapEnd(GameObject* other)
	{
		for (auto iter = overlappingObjects.begin(); iter != overlappingObjects.end(); /* */)
		{
			if (*iter == other)
			{
				iter = overlappingObjects.erase(iter);
			}
			else
			{
				++iter;
			}
		}

		switch (m_Type)
		{
		case GameObjectType::PLAYER:
		{

		} break;
		default: // All non-player objects
		{
			if (other->HasTag("Player0") ||
				other->HasTag("Player1"))
			{
				m_bInteractable = false;
			}
		} break;
		}
	}
} // namespace flex
