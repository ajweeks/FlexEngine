#include "stdafx.hpp"

#pragma warning(push, 0)
#include <LinearMath/btTransform.h>
#include <LinearMath/btIDebugDraw.h>
#include <BulletCollision/BroadphaseCollision/btBroadphaseProxy.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#pragma warning(pop)

#include "Physics/RigidBody.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "GameContext.hpp"
#include "Player.hpp"

namespace flex
{
	GameObject::GameObject(const std::string& name, GameObjectType type) :
		m_Name(name),
		m_Type(type)
	{
		m_Transform.SetAsIdentity();
		m_Transform.SetGameObject(this);
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
			m_ValveMembers.averageRotationSpeeds = RollingAverage(8);

			for (i32 i = 0; i < 4; ++i)
			{
				m_ValveMembers.wasInQuadrantSinceIdle[i] = false;
			}
		} break;
		}

		if (m_RigidBody && m_CollisionShape)
		{
			btTransform startingTransform = TransformToBtTransform(m_Transform);
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

		if (m_MeshComponent)
		{
			m_MeshComponent->Destroy(gameContext);
			SafeDelete(m_MeshComponent);
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
		if (m_ObjectInteractingWith)
		{
			// TODO: Write real fancy-lookin outline shader instead of drawing a lil cross
			btIDebugDraw* debugDrawer = gameContext.renderer->GetDebugDrawer();
			auto pos = Vec3ToBtVec3(m_Transform.GetGlobalPosition());
			debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.1f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.1f, 0.1f));
		}
		else if (m_bInteractable)
		{
			// TODO: Write real fancy-lookin outline shader instead of drawing a lil cross
			btIDebugDraw* debugDrawer = gameContext.renderer->GetDebugDrawer();
			auto pos = Vec3ToBtVec3(m_Transform.GetGlobalPosition());
			debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.95f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.95f, 0.1f));
		}

		switch (m_Type)
		{
		case GameObjectType::OBJECT:
		{
		} break;
		case GameObjectType::PLAYER:
		{
			// NOTE: Handled in Player class
		} break;
		case GameObjectType::SKYBOX:
		{
		} break;
		case GameObjectType::REFLECTION_PROBE:
		{
		} break;
		case GameObjectType::VALVE:
		{
			real joystickX = 0.0f;
			real joystickY = 0.0f;

			if (m_ObjectInteractingWith)
			{
				i32 playerIndex = ((Player*)m_ObjectInteractingWith)->GetIndex();
				joystickX = gameContext.inputManager->GetGamepadAxisValue(playerIndex, InputManager::GamepadAxis::RIGHT_STICK_X);
				joystickY = gameContext.inputManager->GetGamepadAxisValue(playerIndex, InputManager::GamepadAxis::RIGHT_STICK_Y);
			}

			//Logger::LogInfo(std::to_string(pJoystickX) + " " + std::to_string(pJoystickY) + "\n" +
			//				std::to_string(joystickX) + " " + std::to_string(joystickY));

			i32 currentQuadrant = -1;

			real minimumExtensionLength = 0.35f;
			real extensionLength = glm::length(glm::vec2(joystickX, joystickY));
			if (extensionLength > minimumExtensionLength)
			{
				if (m_ValveMembers.stickStartTime == -1.0f)
				{
					m_ValveMembers.stickStartTime = gameContext.elapsedTime;
				}

				//if (pJoystickX < 0.0f && joystickX >= 0.0f)
				//{
				//	rotatingCW = (joystickY < 0.0f);
				//}
				//else if (pJoystickX >= 0.0f && joystickX < 0.0f)
				//{
				//	rotatingCW = (joystickY >= 0.0f);
				//}

				//if (pJoystickY < 0.0f && joystickY >= 0.0f)
				//{
				//	rotatingCW = (joystickX >= 0.0f);
				//}
				//else if (pJoystickY >= 0.0f && joystickY < 0.0f)
				//{
				//	rotatingCW = (joystickX < 0.0f);
				//}

				// Ignore previous values generated when stick was inside dead zone
				if (m_ValveMembers.previousQuadrant != -1)
				{
					real currentAngle = atan2(joystickY, joystickX) + PI;
					real previousAngle = atan2(m_ValveMembers.pJoystickY, m_ValveMembers.pJoystickX) + PI;
					// Asymptote occurs on left
					if (joystickX < 0.0f)
					{
						if (m_ValveMembers.pJoystickY < 0.0f && joystickY >= 0.0f)
						{
							// CCW
							currentAngle -= TWO_PI;
						}
						else if (m_ValveMembers.pJoystickY >= 0.0f && joystickY < 0.0f)
						{
							// CW
							currentAngle += TWO_PI;
						}
					}
					real currentRotationSpeed = (currentAngle - previousAngle) / gameContext.deltaTime;

					m_ValveMembers.averageRotationSpeeds.AddValue(currentRotationSpeed);
					m_ValveMembers.stickRotationSpeed = (-m_ValveMembers.averageRotationSpeeds.currentAverage) * 0.45f;
					real maxStickSpeed = 6.0f;
					m_ValveMembers.stickRotationSpeed = glm::clamp(m_ValveMembers.stickRotationSpeed, -maxStickSpeed, maxStickSpeed);

					//Logger::LogInfo(std::to_string(currentAngle) + ", " + 
					//				std::to_string(previousAngle) + 
					//				" diff: " + std::to_string(currentAngle - previousAngle) + 
					//				" cur: " + std::to_string(currentRotationSpeed) + 
					//				" avg: " + std::to_string(stickRotationSpeed));
				}

				if (joystickX > 0.0f)
				{
					currentQuadrant = (joystickY > 0.0f ? 2 : 1);
				}
				else
				{
					currentQuadrant = (joystickY > 0.0f ? 3 : 0);
				}
				m_ValveMembers.wasInQuadrantSinceIdle[currentQuadrant] = true;
			}
			else
			{
				m_ValveMembers.stickStartingQuadrant = -1;
				m_ValveMembers.stickStartTime = -1.0f;
				m_ValveMembers.stickRotationSpeed = 0.0f;
				m_ValveMembers.averageRotationSpeeds.Reset();
				for (i32 i = 0; i < 4; ++i)
				{
					m_ValveMembers.wasInQuadrantSinceIdle[i] = false;
				}
			}

			if (m_ValveMembers.previousQuadrant != currentQuadrant)
			{
				if (m_ValveMembers.previousQuadrant == -1)
				{
					m_ValveMembers.stickStartingQuadrant = currentQuadrant;
				}
				else
				{
					//bool rotatingCW = (previousQuadrant - currentQuadrant < 0);
					//if (previousQuadrant == 3 && currentQuadrant == 0)
					//{
					//	rotatingCW = true;
					//}
					//else if (previousQuadrant == 0 && currentQuadrant == 3)
					//{
					//	rotatingCW = false;
					//}

					//rotatingCW = rotatingCW;

					if (m_ValveMembers.stickStartingQuadrant == currentQuadrant)
					{
						bool touchedAllQuadrants = true;
						for (i32 i = 0; i < 4; ++i)
						{
							if (!m_ValveMembers.wasInQuadrantSinceIdle[i])
							{
								touchedAllQuadrants = false;
							}
						}

						if (touchedAllQuadrants)
						{
							Logger::LogInfo("Full loop (" + std::string(m_ValveMembers.rotatingCW ? "CW" : "CCW") + ")");

							//real rotationTime = gameContext.elapsedTime - stickStartTime;
							//stickRotationSpeed = glm::clamp(1.0f / rotationTime, 0.1f, 5.0f);

							//stickStartTime = -1.0f;

							//for (i32 i = 0; i < 4; ++i)
							//{
							//	if (i != currentQuadrant)
							//	{
							//		wasInQuadrantSinceIdle[i] = false;
							//	}
							//}
						}
					}
				}
			}

			if (m_ValveMembers.stickRotationSpeed != 0.0f)
			{
				m_ValveMembers.pStickRotationSpeed = m_ValveMembers.stickRotationSpeed;
			}
			else
			{
				m_ValveMembers.pStickRotationSpeed *= 0.8f;
			}

			m_ValveMembers.previousQuadrant = currentQuadrant;

			m_ValveMembers.pJoystickX = joystickX;
			m_ValveMembers.pJoystickY = joystickY;

			real rotationSpeed = 2.0f * gameContext.deltaTime * m_ValveMembers.pStickRotationSpeed;
			m_ValveMembers.rotation += (m_ValveMembers.rotatingCW ? -rotationSpeed : rotationSpeed);

			m_RigidBody->GetRigidBodyInternal()->activate(true);
			m_RigidBody->SetRotation(glm::quat(glm::vec3(0, m_ValveMembers.rotation, 0)));

		} break;
		case GameObjectType::NONE:
		default:
		{
		} break;
		}

		if (m_RigidBody)
		{
			m_Transform.MatchRigidBody(m_RigidBody, true);
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Update(gameContext);
		}
	}

	bool GameObject::SetInteractingWith(GameObject* gameObject)
	{
		m_ObjectInteractingWith = gameObject;
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

	void GameObject::SetVisible(bool visible, bool effectChildren)
	{
		m_bVisible = visible;

		if (effectChildren)
		{
			for (GameObject* child : m_Children)
			{
				child->SetVisible(visible, effectChildren);
			}
		}
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
				m_bInteractable = true;
			}
		} break;
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
